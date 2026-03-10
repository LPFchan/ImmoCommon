# Codebase Assessment — Post-Fix Review

*Date: 2026-03-10*

---

## 1. `ensure_provisioned()` — Inverted Retry Loop

**File:** `lib/immo_provisioning.cpp:143`

`key_is_all_zeros` returns `true` when the key IS blank. The `!` negation flips this:

| State | Loop runs? | Expected |
|-------|---|---|
| Key blank (needs provisioning) | No | Yes |
| Key valid (provisioned) | Yes | No |

The safety-net retry loop is backwards. Low impact in practice (first 30-sec window handles most cases; VBUS disconnect exits the loop), but the documented behaviour — "loop indefinitely until provisioning succeeds" — is broken.

**Fix:** Remove the `!` and rename the parameter to match what the callback actually does:

```cpp
// immo_provisioning.h
bool (*is_unprovisioned)()  // returns true when key is blank

// immo_provisioning.cpp
while (is_unprovisioned && is_unprovisioned() && prov_is_vbus_present()) {
```

---

## 2. `gen_mic.py` — Stale After CCM Encryption Fix

**File:** `tools/test_vectors/gen_mic.py:57-61`

The script still outputs the pre-Fix-#5 format (full plaintext + tag). The firmware now outputs `counter_plain(4) + command_encrypted(1) + mic(8)`.

Worse, the firmware's CCM AAD handling is non-standard: all 5 bytes go into CBC-MAC as "message," and only byte 4 is encrypted using `S1[0]` (not `S1[4]` as standard CCM would). The Python `cryptography.AESCCM` class cannot replicate this with standard parameters — a drop-in fix isn't possible.

**Fix options:**
1. Reimplement the script with manual CCM matching the firmware's construction
2. Migrate the firmware to standard RFC 3610 AAD formatting (see trade-off below)
3. At minimum, add a disclaimer to the script and a comment in `immo_crypto.cpp`:

```cpp
// Non-standard CCM: full msg (counter + command) feeds CBC-MAC as "message" with no AAD block.
// Only bytes [aad_len:] are counter-encrypted, starting at S1[0].
// Differs from RFC 3610 but is cryptographically sound for this use case.
```

**RFC 3610 migration trade-off:**

| | Notes |
|---|---|
| **Pro** | `gen_mic.py` fixed trivially — pass `aad=counter_bytes` to Python's `AESCCM`, no manual reimplementation |
| **Pro** | Any RFC 3610-compliant library verifies payloads out of the box |
| **Pro** | Code maps 1:1 to the spec; straightforward to audit |
| **Pro** | Extending AAD fields (device ID, firmware version) is well-defined |
| **Con** | Breaking change — B0 flags change (`0x19` → `0x59`), extra CBC-MAC block added, MIC values change entirely. Both devices must be reflashed simultaneously |
| **Con** | One extra AES-ECB call per operation (50% more CBC-MAC blocks for this message size) |
| **Con** | `msg_len` in B0 must change from 5 to 1; incorrect values silently break MIC generation |
| **Con** | Risk of introducing a bug while migrating a working crypto implementation |

---

## 3. `readVbat_mv()` — Single ADC Sample Near Threshold

**File:** `Uguisu/firmware/include/uguisu_nrf52840.h:59`

SAADC noise is ~±15 mV at this scaling. At the 3400 mV low-battery threshold, consecutive presses can alternate between normal and low-battery LED feedback.

**Fix:** 4-sample average, ~4 ms overhead:

```cpp
uint32_t sum = 0;
for (int i = 0; i < 4; i++) { sum += analogRead(UGUISU_VBAT_PIN); delay(1); }
const uint16_t mv = (uint16_t)((sum / 4) * 6000u / 4096u);
```

---

## 4. No Firmware Version in Boot Message

Neither firmware identifies its version over serial. Undiagnosable in the field without reflashing.

**Fix:** `Serial.println("BOOTED:Guillemot-" __DATE__);`

---

## 5. CI Has No Test Step

`release.yml` compiles both firmwares but runs no validation. Crypto regressions would ship silently.

**Fix:** Add a step running `gen_mic.py` against known-good vectors once the script is updated.

---

| Priority | Item |
|----------|------|
| High | `ensure_provisioned` retry loop logic |
| High | `gen_mic.py` stale test vectors |
| Medium | Battery ADC averaging |
| Medium | Firmware version in boot message |
| Low | CCM non-standard construction comment |
| Low | CI test step |

