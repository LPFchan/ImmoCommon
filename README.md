# Guillemot (Receiver) — BLE Immobilizer for Ninebot Max G30

Guillemot is the **deck receiver** module of a two-module immobilizer system (Uguisu fob + Guillemot receiver) for the Ninebot Max G30. Guillemot validates short **encrypted BLE advertisements** from the fob and controls an **SR latch** that gates battery-to-ESC power (inline XT60 splice).

This repository currently focuses on **Guillemot receiver firmware + hardware design files**.

## Safety / legal

- This is a prototype security/power-interrupt device. Use at your own risk.
- Not affiliated with Segway-Ninebot.
- Do not test “lock” behavior while riding.

## Hardware summary (receiver)

- **MCU**: Seeed XIAO nRF52840
- **Latch control GPIO**: `D0=SET`, `D1=RESET`
- **Buzzer PWM**: `D3`
- **Power**: buck to 3.3 V, duty-cycled scanning while locked

## BLE protocol (payload)

Advertisement-carried payload (11 bytes):

| Field | Size |
|------|------|
| Device ID | 2 B |
| Counter | 4 B |
| Command | 1 B |
| MIC | 4 B |

Commands: `0x01=unlock`, `0x02=lock`. Receiver accepts only `counter > last_seen_counter` (anti-replay).

## Firmware (PlatformIO)

Firmware lives in `firmware/guillemot/`.

### Prereqs

- Install PlatformIO (Cursor/VSCode extension).
- Hardware: Seeed XIAO nRF52840 connected over USB.

### Build / upload

Open `firmware/guillemot/` in PlatformIO and use **Build** / **Upload**.

## Initialization

Guillemot initialises with **Whimbrel** ([github.com/LPFchan/Whimbrel](https://github.com/LPFchan/Whimbrel)), a browser-based provisioning app that injects the same AES-128 key into both Uguisu and Guillemot over Web Serial.

1. Open Whimbrel in Chrome or Edge, generate a secret, and flash the key fob (Uguisu).
2. Plug the **receiver** (Guillemot) into the PC via USB-C.
3. In Whimbrel, click **Flash Receiver**. Select the Guillemot serial port when prompted.

When the board is powered over USB (VBUS present), it waits up to **30 seconds** for a line of the form:

`PROV:GUILLEMOT_01:<32-hex-key>:00000000:<4-hex-CRC>` (CRC-16-CCITT of key; device replies `ERR:CHECKSUM` if mismatch)

It then stores the 16-byte key in internal flash (`/psk.bin`) and clears the counter log so the next fob advert is accepted.

