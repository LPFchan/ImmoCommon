# Technical Update: Crypto Standardization & Logic Fixes

**Date:** 2026-03-10
**Scope:** `immo_crypto` (RFC 3610), `immo_provisioning` logic, Fob stability, CI/CD validation.

---

## 1. RFC 3610 Cryptography Migration
Migrated from a custom CCM-like construction to standard **RFC 3610** to enable interoperability with standard libraries and improve auditability.

*   **Changes:**
    *   **AAD:** The 4-byte counter is now treated as Additional Authenticated Data (AAD) rather than message plaintext.
    *   **Formatting:** Updated B0 block flags (`Adata=1`) and inserted standard AAD length headers.
    *   **Encryption:** Only the 1-byte command is now counter-encrypted; the 4-byte counter remains plaintext but authenticated.
*   **Impact:** **Breaking change.** All devices must be reflashed. Tooling (`gen_mic.py`) now uses standard `cryptography.AESCCM`.

---

## 2. Provisioning Loop Fix
Corrected an inverted condition in `ensure_provisioned()` where the MCU would exit the provisioning loop when the key was blank, instead of staying trapped until successful setup.
*   **Fix:** Removed the `!` negation and renamed callback to `is_unprovisioned()` for clarity.

---

## 3. Hardware Stability: ADC Averaging
To prevent "flickering" low-battery LED indications caused by SAADC noise (~±15 mV), `readVbat_mv()` now uses a **4-sample average** with 1ms intervals. This smooths jitter near the 3.4V threshold without adding perceptible lag.

---

## 4. Observability & CI/CD
*   **Versioning:** Added `__TIMESTAMP__` and hardware `DEVICEID` to serial boot messages. This identifies both the specific firmware build and the physical hardware without requiring external scripts or libraries.
*   **CI Validation:** Added a `Verify Cryptography Implementation` step to `.github/workflows/release.yml`. It validates the current build against a "Golden Vector" using `gen_mic.py`, preventing cryptographic regressions.

---

## 5. File Summary
| Component | Affected Files |
| :--- | :--- |
| **Crypto** | `lib/immo_crypto.cpp`, `tools/test_vectors/gen_mic.py` |
| **Provisioning** | `lib/immo_provisioning.h/cpp`, `*/main.cpp` |
| **Stability** | `Uguisu/firmware/include/uguisu_nrf52840.h` |
| **CI/CD** | `.github/workflows/release.yml` |
