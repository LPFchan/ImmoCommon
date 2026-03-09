# Immogen — Ninebot G30 BLE Immobilizer Monorepo

**Immogen** is a three-part BLE immobilizer system for the Ninebot Max G30: [**Uguisu**](https://github.com/LPFchan/Immogen/tree/main/Uguisu#readme) (key fob), [**Guillemot**](https://github.com/LPFchan/Immogen/tree/main/Guillemot#readme) (deck receiver), and [**Whimbrel**](https://github.com/LPFchan/Whimbrel) (Web Serial provisioning app). Guillemot controls battery-to-ESC power via an inline XT60 splice; no valid BLE unlock means the scooter stays inoperable. Male/female pigtails make installation reversible—no permanent modification to the vehicle.

```
┌─────────────────┐       BLE advertisement        ┌──────────────────────┐
│  UGUISU (FOB)   │ ─────────────────────────────> │ GUILLEMOT (RECEIVER) │
│  XIAO nRF52840  │       (2.4 GHz, ~2 m)          │  XIAO nRF52840       │
└───────┬─────────┘                                └──────────┬───────────┘
        │                                                     │
        │                 ┌────────────────────┐              │
        └──────(USB-C)──> │ WHIMBREL (WEB APP) │ <──(USB-C)───┘
       (Provisioning)     │ Chrome / Edge      │    (Provisioning)
                          └────────────────────┘
```

Use [Whimbrel](https://github.com/LPFchan/Whimbrel) for firmware flashing and key provisioning via Web Serial.

---

## Repository Structure

| Path | Description |
| --- | --- |
| **`lib/`** | Shared C++ library (crypto, provisioning, storage) used by both firmwares |
| **`Guillemot/`** | Deck receiver—firmware, KiCad, BOM. See [Guillemot README](https://github.com/LPFchan/Immogen/tree/main/Guillemot#readme) |
| **`Uguisu/`** | Key fob—firmware, KiCad, BOM. See [Uguisu README](https://github.com/LPFchan/Immogen/tree/main/Uguisu#readme) |
| **`tools/`** | LED visualizer, BLE timing simulator, buzzer tuner (HTML); test vectors (`gen_mic.py`) |
| **`logs/`** | Migration reports and guides |

---

## Components

| Component | Role |
| --- | --- |
| **Uguisu** | System OFF → button press → BLE broadcast ~2 s → System OFF |
| **Guillemot** | Duty-cycled scan (25 ms / 500 ms, 5%), validates adverts → SR latch → power gate |
| **lib/** | AES-128-CCM MIC, provisioning loop, counter storage (LittleFS) |
| **Whimbrel** | Web app for flashing and provisioning |

---

## BLE Protocol

Advertisement-based; no persistent connection. Both devices share the same company ID and pre-shared key (PSK). For implementation details, see [Guillemot](https://github.com/LPFchan/Immogen/tree/main/Guillemot#readme) and [Uguisu](https://github.com/LPFchan/Immogen/tree/main/Uguisu#readme) READMEs.

### Payload (13 bytes after 2-byte company ID)

| Offset | Size | Field   | Purpose                    |
| ------ | ---- | ------- | -------------------------- |
| 0      | 4    | counter | Anti-replay, monotonic     |
| 4      | 1    | command | 0x01 = Unlock, 0x02 = Lock |
| 5      | 8    | mic     | AES-128-CCM auth tag       |

### Test vectors

```bash
python3 tools/test_vectors/gen_mic.py --company-id 0xFFFF --counter 0 --command 1 --key <32 hex chars>
```

---

## Quick Start

Pre-built firmware is available at [Releases](https://github.com/LPFchan/Immogen/releases). Use [Whimbrel](https://github.com/LPFchan/Whimbrel) to flash firmware and provision keys.

**Build from source:**

```bash
# Build receiver (Guillemot)
cd Guillemot/firmware && pio run

# Build fob (Uguisu)
cd Uguisu/firmware && pio run
```

---

## Further Reading

- **Deck receiver (Guillemot):** [Guillemot README](https://github.com/LPFchan/Immogen/tree/main/Guillemot#readme) — hardware, PCB layout, BOM, operation
- **Key fob (Uguisu):** [Uguisu README](https://github.com/LPFchan/Immogen/tree/main/Uguisu#readme) — hardware, GPIO, LED behaviour, boot flow
- **Provisioning & flashing:** [Whimbrel](https://github.com/LPFchan/Whimbrel)

---

## TBD

- Uguisu enclosure CAD (3D print).
- First-build validation: PPK2 power, BLE range through deck, pre-charge waveform.

---

## Safety & Legal

- Prototype security/power-interrupt device. Use at your own risk.
- Not affiliated with Segway-Ninebot.
- **Do not test lock behavior while riding.**
