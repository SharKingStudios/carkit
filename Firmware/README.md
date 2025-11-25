<p align="center">
  <img src="/Renders/banner.png" alt="Banner"/>
</p>

# CarKit Firmware  

### ESP32-C3 firmware for the CarKit RC control board  

---

## What is this?

This folder contains the **firmware** for the CarKit board – an all-in-one **ESP32-C3-WROOM-02-H4**–based controller for RC cars.

The firmware handles:

- **Bluetooth gamepad input** (via Bluepad32)
- **Drive + steering DC motors** (DRV8876 drivers)
- Optional **servo steering** and **BLDC/ESC** output
- **4× RGB LEDs** for status/mode/feedback
- An on-board **buzzer** for power-on tones, connect chirps, and a horn
- Optional **Wi-Fi debug web page** to configure and persist settings (NVS)

If the PCB is the body, this is the nervous system.

---

## Features Overview

- **Bluetooth Controller Support**
  - Uses **Bluepad32** to support Xbox-style and other BLE gamepads.
  - Reads triggers, sticks, and buttons for driving and mode control.

- **Two Drive Modes**
  - **Car Mode** (default)  
    - Throttle from **triggers** (forward/reverse).
    - Steering from **left stick X**.
  - **Tank Mode**  
    - Left/Right “tracks” from **left stick Y** and **right stick Y**.
    - Mapping of **which motor driver is left/right** is configurable.

- **Motor Outputs**
  - **U4 DRV8876** → Steering DC motor
  - **U5 DRV8876** → Drive DC motor
  - Each with:
    - **PWM speed control**
    - **Direction control**
    - Optional **invert flags** in firmware so you can fix direction without rewiring.

- **Servo + BLDC Support (Optional)**
  - **Servo output** on `GPIO2` (e.g. steering servo).
  - **BLDC/ESC signal** on `GPIO3` (for brushless motor via external ESC).
  - Both can be enabled/disabled via constants and web UI.

- **Addressable RGB LEDs (4× SD6812/SK6812)**
  - Provide:
    - Connection status
    - Drive mode indication
    - Live feedback for throttle and steering
    - Simple startup / pairing animations

- **Buzzer (FST-1230 on GPIO 1)**
  - Power-on jingle
  - “Controller connected” chirp
  - Horn while a button is held (default: **A** button)

- **Debug Web UI (Optional)**
  - ESP32-C3 hosts a small Wi-Fi AP and web page.
  - Lets you tune and save config (NVS) without recompiling.

---

## Pin Mapping (Firmware View)

| Function                      | GPIO | Notes                                          |
|------------------------------|------|------------------------------------------------|
| RGB LED Data (4× SD6812)     | 0    | LED chain input                                |
| Buzzer (FST-1230)            | 1    | PWM-driven, used for jingles + horn            |
| Servo Signal                 | 2    | Optional steering / general RC servo           |
| BLDC / ESC Signal            | 3    | Optional ESC throttle signal                   |
| Steering DC EN (U4 DRV8876)  | 4    | PWM (EN) for steering DC motor                 |
| Steering DC PH (U4 DRV8876)  | 5    | Direction for steering motor                   |
| Drive DC EN (U5 DRV8876)     | 6    | PWM (EN) for drive DC motor                    |
| Drive DC PH (U5 DRV8876)     | 7    | Direction for drive motor                      |
| USB D- / D+                  | 18/19| USB interface (depends on your board layout)   |
| Extra GPIOs                  | 10/20/21 | Broken out, unused by firmware internally |

If you make a board revision and it changes any of these, update the pin constants at the top of `CarKit_Firmware.ino`.

---

## LED Layout (Firmware Roles)

There are **4 addressable LEDs** chained on `GPIO0`. The firmware uses them as:

| Index | Role         | Description                                            |
|-------|--------------|--------------------------------------------------------|
| 0     | User LED 1   | Throttle visualization (forward/reverse intensity)    |
| 1     | User LED 2   | Steering visualization (left/right intensity)         |
| 2     | Mode LED     | Drive mode (Car vs Tank)                              |
| 3     | Status LED   | Bluetooth status (pairing vs connected)               |

Animations include:

- Boot “wipe” animation on startup.
- Pulsing status while waiting for a controller.
- Solid connection color when a controller is paired.

---

## Gamepad Mapping

### Car Mode (Default)

- **Throttle**
  - **Right trigger** → forward.
  - **Left trigger** → reverse.
  - Firmware combines them into a single value:  
    `drive = forward_trigger - reverse_trigger`.

- **Steering**
  - **Left stick X** (left/right).

- **Horn**
  - Default: **A button**.
  - Press & hold = horn tone (buzzer on GPIO 1).

- **Drive Mode Toggle**
  - Default: **Y button**.
  - Toggles between **Car Mode** and **Tank Mode**.
  - The last used mode is stored in NVS and restored on boot.

### Tank Mode

- **Left motor**  = **Left stick Y** (up/down).
- **Right motor** = **Right stick Y** (up/down).

Which DRV8876 (U4 vs U5) is considered “left” is configurable (no need to swap wires on the board).

---

## Configuration & Constants

Most behavior is controlled by constants at the top of `CarKit_Firmware.ino`:

### Motor Direction

```cpp
const bool DEFAULT_REVERSE_STEERING_DC = false;  // reverse U4 sense
const bool DEFAULT_REVERSE_DRIVE_DC    = false;  // reverse U5 sense
```
Flip these to `true` if a motor spins “backwards”.

### Feature Toggles
```cpp
const bool DEFAULT_ENABLE_BLDC_OUTPUT  = false;  // ESC on GPIO3
const bool DEFAULT_ENABLE_SERVO_OUTPUT = true;   // servo on GPIO2
const bool DEFAULT_TANK_LEFT_IS_U4     = true;   // true: U4 = left, false: U5 = left
```
Use these to enable/disable outputs and tank drive mapping.

### Servo Settings
```cpp
const int DEFAULT_SERVO_CENTER_US      = 1500;   // servo center pulse width (microseconds)
const int DEFAULT_SERVO_MIN_US         = 1000;   // min pulse (microseconds)
const int DEFAULT_SERVO_MAX_US         = 2000;   // max pulse (microseconds)
```
Adjust `DEFAULT_SERVO_CENTER_US` if your steering servo sits slightly off center.

### Input / Mode Defaults
```cpp
const HornButton DEFAULT_HORN_BUTTON   = HORN_BTN_A;       // A/B/X/Y
const DriveMode  DEFAULT_DRIVE_MODE    = DRIVE_MODE_CAR;   // CAR or TANK
```
These define the default horn button and drive mode on first boot.

---

## Persistent Settings (NVS)

The firmware uses the ESP32’s Preferences (NVS) to store configuration at runtime:
- Reverse flags (steering/drive)
- Servo enable / BLDC enable
- Tank mapping (which side is U4)
- Servo center trim (µs)
- Horn button choice
- Default drive mode

On first boot, firmware uses the compile-time defaults.
After you change settings via the web UI (or code), they are saved to NVS and override defaults on subsequent boots.

If your configuration gets messy, you can clear NVS (e.g. via another sketch or ESP32 erase tools) to go back to the original defaults.

---

## Debug Web UI

> Optional but very handy while tuning

If `ENABLE_DEBUG_WEBUI` is set to `1` in `CarKit_Firmware.ino`, the ESP32-C3:
 - Starts a Wi-Fi access point: 
   - SSID: CarKit-Debug
   - Password: carkit123 (change in code if desired)
 - Hosts a small config site at:
   - `http://192.168.4.1/`
 - Lets you edit and save:
   - Reverse steering / drive
   - Enable/disable servo output and BLDC output
   - Tank mapping (U4 vs U5 as left)
   - Servo center (microseconds)
   - Horn button (A/B/X/Y)
   - Default drive mode (Car / Tank)

When you click Save:
 - Values are written into NVS.
 - Some changes apply immediately; all are loaded on next boot.

To remove the web UI set:
```cpp
#define ENABLE_DEBUG_WEBUI 0
```
near the top of the file and recompile.

---

## Safety / Failsafes

 - The firmware uses a simple idle timeout:
   - If no controller updates arrive for a short period, throttle and steering commands are forced to zero to stop the motors.

 - On controller disconnect:
   - Internally commanded speeds are set to zero.
   - The status LED reverts to “searching/pairing” pattern.

You should still test carefully and use reasonable supply voltages.

---

## Build & Flash Instructions

### 1. Toolchain Setup

You’ll need:

- **Arduino IDE** (recommended) or **PlatformIO**
- The **Bluepad32 ESP32 core** installed (board package that provides `esp32_bluepad32` boards)

For Arduino IDE, follow the Bluepad32 docs to add their ESP32 board URL, then install the `esp32_bluepad32` package.

---

### 2. Board Selection (Arduino IDE)

In **Tools → Board**, select:

> `esp32_bluepad32 → LOLIN C3 Mini`

This matches the **ESP32-C3-WROOM-02-H4** module closely enough for flash size, CPU, and Bluetooth LE.

---

### 3. Required Libraries

Install these from **Tools → Manage Libraries…**:

- **Adafruit NeoPixel**

These are included with the ESP32 / Bluepad32 cores and don’t usually need separate installation:

- **Bluepad32**
- **Preferences**
- **WiFi**
- **WebServer**

If the IDE complains about missing headers, double-check that:
- The correct board core (`esp32_bluepad32`) is selected, and  
- The libraries above are installed/enabled.

---

### 4. Uploading the Firmware

1. Open `CarKit_Firmware.ino` in **Arduino IDE**.
2. Under **Tools**:
   - Set **Board** to `LOLIN C3 Mini (esp32_bluepad32)`.
   - Set the correct **Port** for your CarKit board (USB-C serial).
3. Click **Upload**.

If your board requires a bootloader sequence:

- Hold **BOOT** (if present), press and release **RESET**, then release **BOOT**.
- Try uploading again.

On a successful upload you should see:

- Buzzer plays the **power-on jingle**.
- The RGB LEDs do a **boot animation**.
- The status LED goes into a “waiting for controller” pattern until a gamepad connects.

---

## Resetting Configuration

If you’ve played with settings and want to go back to firmware defaults:

 - Erase the ESP32-C3’s NVS partition (e.g. using esptool.py or a dedicated “erase” sketch)

On next boot, the firmware will use the compile-time defaults again.
