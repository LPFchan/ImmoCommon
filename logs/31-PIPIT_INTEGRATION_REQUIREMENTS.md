# Pipit App Integration: Architecture & Technical Requirements

*Date: 2026-03-10*

**Status:** Technical Writeup
**Scope:** Outline of required changes and key architectural decisions for Immogen (Guillemot) and Whimbrel to support the new Pipit companion app.

---

## 1. Key Architectural Decisions

### 1.1 Multi-Device Counter & Key Management (CRITICAL)
Currently, Guillemot only stores a single 16-byte Pre-Shared Key (`g_psk`) and tracks a single monotonically increasing counter to prevent replay attacks.
* **The Problem:** If the Uguisu physical fob and the Pipit app share the *same* key and counter, they will desync. Unlocking via Pipit would advance Guillemot's counter, leaving Uguisu's internal counter lagging behind. Subsequent fob presses would be rejected until Uguisu "catches up."
* **The Decision:** Guillemot must be updated to support a fixed array of **4 Key Slots** (Slot 0–3), each with independent PSK and counter storage. Slot 0 is reserved for the Uguisu hardware fob; Slots 1–3 are available for Pipit companion app instances. When all slots are occupied, a new device cannot be provisioned until an existing slot is revoked (see §1.3).

### 1.2 GATT Protocol Design vs. Connectionless Broadcasting
The Uguisu fob operates connectionlessly: it broadcasts its unlock payload (as a Peripheral) and Guillemot scans for it (as a Central). For Pipit, however, a GATT connection is strictly required due to mobile OS constraints and the direction of data flow.

* **The Directional Constraint:** Guillemot controls the physical vehicle latch, meaning the unlock command *must* ultimately flow from Pipit to Guillemot.
* **Why Connectionless Fails:** To replicate Uguisu's flow, Pipit would need to broadcast the payload in the background. However, iOS severely restricts background BLE advertising—CoreBluetooth automatically strips out custom Manufacturer Specific Data and hashes Service UUIDs into an Apple-proprietary format. If Pipit broadcasted in the background, Guillemot would scan empty packets and never receive the unlock command.
* **The "Flipped" Discovery:** To guarantee reliable background wake-ups, the roles must be flipped for discovery: Guillemot acts as the Advertiser (emitting a 500ms beacon) and Pipit acts as the Scanner.
* **The GATT Requirement:** Once Guillemot's beacon wakes Pipit up, Pipit still holds the unlock payload. Because the BLE specification does not allow a Scanner to send custom data connectionlessly (e.g., inside a Scan Request), Pipit is forced to establish a GATT connection with the Advertiser to write the data.
* **The Decision:** Guillemot must host a GATT Server. The simplest migration path is to expose a single `Command` characteristic. Pipit will connect and write an encrypted AES-CCM payload (identical in format to Uguisu's MSD), which Guillemot will then route through its existing `verify_payload()` logic.

### 1.3 Management PIN & BLE Key Management
Once Guillemot is installed inside the vehicle, requiring USB-C serial access for routine key management (revoking a lost phone, adding a replacement) would mean a full vehicle teardown. To avoid this, key slot management is exposed over BLE, authenticated by a **6-digit management PIN**.

#### 1.3.1 Provisioning Lifecycle (Whimbrel Onboarding Flow)
The Whimbrel dashboard guides the user through the following steps in order:

1. **Flash Guillemot firmware** via USB-C DFU. *(Skippable — a "Skip" button is provided for devices with firmware already flashed.)*
2. **Provision Uguisu** (Slot 0 fob key). Whimbrel generates a key, flashes it to the Uguisu fob via USB-C serial, and prompts the user to disconnect Uguisu.
3. **Provision Guillemot** (Slot 0). Whimbrel flashes the same key to Guillemot via USB-C serial (`PROV:0:<key>:<counter>`). No management PIN is set at this stage — fob-only users manage slots exclusively via wired connection.
4. **"Add a phone key?"** prompt. If the user declines, onboarding is complete.
5. **"Enter a management PIN"** prompt. The user enters a new 6-digit PIN. Whimbrel sends the PIN hash to Guillemot via serial (`SETPIN:<hash>`) and provisions a new key into Slot 1 (`PROV:1:<key>:<counter>`).
6. **QR code display.** Whimbrel encodes the slot key and management PIN into a QR code (`immogen://prov?slot=1&key=<hex>&ctr=0&pin=<6digits>`) and displays it with obscure/reveal controls for the phone to scan.

**Post-installation provisioning:** Subsequent phones (Slots 2–3) and replacement Uguisu fobs can be provisioned without USB-C access to Guillemot. The user authenticates to Guillemot over BLE using the management PIN (via Whimbrel over Web Bluetooth, or via Pipit) and provisions new slot keys. For phone slots, a QR code is displayed for the target phone to scan. For Uguisu fob replacement, Pipit on Android connects to the fob directly via USB-C OTG to flash the new key. iOS does not support USB serial communication, so fob provisioning from an iPhone requires Whimbrel on a laptop (see §4 Platform Capability Matrix).

#### 1.3.2 BLE-Authenticated Key Management
Once a management PIN has been set, the following operations are available over BLE from either **Pipit** (native) or **Whimbrel** (Web Bluetooth), authenticated by the PIN:
* **Query slot status:** Read which of the 4 slots are occupied/empty, with last-seen counter values.
* **Revoke a slot:** Zero a specific slot's PSK and reset its counter, freeing it for re-provisioning without affecting other slots.
* **Provision a new phone slot:** Generate a fresh key, write it to an empty slot on Guillemot, and display a QR code for the target phone to scan. Pipit can serve as the provisioning client here — it displays the QR code for the new phone to scan directly.
* **Provision/replace Uguisu fob (Slot 0):** Generate a fresh key, write it to Slot 0 on Guillemot over BLE, then flash the same key to the Uguisu fob via USB-C. **Android only** — Pipit connects to the fob using USB Host/OTG (`usb-serial-for-android`). Not available on iOS (no USB serial API); use Whimbrel on a laptop instead.
* **Change management PIN:** Requires the current PIN to authorize. Updates the stored PIN hash on Guillemot.

#### 1.3.3 Brute-Force Rate Limiting
A 6-digit PIN has 1,000,000 combinations. To prevent BLE brute-force attacks, Guillemot enforces:
* **Exponential backoff:** After 3 consecutive failed PIN attempts, Guillemot doubles the lockout period starting at 5 seconds (5s → 10s → 20s → 40s → ...).
* **Hard lockout:** After 10 consecutive failed attempts, BLE management is disabled entirely. Recovery requires USB-C serial access via Whimbrel to reset the lockout counter.
* **Counter reset:** A successful PIN entry resets the failure counter to zero.

#### 1.3.4 Recovery Fallback
USB-C serial via Whimbrel remains the break-glass recovery path for:
* All slot keys revoked or lost (no valid PSK to connect, no PIN to authenticate).
* Management PIN forgotten (hard lockout or simply unknown).
* Hard lockout triggered by failed attempts.

In these scenarios, the user must physically access the Guillemot USB-C port, which requires vehicle disassembly — acceptable for a recovery-only path.

---

## 2. Changes Required: Immogen (Guillemot Firmware)

To facilitate the background proximity wake-up and connection-based authentication, the following firmware modifications are required:

1. **Enable Dual BLE Roles:**
   * Modify `Bluefruit.begin(0, 1)` to `Bluefruit.begin(1, 1)` in `main.cpp` to operate simultaneously as a Central (scanning for Uguisu) and a Peripheral (advertising for Pipit).
2. **Broadcast Proximity Beacon:**
   * Implement continuous BLE advertising with a 500ms interval (`Bluefruit.Advertising.setInterval(800, 800);`).
   * Include the new custom 128-bit "Immogen Proximity" Service UUID in the advertisement payload to wake up iOS devices in the background.
3. **Implement GATT Server:**
   * Add the "Immogen Proximity" `BLEService` with separate characteristics for:
     * **Unlock Command:** Receives encrypted AES-CCM unlock payloads (routed through `verify_payload()`).
     * **Management Auth:** Accepts a PIN challenge to establish an authenticated management session.
     * **Management Command:** Gated behind successful PIN auth; handles slot queries, revocation, provisioning, and PIN changes.
   * Attach write callbacks to handle Pipit's connection and incoming payloads.
4. **Update Storage & Provisioning:**
   * Refactor `immo::CounterStore` and `immo_provisioning` to manage multiple Key Slots without overwriting existing provisioned devices.
5. **Management PIN Storage & Rate Limiting:**
   * Store the management PIN as a hash in non-volatile flash (alongside the key slot array). The PIN is only set when the first phone is provisioned — until then, BLE management characteristics are not exposed.
   * Implement a brute-force rate limiter: exponential backoff after 3 failed attempts (5s doubling), hard lockout after 10 failures requiring USB-C serial reset.

---

## 3. Changes Required: Whimbrel (Provisioning Dashboard)

Whimbrel operates in two modes: **USB-C serial** (initial setup and recovery) and **Web Bluetooth** (post-installation key management). The dashboard must support both transports for the commands below.

### 3.1 Initial Setup (USB-C Serial Only)
The Whimbrel onboarding wizard follows the step order defined in §1.3.1:

1. **Flash Guillemot Firmware** via USB-C DFU. A "Skip" button allows users with pre-flashed devices to advance.
2. **Provision Uguisu Fob (Slot 0 key):** Generate key, flash to Uguisu fob via USB-C serial, prompt user to disconnect fob.
3. **Provision Guillemot (Slot 0 key):** Flash the same key to Guillemot via serial (`PROV:0:<key>:<counter>`). No management PIN exists yet.
4. **"Add a phone key?" prompt.** If declined, onboarding is complete.
5. **"Enter a management PIN" prompt.** User enters a 6-digit PIN. Whimbrel sends the PIN hash and a new Slot 1 key to Guillemot via serial (`SETPIN:<hash>`, `PROV:1:<key>:<counter>`).
6. **QR Code Display.** Whimbrel encodes the slot key and management PIN into a QR code (`immogen://prov?slot=1&key=<hex>&ctr=0&pin=<6digits>`) with obscure/reveal controls to prevent shoulder-surfing.

### 3.2 Post-Installation Key Management (Web Bluetooth or USB-C Serial)
Once Guillemot is installed and a management PIN has been set, the following operations are available from Whimbrel over **Web Bluetooth** (no vehicle disassembly required) or from **Pipit** over native BLE:

1. **Slot Status Query:** Read the status of all 4 key slots (occupied/empty, slot index, last-seen counter value) to display a live overview of provisioned devices.
2. **Slot Revocation:** Zero a specific slot's PSK and reset its counter, freeing it for re-provisioning. The UI must present per-slot "Revoke" controls with a confirmation step.
3. **Provision Additional Phones (Slots 2–3):** Generate a fresh key, write it to an empty slot on Guillemot, and display a QR code for the new phone to scan. Both Whimbrel and Pipit can serve as the provisioning client (displaying the QR code).
4. **Provision/Replace Uguisu Fob (Slot 0):** Generate a fresh key, write it to Slot 0 on Guillemot over BLE, then flash the same key to the Uguisu fob via USB-C. **Android Pipit only** (USB Host/OTG); on iOS, use Whimbrel on a laptop with Web Serial.
5. **Change Management PIN:** Requires the current PIN. Updates the stored hash on Guillemot.

All BLE management commands require successful PIN authentication first (see §1.3.2).

### 3.3 Serial Protocol Expansion
The following serial commands must be implemented on Guillemot to support both the initial USB-C setup and the USB-C recovery fallback:
* `PROV:<slot>:<key>:<counter>` — Provision a key into the specified slot.
* `SETPIN:<hash>` — Set or update the management PIN hash.
* `SLOTS?` — Query the status of all 4 key slots.
* `REVOKE:<slot>` — Zero a slot's PSK and reset its counter.
* `RESETLOCK` — Clear the brute-force lockout counter (recovery only).

### 3.4 QR Code & Provisioning URI Schema
* Integrate a QR code library (e.g., `qrcode.js`) for both Whimbrel and ensure Pipit can also generate and display QR codes.
* Standardized URI format: `immogen://prov?slot=<n>&key=<hex>&ctr=0&pin=<6digits>`, parseable by the KMP shared module.

---

## 4. Platform Capability Matrix

The capabilities of each client differ due to iOS's lack of USB device APIs. BLE DFU has been evaluated and rejected — all firmware flashing uses USB-C (UF2 mass storage for Guillemot, CDC serial for Uguisu).

| Operation | Pipit (Android) | Pipit (iOS) | Whimbrel (laptop) |
|---|---|---|---|
| **Guillemot firmware flash (USB DFU)** | USB OTG — copy `.uf2` to bootloader mass storage via `libaums` | **Not supported** — no USB device API on iOS | USB-C DFU (existing flow) |
| **Uguisu firmware/key flash (USB serial)** | USB OTG — CDC serial via `usb-serial-for-android` | **Not supported** — no USB serial API on iOS | Web Serial (existing flow) |
| **Guillemot key management (BLE)** | BLE GATT + management PIN | BLE GATT + management PIN | Web Bluetooth + management PIN |
| **Phone provisioning (QR)** | BLE + QR code display | BLE + QR code display | Web Bluetooth + QR code display |
| **Proximity unlock** | BLE background scan (Foreground Service) | BLE background scan (CoreBluetooth) | N/A |
| **Active key fob** | BLE GATT write | BLE GATT write | N/A |

### 4.1 Android USB Implementation Details

**Guillemot DFU (UF2 mass storage):**
* The Xiao nRF52840's Adafruit bootloader presents as a USB mass storage device when entering DFU mode (double-tap reset).
* Pipit uses `libaums` (pure Java, no root) to mount the FAT filesystem and copy the `.uf2` file.
* The app must handle USB re-enumeration: the device detaches as a CDC serial device and reattaches as mass storage when entering bootloader mode. Use `ACTION_USB_DEVICE_ATTACHED` / `ACTION_USB_DEVICE_DETACHED` broadcast receivers.

**Uguisu provisioning (USB CDC serial):**
* Pipit uses `usb-serial-for-android` to open a CDC/ACM serial connection to the Uguisu fob (or Guillemot in normal mode) and send provisioning commands.
* Auto-detection of CDC/ACM devices by interface type is supported since library v3.5.0, avoiding the need for VID/PID probing.

**Common USB gotchas:**
* Android requires explicit user permission per USB device via `UsbManager.requestPermission()`. The permission dialog appears on each new connection. Using an intent filter with VID/PID matching can auto-grant, but behavior varies across OEMs.
* The phone must supply 5V via USB-C OTG. Most modern Android devices (2020+) support this, but low-end devices may not provide sufficient current.

### 4.2 iOS Limitations (Confirmed)

iOS provides no public API for generic USB device communication. The following paths have been investigated and ruled out:
* **`ExternalAccessory` framework:** Requires MFi certification and an Apple authentication coprocessor in the hardware. Not viable for a bare Xiao nRF52840.
* **DriverKit (USBDriverKit):** Available on iPadOS only (M1+ iPads). Not available on iPhone.
* **WebUSB:** Not supported in Safari on any Apple platform.
* **USB CDC serial:** No API exists on iOS.
* **UF2 via Files app:** The Xiao's UF2 bootloader virtual FAT filesystem is untested on iOS and unreliable — iOS may write metadata files (`.Trashes`, `.fseventsd`) that confuse the bootloader.

**Consequence:** iOS Pipit users who need to flash firmware or provision an Uguisu fob must use Whimbrel on a laptop with USB-C access. All BLE-based operations (key management, phone provisioning, proximity unlock) work identically on both platforms.
