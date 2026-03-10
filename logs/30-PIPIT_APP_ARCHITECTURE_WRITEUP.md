# Pipit: Companion App Architecture & Technical Writeup

*Date: 2026-03-10*

**Status:** Architectural Blueprint & Rationale
**Scope:** Architecture for the "Pipit" companion iOS/Android app, cross-platform core, UI framework, BLE strategy, and key provisioning.

---

## 1. Executive Summary

**Pipit** is the companion iOS and Android application for the Immogen vehicle immobilizer ecosystem. Named to follow the ornithological theme (Guillemot, Uguisu, Whimbrel), Pipit serves two primary functions:

1. **Proximity Unlock (Background):** A low-power background service that detects when the user approaches the vehicle and automatically unlocks Guillemot, with an adjustable proximity threshold.
2. **Active Key Fob (Foreground):** A user interface to manually lock and unlock the vehicle, functioning identically to the Uguisu hardware fob.

To balance cross-platform efficiency with platform-native performance, Pipit uses **Kotlin Multiplatform (KMP)** for shared business logic while maintaining strictly native UI frameworks (**SwiftUI/UIKit** for iOS, **Jetpack Compose** for Android).

---

## 2. Core Architecture Decisions & Rationale

### 2.1 Cross-Platform Core: Kotlin Multiplatform (KMP)
Rather than writing the business logic twice (Swift for iOS, Kotlin for Android) or wrapping the existing C++ firmware codebase (`immo_crypto.cpp`) in JNI/Objective-C++, we elected to use **Kotlin Multiplatform**.

**Rationale:**
* **Developer Experience:** Native Kotlin development provides zero friction on Android and compiles to a highly performant, Swift-friendly native framework for iOS.
* **Memory Safety:** KMP offers a modern, memory-safe environment compared to maintaining manual memory management via C++ wrappers.
* **Trade-off:** The primary trade-off is the need to port the existing C++ AES-CCM (RFC3610) cryptography and MAC logic into pure Kotlin. To mitigate risks, this KMP implementation will be exhaustively unit-tested against the existing C++ test vectors to guarantee bit-for-bit parity.

### 2.2 User Interface: Platform-Native (Compose & SwiftUI)
The application will not use cross-platform UI frameworks like Flutter or React Native.

**Rationale:**
* **Native Feel:** The app must feel like a first-class citizen on both platforms. 
* **Hardware Integration:** BLE interactions, camera usage (for QR scanning), and secure enclave access are deeply intertwined with the UI/UX. Relying on native toolkits—**Jetpack Compose** for Android and **SwiftUI/UIKit** for iOS—ensures smooth, reliable integrations with platform-specific APIs.

### 2.3 Provisioning Strategy: QR Code & Secure Enclave
To provision a phone as a trusted key, Pipit will use the device's camera to scan a QR code displayed by **Whimbrel** (during initial setup) or by **another Pipit instance** (for subsequent phones, post-installation).

The QR code encodes a provisioning URI containing the slot key and a 6-digit management PIN: `immogen://prov?slot=<n>&key=<hex>&ctr=0&pin=<6digits>`. The management PIN enables BLE-authenticated key slot management after installation (see Integration Requirements doc §1.3).

**Rationale:**
* **Security & Usability:** QR codes provide a high-bandwidth, air-gapped method of securely transferring root cryptographic keys without pairing over a vulnerable, unencrypted BLE channel.
* **Storage:** Once scanned, the slot key is stored strictly within the device's hardware-backed keystores: **Android Keystore** and **iOS Keychain**. The management PIN is stored alongside it. These secrets never touch plaintext storage, ensuring the app remains secure even if the host OS is compromised.
* **Pipit as Provisioning Client:** A provisioned Pipit instance can authenticate to Guillemot over BLE using the management PIN and perform key management post-installation — provisioning additional phones (displaying QR codes), revoking lost devices, and changing the management PIN. On **Android**, Pipit additionally supports USB operations via OTG: firmware flashing (UF2 mass storage via `libaums`) and Uguisu fob provisioning (CDC serial via `usb-serial-for-android`). On **iOS**, USB operations are not available (no USB device API); these require Whimbrel on a laptop. See the Integration Requirements doc §4 for the full platform capability matrix.

---

## 3. Background BLE Architecture (The iOS Challenge)

The most complex requirement is ensuring reliable background proximity unlock, particularly on iOS. 

### 3.1 The iOS Background Advertising Limitation
Initially, the intuitive approach is to have the phone act as a BLE Peripheral (Broadcaster) continuously announcing its presence, and Guillemot acting as the Central (Scanner) to detect the phone. 

However, **iOS severely restricts background BLE advertising.** When an iOS app goes into the background, Apple strips out the Manufacturer Data and hashes the custom Service UUID into an undocumented, proprietary "overflow area." Since Guillemot runs on an nRF52840 MCU and not Apple silicon, it cannot easily decode this proprietary hash to recognize the iPhone.

### 3.2 The Flipped Solution: Guillemot as Advertiser
To achieve 100% reliable background wake-ups on both iOS and Android, the architecture flips the traditional BLE roles:

1. **Guillemot acts as a Central AND a Peripheral:**
   * It continues its 5% duty-cycle scanning (25ms window / 500ms interval) to listen for Uguisu fobs.
   * *Simultaneously*, it broadcasts a slow, continuous advertisement beacon containing a specific "Immogen Proximity" Service UUID every **500ms**.
2. **Pipit acts as the Central (Scanner):**
   * The Pipit app registers a background scanner with the OS (via CoreBluetooth on iOS and `BluetoothLeScanner` with a Foreground Service on Android) specifically looking for the "Immogen Proximity" Service UUID.
   * When the user walks within range, the phone's OS hardware detects the 500ms beacon and wakes the Pipit app in the background. **Note:** iOS background scan delivery is not instantaneous—CoreBluetooth may batch scan results and deliver them with a delay ranging from sub-second to several minutes, depending on iOS version, device state, and app usage recency. `CBCentralManagerScanOptionAllowDuplicatesKey` is also ignored in background mode. The implementation must use `centralManager:willRestoreState:` for state restoration to maximize reliability.
   * Pipit connects to Guillemot, executes the challenge-response authentication, and unlocks the vehicle.

### 3.3 Power Consumption Impact on Guillemot
One concern with Guillemot advertising continuously is battery drain. However, the math proves this impact is negligible:
* **Current Scan Draw:** ~306 µA (5% duty cycle listening for Uguisu).
* **New Beacon Draw:** Advertising once every 500ms requires ~2ms of TX active time at ~5.3 mA, which averages to just **~21.2 µA**.
* **Total New Power Draw:** ~327 µA.
* **Battery Life Effect:** At 327 µA, a 15.3 Ah scooter battery will last roughly 5.3 years in standby mode, which is far beyond the natural self-discharge rate of lithium-ion cells (~12 months).

**Rationale:** Trading ~21 µA of power on Guillemot for 100% reliable iOS background wake-ups is an optimal architectural compromise.

### 3.4 End-to-End Unlock Latency (GATT Path)
Unlike Uguisu's connectionless broadcast (which Guillemot detects in a single scan window), the Pipit unlock path involves multiple sequential BLE operations after the background wake-up:

| Step | Typical Latency |
|---|---|
| iOS/Android background scan delivery | Sub-second to minutes (variable; see §3.2 note) |
| GATT connection establishment | ~50–200 ms |
| Service & characteristic discovery | ~100–300 ms |
| Encrypted payload write + ACK | ~10–50 ms |
| Guillemot `verify_payload()` + latch actuation | ~5–15 ms |
| **Total (post-wake)** | **~165–565 ms** |

In practice, the post-wake GATT path will feel near-instant once the phone has been woken. The dominant latency variable is the OS background scan delivery itself. Users should expect that proximity unlock via Pipit will not feel as instantaneous as a physical Uguisu fob press, particularly on iOS where background scan batching can introduce noticeable delay.

---

## 4. Implementation Blueprint

To realize this architecture, the implementation will be split into the following phases:

1. **KMP Project Scaffolding:** Create the Pipit monorepo structure containing `shared`, `androidApp`, and `iosApp`.
2. **Crypto & Protocol Porting:** Implement pure-Kotlin versions of `immo_crypto.cpp` (AES-128 and RFC3610 MIC generation) inside the KMP `shared` module, validating against established test vectors.
3. **Platform Interfaces (expect/actual):** Define KMP wrappers for secure storage (Keychain/Keystore) and BLE Central managers.
4. **Guillemot Firmware Update:** Modify `Guillemot/firmware/src/main.cpp` and `guillemot_nrf52840.h` to emit the 500ms advertisement beacon.
5. **Native UI & Hardware Integration:** Build the QR scanning provisioning flow and the foreground Key Fob interfaces using Jetpack Compose and SwiftUI.
6. **Background Services:** Implement Android Foreground Services and iOS CoreBluetooth background modes to handle the proximity wake-up and automated authentication sequence.
