# Uguisu — Ninebot G30 BLE Immobilizer Fob

Uguisu is the **BLE key fob** module of a three-part immobilizer system (Uguisu fob + [Guillemot](https://github.com/LPFchan/Guillemot) receiver + [Whimbrel](https://github.com/LPFchan/Whimbrel) web app) for the Ninebot Max G30. It broadcasts short, encrypted BLE advertisements on button press.

This repository contains the **Uguisu fob firmware and hardware design files**. Note that shared protocol and cryptography logic is implemented in the [ImmoCommon](https://github.com/LPFchan/ImmoCommon) submodule.

## Hardware

- **MCU**: Seeed Studio XIAO nRF52840
- **PCB Design**: KiCad (`Uguisu.kicad_sch` / `Uguisu.kicad_pcb`)

## Firmware

The firmware is a [PlatformIO](https://platformio.org/) project located in `firmware/uguisu/`.
- Open the folder in VSCode/Cursor with the PlatformIO extension.
- Build and upload via USB.

## Provisioning

Uguisu must be initialized with the [Whimbrel](https://github.com/LPFchan/Whimbrel) web app.
- Connect the fob via USB and use Whimbrel to inject a shared AES-128 key.
- The key and anti-replay counter are stored persistently in internal flash.

## Protocol

- **BLE**: Broadcasts Manufacturer Specific Data containing a device ID, counter, command, and 4-byte AES-CCM Message Integrity Code (MIC).
- **Shared Library**: Payload generation and cryptography are handled by [ImmoCommon](https://github.com/LPFchan/ImmoCommon).
- *Note: A script to generate test vectors is available in `tools/test_vectors/`.*

## Safety & Notes

- **Safety**: This is a prototype security device. Use at your own risk. Do not test lock behavior while riding.
- **Legal**: Not affiliated with Segway-Ninebot.
- **Repo**: Please avoid committing KiCad per-user state (`*.kicad_prl`) or lock files.
