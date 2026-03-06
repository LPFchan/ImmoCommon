# Uguisu (BLE fob) — Ninebot G30 immobilizer

Uguisu is the **BLE key fob** module for a two-module immobilizer system:

- **Uguisu (this repo)**: short, encrypted BLE advertisements on button press
- **Guillemot (separate repo)**: scooter deck receiver that validates adverts and gates battery-to-ESC power

## Contents

- KiCad project files:
  - `Uguisu.kicad_sch`
  - `Uguisu.kicad_pcb`

## Notes

- This repo intentionally avoids committing KiCad per-user state (`*.kicad_prl`) and backup/lock artifacts.
- Safety: do not test power-interrupt/lock behavior while riding.

