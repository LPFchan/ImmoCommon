# Immogen — Ninebot G30 BLE Immobilizer

**Immogen** is a three-part BLE immobilizer system for the Ninebot Max G30, consisting of **[Uguisu](https://github.com/LPFchan/Immogen/tree/main/Uguisu#readme)** (key fob), **[Guillemot](https://github.com/LPFchan/Immogen/tree/main/Guillemot#readme)** (deck receiver), and **[Whimbrel](https://github.com/LPFchan/Whimbrel)** (Web Serial provisioning app).

Guillemot sits inline between the battery and ESC via an XT60 splice. Without a valid BLE unlock from Uguisu, the scooter stays inoperable. Male/female pigtails keep the install reversible—no permanent modification required.

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

## Repository Structure


| Path             | Description                                                                                                                                                                       |
| ---------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Uguisu/`    | Key fob — firmware, KiCad, BOM. Button press → 2 s BLE broadcast → sleep. [README](https://github.com/LPFchan/Immogen/tree/main/Uguisu#readme)                                 |
| `Guillemot/` | Deck receiver — firmware, KiCad, BOM. Duty-cycled scan (5%), validates adverts → SR latch → power gate. [README](https://github.com/LPFchan/Immogen/tree/main/Guillemot#readme) |
| `lib/`       | Shared C++ library: AES-128-CCM MIC, provisioning loop, LittleFS counter storage                                                                                                  |
| `tools/`     | LED visualizer, BLE timing simulator, buzzer tuner (HTML); MIC test vectors (`gen_mic.py`)                                                                                        |
| `logs/`      | Technical writeups: architecture reviews, BLE power analysis, design decisions, migration guides, documentation reports                                                              |


## BLE Protocol

Advertisement-only; no persistent connection. Both devices share a company ID, a 2-byte magic prefix (`0x494D`), and a pre-shared key (PSK). Each advertisement carries a 13-byte payload after the manufacturer specific data header (company ID + magic):


| Offset | Size | Field   | Purpose                    |
| ------ | ---- | ------- | -------------------------- |
| 0      | 4    | counter | Anti-replay, monotonic (AAD)|
| 4      | 1    | command | 0x01 = Unlock, 0x02 = Lock (Encrypted) |
| 5      | 8    | mic     | AES-128-CCM auth tag       |

## Quick Start

Pre-built firmware is available on the [Releases](https://github.com/LPFchan/Immogen/releases) page. Use [Whimbrel](https://github.com/LPFchan/Whimbrel) to flash and provision keys via Web Serial.

**Build from source:**

```bash
# Deck receiver
cd Guillemot/firmware && pio run

# Key fob
cd Uguisu/firmware && pio run
```

## Roadmap

- Uguisu enclosure CAD (3D print).
- First-build validation: PPK2 power, BLE range through deck, pre-charge waveform.

## Safety & Legal

- Prototype security/power-interrupt device. Use at your own risk.
- Not affiliated with Segway-Ninebot.
- **Do not test lock behavior while riding.**

