#pragma once
#include <stdint.h>
#include <stddef.h>

namespace immo {

// Returns true if VBUS is present.
bool prov_is_vbus_present();

// Wait for a valid PROV: string on Serial. Times out after `timeout_ms`.
// Format: PROV:<key_32_hex>:<counter_8_hex>:<checksum_4_hex>
// If valid, calls `on_success` with the parsed 16-byte key and counter.
// `on_success` should return true if it successfully wrote the key to storage.
bool prov_run_serial_loop(uint32_t timeout_ms, bool (*on_success)(const uint8_t key[16], uint32_t counter));

// Runs the provisioning loop if VBUS is present.
// If VBUS is present, it will run the loop once for `timeout_ms` to allow re-provisioning.
// If `is_provisioned` returns false after that, and VBUS is still present,
// it will loop indefinitely until provisioning succeeds or VBUS is disconnected.
void ensure_provisioned(
    uint32_t timeout_ms,
    bool (*on_success)(const uint8_t[16], uint32_t),
    void (*load_provisioning)(),
    bool (*is_provisioned)()
);

}  // namespace immo
