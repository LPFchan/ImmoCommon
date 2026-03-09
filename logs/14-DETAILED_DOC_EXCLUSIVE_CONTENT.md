# Content Exclusive to BLE_Immobilizer_G30_Detailed — Migration Extract

This document captures information from `BLE_Immobilizer_G30_Detailed(10).md` that is **not** present in the Immogen monorepo README, Guillemot README, or Uguisu README. Use this as a migration checklist when consolidating docs or as a reference for content not yet ported.

---

## 1. Design Notes

- **EasyEDA → KiCad:** Schematics and PCBs are designed in KiCad. [easyeda2kicad](https://github.com/wokwi/easyeda2kicad) is used to import symbols and footprints from EasyEDA/LCSC. Example: `easyeda2kicad --full --lcsc_id=Cxxxx` (replace `Cxxxx` with the LCSC part number).