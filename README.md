# Immogen — Ninebot G30 BLE Immobilizer Monorepo

**Immogen** (formerly ImmoCommon) is the monorepo for the BLE immobilizer system for the Ninebot Max G30. It contains:

- **`lib/`** — Shared C++ library (crypto, provisioning, storage) used by both firmwares
- **`Guillemot/`** — Deck receiver firmware and hardware (validates BLE, controls power gate)
- **`Uguisu/`** — Key fob firmware and hardware (broadcasts encrypted BLE on button press)
- **`tools/`** — HTML utilities (LED visualizer, BLE timing simulator, buzzer tuner)

Use [Whimbrel](https://github.com/LPFchan/Whimbrel) for firmware flashing and key provisioning via Web Serial.

---

## Quick Start

```bash
# Build receiver (Guillemot)
cd Guillemot/firmware && pio run

# Build fob (Uguisu)
cd Uguisu/firmware && pio run
```

---

## Architecture

| Component | Role |
| --- | --- |
| **Uguisu** | System OFF → button press → BLE broadcast ~2 s → System OFF |
| **Guillemot** | Duty-cycled scan (20 ms / 2 s), validates adverts → SR latch → power gate |
| **lib/** | AES-128-CCM MIC, provisioning loop, counter storage (LittleFS) |
| **Whimbrel** | Web app for flashing and provisioning |

---

## BLE Protocol

Advertisement-based; no persistent connection. Both devices share the same company ID and pre-shared key (PSK).

### Payload (13 bytes after 2-byte company ID)

| Offset | Size | Field   | Purpose                    |
| ------ | ---- | ------- | -------------------------- |
| 0      | 4    | counter | Anti-replay, monotonic     |
| 4      | 1    | command | 0x01 = Unlock, 0x02 = Lock |
| 5      | 8    | mic     | AES-128-CCM auth tag       |

### Test vectors

```bash
python3 lib/tools/test_vectors/gen_mic.py --company-id 0xFFFF --counter 0 --command 1 --key <32 hex chars>
```

---

## Releases

Tag and push to create a release (both firmwares built, four artifacts):

```bash
git tag v1.0.0
git push origin v1.0.0
```

Artifacts: `guillemot-1.0.0.hex`, `guillemot-1.0.0.zip`, `uguisu-1.0.0.hex`, `uguisu-1.0.0.zip`.

---

## Safety & Legal

- Prototype security/power-interrupt device. Use at your own risk.
- Not affiliated with Segway-Ninebot.
- **Do not test lock behavior while riding.**
