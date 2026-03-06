#include <Arduino.h>

#include <bluefruit.h>
#include <nrf_soc.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "guillemot_config.h"

using namespace Adafruit_LittleFS_Namespace;

namespace {

// Default PSK when not yet provisioned via Whimbrel (zeros).
static constexpr uint8_t k_default_psk[16] = {0};

static constexpr const char* COUNTER_LOG_PATH = "/ctr.log";
static constexpr const char* PSK_STORAGE_PATH = "/psk.bin";
static constexpr size_t COUNTER_LOG_MAX_BYTES = 4096;
static constexpr size_t PROV_TIMEOUT_MS = 30000;
static constexpr size_t PROV_KEY_HEX_LEN = 32;
static constexpr size_t PROV_COUNTER_HEX_LEN = 8;

// Runtime key: loaded from flash (Whimbrel-provisioned) or compile-time default.
uint8_t g_psk[16];

enum class Command : uint8_t {
  Unlock = 0x01,
  Lock = 0x02,
};

struct Payload {
  uint16_t device_id;
  uint32_t counter;
  Command command;
  uint8_t mic[MIC_LEN];
};

struct CounterRecord {
  uint16_t device_id;
  uint32_t counter;
  uint32_t crc32;
};

uint32_t crc32_ieee(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

uint32_t record_crc(const CounterRecord& r) {
  uint8_t buf[sizeof(r.device_id) + sizeof(r.counter)];
  memcpy(buf + 0, &r.device_id, sizeof(r.device_id));
  memcpy(buf + sizeof(r.device_id), &r.counter, sizeof(r.counter));
  return crc32_ieee(buf, sizeof(buf));
}

class CounterStore {
public:
  bool begin() {
    return InternalFS.begin();
  }

  void load() {
    last_device_id_ = 0;
    last_counter_ = 0;

    Adafruit_LittleFS_Namespace::File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_READ));
    if (!f) return;

    CounterRecord rec{};
    while (f.read(reinterpret_cast<void*>(&rec), sizeof(rec)) == sizeof(rec)) {
      if (record_crc(rec) != rec.crc32) continue;
      last_device_id_ = rec.device_id;
      last_counter_ = rec.counter;
    }
  }

  uint32_t lastCounterFor(uint16_t device_id) const {
    if (device_id != last_device_id_) return 0;
    return last_counter_;
  }

  void update(uint16_t device_id, uint32_t counter) {
    rotateIfNeeded_();

    CounterRecord rec{};
    rec.device_id = device_id;
    rec.counter = counter;
    rec.crc32 = record_crc(rec);

    Adafruit_LittleFS_Namespace::File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_WRITE));
    if (!f) return;
    f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
    f.flush();

    last_device_id_ = device_id;
    last_counter_ = counter;
  }

private:
  void rotateIfNeeded_() {
    Adafruit_LittleFS_Namespace::File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_READ));
    if (!f) return;
    const size_t sz = f.size();
    f.close();

    if (sz < COUNTER_LOG_MAX_BYTES) return;
    InternalFS.remove("/ctr.old");
    InternalFS.rename(COUNTER_LOG_PATH, "/ctr.old");
  }

  uint16_t last_device_id_{0};
  uint32_t last_counter_{0};
};

CounterStore g_store;

// --- Whimbrel provisioning (Web Serial PROV: line, 30s timeout) ---

static bool prov_is_vbus_present() {
#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA)
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
#else
  return false;
#endif
}

// Decode two hex chars to one byte; returns false if invalid.
static bool hex_byte(const char* hex, uint8_t* out) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  int hi = nib(hex[0]);
  int lo = nib(hex[1]);
  if (hi < 0 || lo < 0) return false;
  *out = static_cast<uint8_t>((hi << 4) | lo);
  return true;
}

// CRC-16-CCITT (poly 0x1021, init 0xFFFF) over data. Used for PROV checksum.
static uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int k = 0; k < 8; k++)
      crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
  }
  return crc;
}

// Wait for "PROV:DEVICE_ID:KEY_HEX:COUNTER_HEX:CHECKSUM_HEX". CHECKSUM is CRC-16-CCITT of key (4 hex chars).
// On success: write 16-byte key to PSK_STORAGE_PATH, remove counter log, print ACK:PROV_SUCCESS, return true.
// On malformed: print ERR:MALFORMED, return false. On timeout: return false.
static bool prov_run_serial_loop() {
  const uint32_t deadline = millis() + PROV_TIMEOUT_MS;
  char line[128];
  size_t len = 0;

  while (millis() < deadline && len < sizeof(line) - 1) {
    if (!Serial.available()) {
      delay(10);
      continue;
    }
    int c = Serial.read();
    if (c < 0) continue;
    if (c == '\n' || c == '\r') {
      line[len] = '\0';
      if (len == 0) continue;

      if (strncmp(line, "PROV:", 5) != 0) {
        Serial.println("ERR:MALFORMED");
        return false;
      }
      const char* rest = line + 5;
      const char* col1 = strchr(rest, ':');
      const char* col2 = col1 ? strchr(col1 + 1, ':') : nullptr;
      const char* col3 = col2 ? strchr(col2 + 1, ':') : nullptr;
      if (!col1 || !col2 || !col3) {
        Serial.println("ERR:MALFORMED");
        return false;
      }
      const char* key_hex = col1 + 1;
      const char* counter_hex = col2 + 1;
      const char* checksum_hex = col3 + 1;
      size_t key_len = (size_t)(col2 - key_hex);
      size_t counter_len = (size_t)(col3 - counter_hex);
      if (key_len != PROV_KEY_HEX_LEN || counter_len != PROV_COUNTER_HEX_LEN) {
        Serial.println("ERR:MALFORMED");
        return false;
      }
      if (strlen(checksum_hex) < 4) {
        Serial.println("ERR:MALFORMED");
        return false;
      }
      uint8_t key_buf[16];
      for (size_t i = 0; i < 16; i++) {
        if (!hex_byte(key_hex + i * 2, &key_buf[i])) {
          Serial.println("ERR:MALFORMED");
          return false;
        }
      }
      uint8_t checksum_hi = 0, checksum_lo = 0;
      if (!hex_byte(checksum_hex, &checksum_hi) || !hex_byte(checksum_hex + 2, &checksum_lo)) {
        Serial.println("ERR:MALFORMED");
        return false;
      }
      uint16_t checksum_received = (uint16_t)checksum_hi << 8 | checksum_lo;
      if (crc16_ccitt(key_buf, 16) != checksum_received) {
        Serial.println("ERR:CHECKSUM");
        return false;
      }
      InternalFS.remove(PSK_STORAGE_PATH);
      Adafruit_LittleFS_Namespace::File f(
          InternalFS.open(PSK_STORAGE_PATH, FILE_O_WRITE));
      if (!f) {
        Serial.println("ERR:WRITE");
        return false;
      }
      f.write(key_buf, 16);
      f.flush();
      f.close();

      // Verify: read back and compare key.
      Adafruit_LittleFS_Namespace::File fr(
          InternalFS.open(PSK_STORAGE_PATH, FILE_O_READ));
      if (!fr || fr.size() != 16) {
        Serial.println("ERR:VERIFY_FAIL");
        return false;
      }
      uint8_t read_key[16];
      if (fr.read(read_key, 16) != 16) {
        Serial.println("ERR:VERIFY_FAIL");
        return false;
      }
      bool key_ok = true;
      for (int i = 0; i < 16; i++)
        if (read_key[i] != key_buf[i]) { key_ok = false; break; }
      if (!key_ok) {
        Serial.println("ERR:VERIFY_FAIL");
        return false;
      }

      InternalFS.remove(COUNTER_LOG_PATH);
      Serial.println("ACK:PROV_SUCCESS");
      memcpy(g_psk, key_buf, 16);
      return true;
    }
    line[len++] = (char)c;
  }
  return false;
}

static bool key_is_all_zeros() {
  for (int i = 0; i < 16; i++)
    if (g_psk[i] != 0) return false;
  return true;
}

// Load g_psk from InternalFS if /psk.bin exists (16 bytes), else use compile-time default.
static void load_psk_from_storage() {
  Adafruit_LittleFS_Namespace::File f(
      InternalFS.open(PSK_STORAGE_PATH, FILE_O_READ));
  if (f && f.size() == 16 && f.read(g_psk, 16) == 16) {
    return;
  }
  memcpy(g_psk, k_default_psk, 16);
}

bool aes128_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  nrf_ecb_hal_data_t ecb{};
  memcpy(ecb.key, key, 16);
  memcpy(ecb.cleartext, in, 16);

  const uint32_t err = sd_ecb_block_encrypt(&ecb);
  if (err != NRF_SUCCESS) return false;

  memcpy(out, ecb.ciphertext, 16);
  return true;
}

void xor_block(uint8_t dst[16], const uint8_t a[16], const uint8_t b[16]) {
  for (size_t i = 0; i < 16; i++) dst[i] = a[i] ^ b[i];
}

void ccm_build_nonce(uint16_t device_id, uint32_t counter, uint8_t nonce[13]) {
  nonce[0] = static_cast<uint8_t>(device_id & 0xFF);
  nonce[1] = static_cast<uint8_t>((device_id >> 8) & 0xFF);

  nonce[2] = static_cast<uint8_t>(counter & 0xFF);
  nonce[3] = static_cast<uint8_t>((counter >> 8) & 0xFF);
  nonce[4] = static_cast<uint8_t>((counter >> 16) & 0xFF);
  nonce[5] = static_cast<uint8_t>((counter >> 24) & 0xFF);

  for (size_t i = 6; i < 13; i++) nonce[i] = 0;
}

bool ccm_mic_4(const uint8_t key[16], const uint8_t nonce[13], const uint8_t* msg, size_t msg_len, uint8_t out_mic[4]) {
  if (msg_len > 0xFFFFu) return false;

  const uint8_t L = 2;
  const uint8_t M = 4;
  const uint8_t flags_b0 = static_cast<uint8_t>(((M - 2) / 2) << 3) | static_cast<uint8_t>(L - 1);

  uint8_t b0[16]{};
  b0[0] = flags_b0;
  memcpy(&b0[1], nonce, 13);
  b0[14] = static_cast<uint8_t>((msg_len >> 8) & 0xFF);
  b0[15] = static_cast<uint8_t>(msg_len & 0xFF);

  uint8_t x[16]{};
  uint8_t tmp[16]{};
  xor_block(tmp, x, b0);
  if (!aes128_ecb_encrypt(key, tmp, x)) return false;

  size_t offset = 0;
  while (offset < msg_len) {
    uint8_t block[16]{};
    const size_t n = min(static_cast<size_t>(16), msg_len - offset);
    memcpy(block, msg + offset, n);
    xor_block(tmp, x, block);
    if (!aes128_ecb_encrypt(key, tmp, x)) return false;
    offset += n;
  }

  uint8_t a0[16]{};
  a0[0] = static_cast<uint8_t>(L - 1);
  memcpy(&a0[1], nonce, 13);
  a0[14] = 0;
  a0[15] = 0;

  uint8_t s0[16]{};
  if (!aes128_ecb_encrypt(key, a0, s0)) return false;

  for (size_t i = 0; i < M; i++) out_mic[i] = static_cast<uint8_t>(x[i] ^ s0[i]);
  return true;
}

bool constant_time_eq(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= (a[i] ^ b[i]);
  return diff == 0;
}

void latch_set_pulse() {
  digitalWrite(PIN_LATCH_RESET, LOW);
  digitalWrite(PIN_LATCH_SET, HIGH);
  delay(LATCH_PULSE_MS);
  digitalWrite(PIN_LATCH_SET, LOW);
}

void latch_reset_pulse() {
  digitalWrite(PIN_LATCH_SET, LOW);
  digitalWrite(PIN_LATCH_RESET, HIGH);
  delay(LATCH_PULSE_MS);
  digitalWrite(PIN_LATCH_RESET, LOW);
}

void buzzer_tone_ms(uint16_t duration_ms) {
  tone(PIN_BUZZER, BUZZER_HZ, duration_ms);
  delay(duration_ms);
  noTone(PIN_BUZZER);
}

bool parse_payload_from_report(ble_gap_evt_adv_report_t* report, Payload& out) {
  uint8_t msd[2 + PAYLOAD_LEN];
  const uint8_t len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, msd, sizeof(msd));
  if (len != sizeof(msd)) return false;

  const uint16_t company_id = static_cast<uint16_t>(msd[0] | (static_cast<uint16_t>(msd[1]) << 8));
  if (company_id != MSD_COMPANY_ID) return false;

  const uint8_t* p = msd + 2;
  out.device_id = static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
  out.counter = static_cast<uint32_t>(p[2] | (static_cast<uint32_t>(p[3]) << 8) | (static_cast<uint32_t>(p[4]) << 16) |
                                      (static_cast<uint32_t>(p[5]) << 24));
  out.command = static_cast<Command>(p[6]);
  memcpy(out.mic, p + 7, MIC_LEN);
  return true;
}

bool verify_payload(const Payload& pl) {
  uint8_t nonce[13];
  ccm_build_nonce(pl.device_id, pl.counter, nonce);

  uint8_t msg[2 + 4 + 1];
  msg[0] = static_cast<uint8_t>(pl.device_id & 0xFF);
  msg[1] = static_cast<uint8_t>((pl.device_id >> 8) & 0xFF);
  msg[2] = static_cast<uint8_t>(pl.counter & 0xFF);
  msg[3] = static_cast<uint8_t>((pl.counter >> 8) & 0xFF);
  msg[4] = static_cast<uint8_t>((pl.counter >> 16) & 0xFF);
  msg[5] = static_cast<uint8_t>((pl.counter >> 24) & 0xFF);
  msg[6] = static_cast<uint8_t>(pl.command);

  uint8_t expected[MIC_LEN];
  if (!ccm_mic_4(g_psk, nonce, msg, sizeof(msg), expected)) return false;
  return constant_time_eq(expected, pl.mic, MIC_LEN);
}

void handle_valid_command(const Payload& pl) {
  const uint32_t last = g_store.lastCounterFor(pl.device_id);
  if (pl.counter <= last) return;

  switch (pl.command) {
    case Command::Unlock:
      latch_set_pulse();
      buzzer_tone_ms(BUZZER_UNLOCK_MS);
      g_store.update(pl.device_id, pl.counter);
      break;
    case Command::Lock:
      buzzer_tone_ms(BUZZER_LOCK_MS);
      latch_reset_pulse();
      g_store.update(pl.device_id, pl.counter);
      break;
    default:
      break;
  }
}

void scan_callback(ble_gap_evt_adv_report_t* report) {
  Payload pl{};
  if (!parse_payload_from_report(report, pl)) return;
  if (!verify_payload(pl)) return;
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
  }

  load_psk_from_storage();
  // When on USB, always offer one 30s provisioning window (re-provision or first time).
  if (prov_is_vbus_present()) {
    prov_run_serial_loop();
    load_psk_from_storage();
  }
  // If still not provisioned (key all zeros) and on USB, stay in provisioning until PROV or unplug.
  while (key_is_all_zeros() && prov_is_vbus_present()) {
    prov_run_serial_loop();
    load_psk_from_storage();
  }

  g_store.load();

  Bluefruit.begin(0, 1);
  Bluefruit.setName("Guillemot");

  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.filterMSD(MSD_COMPANY_ID);
  Bluefruit.Scanner.setIntervalMS(SCAN_INTERVAL_MS, SCAN_WINDOW_MS);
  Bluefruit.Scanner.start(0);

  Serial.println("Guillemot scanning");
  Serial.println("BOOTED:Guillemot");
}

void loop() {
  sd_app_evt_wait();
}

