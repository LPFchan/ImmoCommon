#include <Arduino.h>

#include <bluefruit.h>
#include <nrf_soc.h>
#include <nrf_wdt.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "guillemot_config.h"
#include <ImmoCommon.h>

using namespace Adafruit_LittleFS_Namespace;

namespace {

static constexpr const char* COUNTER_LOG_PATH = "/ctr.log";
static constexpr const char* OLD_COUNTER_LOG_PATH = "/ctr.old";

// Runtime key: loaded from flash (Whimbrel-provisioned) or compile-time default.
uint8_t g_psk[16];

immo::CounterStore g_store(COUNTER_LOG_PATH, OLD_COUNTER_LOG_PATH, immo::DEFAULT_COUNTER_LOG_MAX_BYTES);

bool on_provision_success(const uint8_t key[16], uint32_t counter) {
  return immo::prov_write_and_verify(immo::DEFAULT_PROV_PATH, key, counter, g_store, g_psk);
}

static bool key_is_all_zeros() { return immo::is_key_blank(g_psk); }

static void load_psk_from_storage() {
  immo::prov_load_key_or_zero(immo::DEFAULT_PROV_PATH, g_psk);
}

void buzzer_tone_ms(uint16_t hz, uint16_t duration_ms) {
  tone(PIN_BUZZER, hz, duration_ms);
  delay(duration_ms);
  noTone(PIN_BUZZER);
}

bool parse_payload_from_report(ble_gap_evt_adv_report_t* report, uint8_t ct[immo::MSG_LEN], uint8_t mic[immo::MIC_LEN]) {
  uint8_t msd[4 + immo::PAYLOAD_LEN];
  const uint8_t len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, msd, sizeof(msd));
  if (len != sizeof(msd)) return false;

  const uint16_t company_id = static_cast<uint16_t>(msd[0] | (static_cast<uint16_t>(msd[1]) << 8));
  if (company_id != MSD_COMPANY_ID) return false;

  const uint16_t magic = static_cast<uint16_t>(msd[2] | (static_cast<uint16_t>(msd[3]) << 8));
  if (magic != immo::IMMOGEN_MAGIC) return false;

  const uint8_t* p = msd + 4;
  memcpy(ct, p, immo::MSG_LEN);
  memcpy(mic, p + immo::MSG_LEN, immo::MIC_LEN);
  return true;
}

bool verify_payload(const uint8_t ct[immo::MSG_LEN], const uint8_t mic[immo::MIC_LEN], immo::Payload& out_pl) {
  // Extract counter (first 4 bytes of ct, which are unencrypted AAD)
  const uint32_t counter = static_cast<uint32_t>(ct[0] | (static_cast<uint32_t>(ct[1]) << 8) | (static_cast<uint32_t>(ct[2]) << 16) | (static_cast<uint32_t>(ct[3]) << 24));

  uint8_t nonce[immo::NONCE_LEN];
  immo::build_nonce(counter, nonce);

  uint8_t msg[immo::MSG_LEN];
  uint8_t expected[immo::MIC_LEN];
  if (!immo::ccm_auth_decrypt(g_psk, nonce, ct, immo::MSG_LEN, 4, msg, expected)) return false;

  if (!immo::constant_time_eq(expected, mic, immo::MIC_LEN)) return false;

  out_pl.counter = counter;
  out_pl.command = static_cast<immo::Command>(msg[4]);
  memcpy(out_pl.mic, mic, immo::MIC_LEN);
  return true;
}

void handle_valid_command(const immo::Payload& pl) {
  const uint32_t last = g_store.lastCounter();
  if (pl.counter <= last) return;

  g_store.update(pl.counter);

  switch (pl.command) {
    case immo::Command::Unlock:
      latch_set_pulse();
      buzzer_tone_ms(BUZZER_LOW_HZ,  BUZZER_LOW_MS);
      buzzer_tone_ms(BUZZER_HIGH_HZ, BUZZER_HIGH_MS);
      break;
    case immo::Command::Lock:
      buzzer_tone_ms(BUZZER_HIGH_HZ, BUZZER_HIGH_MS);
      buzzer_tone_ms(BUZZER_LOW_HZ,  BUZZER_LOW_MS);
      latch_reset_pulse();
      break;
    default:
      // Ignore unknown commands
      break;
  }
}

void scan_callback(ble_gap_evt_adv_report_t* report) {
  uint8_t ct[immo::MSG_LEN];
  uint8_t mic[immo::MIC_LEN];
  if (!parse_payload_from_report(report, ct, mic)) return;
  if (key_is_all_zeros()) return;
  
  immo::Payload pl{};
  if (!verify_payload(ct, mic, pl)) return;
  handle_valid_command(pl);
}

}  // namespace

void setup() {
  pinMode(PIN_LATCH_SET, OUTPUT);
  pinMode(PIN_LATCH_RESET, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LATCH_SET, LOW);
  digitalWrite(PIN_LATCH_RESET, LOW);
  noTone(PIN_BUZZER);

  Serial.begin(115200);
  delay(50);

  if (!g_store.begin()) {
    Serial.println("InternalFS begin failed");
    immo::led_error_loop(PIN_ERROR_LED);
  }

  load_psk_from_storage();
  immo::ensure_provisioned(immo::DEFAULT_PROV_TIMEOUT_MS, on_provision_success, load_psk_from_storage, key_is_all_zeros);

  g_store.load();

  Bluefruit.begin(0, 1);
  Bluefruit.setName("Guillemot");

  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.filterMSD(MSD_COMPANY_ID);
  Bluefruit.Scanner.setIntervalMS(SCAN_INTERVAL_MS, SCAN_WINDOW_MS);
  Bluefruit.Scanner.start(0);

  nrf_wdt_behaviour_set(NRF_WDT, NRF_WDT_BEHAVIOUR_RUN_SLEEP);
  nrf_wdt_reload_value_set(NRF_WDT, (8000 * 32768) / 1000); // 8 second timeout
  nrf_wdt_reload_request_enable(NRF_WDT, NRF_WDT_RR0);
  nrf_wdt_task_trigger(NRF_WDT, NRF_WDT_TASK_START);

  Serial.println("Guillemot scanning");
  Serial.print("BOOTED: Guillemot-");
  Serial.println(__TIMESTAMP__);
  Serial.print("HWID: ");
  Serial.print(NRF_FICR->DEVICEID[0], HEX);
  Serial.println(NRF_FICR->DEVICEID[1], HEX);
}

void loop() {
  nrf_wdt_reload_request_set(NRF_WDT, NRF_WDT_RR0);
  sd_app_evt_wait();
}
