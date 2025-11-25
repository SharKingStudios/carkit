/*
 * CarKit ESP32-C3-MINI firmware
 * -----------------------------------------
 * - Bluetooth gamepad (Xbox-compatible) using Bluepad32
 * - DRV8876 (U4 = steering DC, U5 = drive DC)
 * - Optional steering servo + BLDC ESC output
 * - SD6812 / SK6812 / WS2812-compatible LEDs on GPIO0
 * - Piezo buzzer (FST-1230) tones & jingles
 * - Multiple drive modes (car & tank)
 * - Optional debug web UI to edit & persist configuration
 *
 * Pin mapping:
 *   GPIO0  -> SD6812 LED data in  (4 LEDs)
 *   GPIO21 -> broken out, unused
 *   GPIO20 -> broken out, unused
 *   GPIO10 -> broken out, unused
 *   GPIO19 -> USB
 *   GPIO18 -> USB
 *   GPIO2  -> Servo PWM breakout (steering servo)   [optional]
 *   GPIO3  -> BLDC ESC / motor controller PWM       [optional]
 *   GPIO4  -> U4 DRV8876 EN / M1_IN1 (PWM / enable)
 *   GPIO5  -> U4 DRV8876 PH / M1_IN2 (direction)
 *   GPIO6  -> U5 DRV8876 EN / M2_IN1 (PWM / enable)
 *   GPIO7  -> U5 DRV8876 PH / M2_IN2 (direction)
 *
 *   Buzzer FST-1230 -> see BUZZER_PIN below
 *
 * LED roles (index in chain, LED 0 = first after MCU):
 *   LED 0: User indicator #1 (drive/throttle)
 *   LED 1: User indicator #2 (steering)
 *   LED 2: MODE LED (drive mode)
 *   LED 3: STATUS LED (BT connection / pairing)
 */

// ==============================
//  USER-CONFIGURABLE CONSTANTS
// ==============================

#include <stdint.h>

// ---- Pin configuration ----
const int LED_PIN          = 0;   // SD6812 / SK6812 / "NeoPixel" chain data
const int BUZZER_PIN       = 1;

const int SERVO_PIN        = 2;   // Steering servo PWM breakout
const int BLDC_ESC_PIN     = 3;   // BLDC ESC PWM breakout

const int U4_EN_PIN        = 4;   // DRV8876 U4 EN / M1_IN1 (PWM / enable)
const int U4_PH_PIN        = 5;   // DRV8876 U4 PH / M1_IN2 (direction)
const int U5_EN_PIN        = 6;   // DRV8876 U5 EN / M2_IN1 (PWM / enable)
const int U5_PH_PIN        = 7;   // DRV8876 U5 PH / M2_IN2 (direction)

// ---- LEDC PWM channels ----
// (Channels are NOT pins – each channel can be attached to one pin.)
const int BUZZER_PWM_CHANNEL = 0;
const int SERVO_PWM_CHANNEL  = 1;
const int BLDC_PWM_CHANNEL   = 2;
const int U4_PWM_CHANNEL     = 3;
const int U5_PWM_CHANNEL     = 4;

// ---- Feature toggles (defaults) ----
const bool DEFAULT_REVERSE_STEERING_DC  = false;  // reverse U4 direction
const bool DEFAULT_REVERSE_DRIVE_DC     = false;  // reverse U5 direction

const bool DEFAULT_ENABLE_BLDC_OUTPUT   = false;  // ESC on GPIO3
const bool DEFAULT_ENABLE_SERVO_OUTPUT  = true;   // steering servo on GPIO2

// Tank mode: which DRV8876 is LEFT side?
// true  -> U4 = left track, U5 = right track
// false -> U5 = left track, U4 = right track
const bool DEFAULT_TANK_LEFT_IS_U4      = true;

// ---- OLD-SCHOOL ENUMS (Arduino-friendly) ----
enum HornButton {
  HORN_BTN_A = 0,
  HORN_BTN_B,
  HORN_BTN_X,
  HORN_BTN_Y,
};

const HornButton DEFAULT_HORN_BUTTON = HORN_BTN_A;

enum DriveMode {
  DRIVE_MODE_CAR  = 0,  // triggers = F/R, left stick X = steering
  DRIVE_MODE_TANK = 1,  // left & right sticks Y = tracks
};

const DriveMode DEFAULT_DRIVE_MODE = DRIVE_MODE_CAR;

// Button used to toggle drive mode at runtime (Y by default)
const HornButton DRIVE_MODE_TOGGLE_BUTTON = HORN_BTN_Y;

// ---- Servo parameters ----
// These are in microseconds and assume standard hobby servo.
const int DEFAULT_SERVO_CENTER_US  = 1500; // "0 position" trim
const int DEFAULT_SERVO_MIN_US     = 1000; // mechanical limit (left)
const int DEFAULT_SERVO_MAX_US     = 2000; // mechanical limit (right);

// ---- LED behaviour ----
const uint8_t LED_GLOBAL_BRIGHTNESS = 64;  // 0..255

// ---- Input shaping ----
const int   GAMEPAD_AXIS_MAX      = 512;    // Bluepad32 approx -512..+512
const int   GAMEPAD_TRIGGER_MAX   = 1023;   // Bluepad32 approx 0..1023
const int   AXIS_DEADZONE         = 40;     // stick deadzone
const unsigned long DRIVE_IDLE_TIMEOUT_MS = 500; // stop motors if no input

// ---- Motor PWM ----
const int MOTOR_PWM_FREQ      = 20000; // 20 kHz, above audible
const int MOTOR_PWM_RES_BITS  = 10;    // 0..1023
const int MOTOR_PWM_MAX       = (1 << MOTOR_PWM_RES_BITS) - 1;

// ---- Buzzer / jingle settings ----
const int   BUZZER_BASE_FREQ_HZ   = 4000;  // FST-1230 nominal freq
const int   BUZZER_VOLUME_DUTY    = MOTOR_PWM_MAX / 2; // ~50% duty

// ---- Servo / ESC PWM ----
const int   SERVO_PWM_FREQ        = 50;    // 50 Hz for servo/ESC
const int   SERVO_PWM_RES_BITS    = 12;    // 4096 steps in 20ms (~4.9us)
const int   SERVO_PWM_MAX         = (1 << SERVO_PWM_RES_BITS) - 1;

// ---- Debug web UI (compile-time switch) ----
#define ENABLE_DEBUG_WEBUI 1

#if ENABLE_DEBUG_WEBUI
// SoftAP credentials
const char* DEBUG_AP_SSID     = "CarKit-Debug";
const char* DEBUG_AP_PASSWORD = "carkit123";  // change if you want
#endif

// NVS namespace
const char* PREFS_NAMESPACE = "carkit";

// ==============================
//   INCLUDES
// ==============================

#include <Arduino.h>
#include <Bluepad32.h>        // Gamepad support
#include <Preferences.h>      // Persistent config (NVS)
#include <Adafruit_NeoPixel.h>

#if ENABLE_DEBUG_WEBUI
#include <WiFi.h>
#include <WebServer.h>
#endif

// ==============================
//   LED STRIP SETUP
// ==============================

const uint8_t NUM_LEDS = 4;
Adafruit_NeoPixel pixels(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// LED indices
const uint8_t LED_USER1 = 0;
const uint8_t LED_USER2 = 1;
const uint8_t LED_MODE  = 2;
const uint8_t LED_STAT  = 3;

// ==============================
//   CONFIG STRUCT
// ==============================

struct Config {
  bool       reverseSteeringDC;
  bool       reverseDriveDC;
  bool       enableBLDC;
  bool       enableServo;
  bool       tankLeftIsU4;
  int        servoCenterUs;
  HornButton hornButton;
  DriveMode  defaultDriveMode;
};

Config cfg;
Preferences prefs;

// ==============================
//   BLUEPAD32 / GAMEPAD
// ==============================

ControllerPtr controllers[BP32_MAX_GAMEPADS];
ControllerPtr primaryGamepad = nullptr;

bool         controllerConnected   = false;
DriveMode    currentDriveMode      = DEFAULT_DRIVE_MODE;
unsigned long lastInputMs          = 0;
bool         lastHornPressed       = false;
bool         lastModeTogglePressed = false;

// Normalized commands from -1.0 .. +1.0
float cmdDrive    = 0.0f;  // forward/reverse (car mode)
float cmdSteer    = 0.0f;  // steering [-1..1] (car mode)
float cmdTankL    = 0.0f;  // tank left
float cmdTankR    = 0.0f;  // tank right

// ==============================
//   PWM / MOTOR HELPERS
// ==============================

int dutyFromNormalized(float x) {
  x = constrain(x, -1.0f, 1.0f);
  float mag = fabs(x);
  int duty = (int)(mag * MOTOR_PWM_MAX);
  if (duty < 0) duty = 0;
  if (duty > MOTOR_PWM_MAX) duty = MOTOR_PWM_MAX;
  return duty;
}

// Drive a DRV8876 in PH/EN mode (sign-magnitude)
void setDrv8876(float normalized, int pwmChannel, int phPin, bool reverse) {
  normalized = constrain(normalized, -1.0f, 1.0f);

  if (fabs(normalized) < 0.02f) {
    // Brake low: EN=0
    ledcWrite(pwmChannel, 0);
    digitalWrite(phPin, LOW);
    return;
  }

  bool dirForward = (normalized > 0.0f);
  if (reverse)
    dirForward = !dirForward;

  int duty = dutyFromNormalized(normalized);
  digitalWrite(phPin, dirForward ? HIGH : LOW);
  ledcWrite(pwmChannel, duty);
}

// Servo pulse (50Hz via LEDC)
int servoUsToDuty(int us) {
  const float periodUs = 1000000.0f / SERVO_PWM_FREQ; // ~20000us
  float dutyCycle = (float)us / periodUs;             // 0..1
  int duty = (int)(dutyCycle * SERVO_PWM_MAX);
  if (duty < 0) duty = 0;
  if (duty > SERVO_PWM_MAX) duty = SERVO_PWM_MAX;
  return duty;
}

void setSteeringServo(float normalized) {
  if (!cfg.enableServo)
    return;

  normalized = constrain(normalized, -1.0f, 1.0f);

  int rangeLeft  = cfg.servoCenterUs - DEFAULT_SERVO_MIN_US;
  int rangeRight = DEFAULT_SERVO_MAX_US - cfg.servoCenterUs;

  int us;
  if (normalized < 0.0f) {
    us = cfg.servoCenterUs + (int)(normalized * rangeLeft);
  } else {
    us = cfg.servoCenterUs + (int)(normalized * rangeRight);
  }

  us = constrain(us, DEFAULT_SERVO_MIN_US, DEFAULT_SERVO_MAX_US);
  int duty = servoUsToDuty(us);
  ledcWrite(SERVO_PWM_CHANNEL, duty);
}

// BLDC ESC (servo-like signal for throttle)
void setBLDC(float normalized) {
  if (!cfg.enableBLDC)
    return;

  normalized = constrain(normalized, -1.0f, 1.0f);

  int us = 1500 + (int)(normalized * 500); // -1..1 => 1000..2000
  us = constrain(us, 1000, 2000);

  int duty = servoUsToDuty(us);
  ledcWrite(BLDC_PWM_CHANNEL, duty);
}

// ==============================
//   BUZZER / JINGLES
// ==============================

bool buzzerActive = false;

void buzzerInit() {
  ledcSetup(BUZZER_PWM_CHANNEL, BUZZER_BASE_FREQ_HZ, MOTOR_PWM_RES_BITS);
  ledcAttachPin(BUZZER_PIN, BUZZER_PWM_CHANNEL);
  ledcWrite(BUZZER_PWM_CHANNEL, 0);
}

void buzzerTone(int freqHz, int durationMs, int volumeDuty = BUZZER_VOLUME_DUTY) {
  if (freqHz <= 0 || durationMs <= 0) return;
  ledcSetup(BUZZER_PWM_CHANNEL, freqHz, MOTOR_PWM_RES_BITS);
  ledcWrite(BUZZER_PWM_CHANNEL, volumeDuty);
  buzzerActive = true;
  delay(durationMs);
  ledcWrite(BUZZER_PWM_CHANNEL, 0);
  buzzerActive = false;
}

void jinglePowerOn() {
  // Simple ascending three-note chime
  buzzerTone(2000, 80);
  delay(60);
  buzzerTone(2600, 80);
  delay(60);
  buzzerTone(3200, 120);
}

void jingleConnected() {
  // Short "connected" chirp
  buzzerTone(2800, 70);
  delay(40);
  buzzerTone(4000, 90);
}

// Horn while button held
void updateHorn(bool pressed) {
  if (pressed && !lastHornPressed) {
    // Start horn tone (continuous)
    ledcSetup(BUZZER_PWM_CHANNEL, BUZZER_BASE_FREQ_HZ, MOTOR_PWM_RES_BITS);
    ledcWrite(BUZZER_PWM_CHANNEL, BUZZER_VOLUME_DUTY);
    buzzerActive = true;
  } else if (!pressed && lastHornPressed) {
    // Stop horn
    ledcWrite(BUZZER_PWM_CHANNEL, 0);
    buzzerActive = false;
  }
  lastHornPressed = pressed;
}

// ==============================
//   LED ANIMATIONS
// ==============================

uint32_t color(uint8_t r, uint8_t g, uint8_t b) {
  return pixels.Color(r, g, b);
}

void ledsInit() {
  pixels.begin();
  pixels.setBrightness(LED_GLOBAL_BRIGHTNESS);
  pixels.clear();
  pixels.show();
}

// Simple power-on "wipe"
void ledsBootAnimation() {
  pixels.clear();
  for (int i = 0; i < NUM_LEDS; ++i) {
    pixels.setPixelColor(i, color(0, 0, 20 + i * 10));
    pixels.show();
    delay(80);
  }
  delay(150);
  pixels.clear();
  pixels.show();
}

void ledsSetStatusPairing() {
  // breathing blue on STATUS LED
  static uint16_t phase = 0;
  phase = (phase + 3) & 0x1FF;  // 0..511
  uint8_t b = (phase < 256) ? phase : (511 - phase);
  pixels.setPixelColor(LED_STAT, color(0, 0, b));
}

void ledsSetStatusConnected() {
  // solid green
  pixels.setPixelColor(LED_STAT, color(0, 40, 0));
}

void ledsSetMode() {
  if (currentDriveMode == DRIVE_MODE_CAR) {
    // cyan
    pixels.setPixelColor(LED_MODE, color(0, 40, 40));
  } else {
    // amber
    pixels.setPixelColor(LED_MODE, color(40, 20, 0));
  }
}

// User LEDs: visualize drive & steering
void ledsSetUserIndicators() {
  // LED_USER1 = throttle magnitude (green forward, red reverse)
  float d = constrain(cmdDrive, -1.0f, 1.0f);
  uint8_t mag = (uint8_t)(fabs(d) * 80);
  uint8_t r = (d < 0) ? mag : 0;
  uint8_t g = (d > 0) ? mag : 0;
  pixels.setPixelColor(LED_USER1, color(r, g, 0));

  // LED_USER2 = steering (blue left, magenta right)
  float s = constrain(cmdSteer, -1.0f, 1.0f);
  uint8_t m = (uint8_t)(fabs(s) * 80);
  uint8_t b = (s < 0) ? m : 0;
  uint8_t mr = (s > 0) ? m : 0;
  pixels.setPixelColor(LED_USER2, color(mr, 0, b));
}

void ledsUpdate() {
  if (controllerConnected)
    ledsSetStatusConnected();
  else
    ledsSetStatusPairing();

  ledsSetMode();
  ledsSetUserIndicators();
  pixels.show();
}

// ==============================
//   CONFIG LOAD / SAVE
// ==============================

void loadConfig() {
  prefs.begin(PREFS_NAMESPACE, true);  // read-only

  cfg.reverseSteeringDC = prefs.getBool("revSteer", DEFAULT_REVERSE_STEERING_DC);
  cfg.reverseDriveDC    = prefs.getBool("revDrive",  DEFAULT_REVERSE_DRIVE_DC);
  cfg.enableBLDC        = prefs.getBool("enBLDC",    DEFAULT_ENABLE_BLDC_OUTPUT);
  cfg.enableServo       = prefs.getBool("enServo",   DEFAULT_ENABLE_SERVO_OUTPUT);
  cfg.tankLeftIsU4      = prefs.getBool("tankL4",    DEFAULT_TANK_LEFT_IS_U4);
  cfg.servoCenterUs     = prefs.getInt ("servoCtr",  DEFAULT_SERVO_CENTER_US);
  cfg.hornButton        = (HornButton)prefs.getUChar("hornBtn", (uint8_t)DEFAULT_HORN_BUTTON);
  cfg.defaultDriveMode  = (DriveMode)prefs.getUChar("drvMode", (uint8_t)DEFAULT_DRIVE_MODE);

  prefs.end();

  currentDriveMode = cfg.defaultDriveMode;
}

void saveConfig() {
  prefs.begin(PREFS_NAMESPACE, false);

  prefs.putBool ("revSteer", cfg.reverseSteeringDC);
  prefs.putBool ("revDrive", cfg.reverseDriveDC);
  prefs.putBool ("enBLDC",   cfg.enableBLDC);
  prefs.putBool ("enServo",  cfg.enableServo);
  prefs.putBool ("tankL4",   cfg.tankLeftIsU4);
  prefs.putInt  ("servoCtr", cfg.servoCenterUs);
  prefs.putUChar("hornBtn",  (uint8_t)cfg.hornButton);
  prefs.putUChar("drvMode",  (uint8_t)currentDriveMode);

  prefs.end();
}

// ==============================
//   DEBUG WEB UI (optional)
// ==============================

#if ENABLE_DEBUG_WEBUI
WebServer server(80);

String htmlBool(bool v) { return v ? "checked" : ""; }

String htmlSelectHorn(HornButton b) {
  String s;
  s += "<select name='hornBtn'>";
  s += "<option value='0'"; if (b == HORN_BTN_A) s += " selected"; s += ">A</option>";
  s += "<option value='1'"; if (b == HORN_BTN_B) s += " selected"; s += ">B</option>";
  s += "<option value='2'"; if (b == HORN_BTN_X) s += " selected"; s += ">X</option>";
  s += "<option value='3'"; if (b == HORN_BTN_Y) s += " selected"; s += ">Y</option>";
  s += "</select>";
  return s;
}

String htmlSelectDriveMode(DriveMode m) {
  String s;
  s += "<select name='drvMode'>";
  s += "<option value='0'"; if (m == DRIVE_MODE_CAR)  s += " selected"; s += ">Car (triggers + steer)</option>";
  s += "<option value='1'"; if (m == DRIVE_MODE_TANK) s += " selected"; s += ">Tank (L/R sticks)</option>";
  s += "</select>";
  return s;
}

void handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8"/>
<title>CarKit Debug Config</title>
<style>
 body { font-family: sans-serif; max-width: 600px; margin: 2em auto; }
 h1 { font-size: 1.4em; }
 label { display:block; margin: 0.3em 0; }
 fieldset { margin-bottom: 1em; }
</style>
</head>
<body>
<h1>CarKit ESP32-C3 Config</h1>
<form method="POST" action="/save">
<fieldset><legend>Motors</legend>
<label><input type="checkbox" name="revSteer" VALUE="1" %REVSTEER%> Reverse steering DC (U4)</label>
<label><input type="checkbox" name="revDrive" VALUE="1" %REVDIRVE%> Reverse drive DC (U5)</label>
<label><input type="checkbox" name="tankL4"  VALUE="1" %TANKL4%> Tank: U4 is LEFT (unchecked = U5 left)</label>
</fieldset>

<fieldset><legend>Outputs</legend>
<label><input type="checkbox" name="enServo" VALUE="1" %ENSERVO%> Enable steering servo (GPIO2)</label>
<label><input type="checkbox" name="enBLDC"  VALUE="1" %ENBLDC%> Enable BLDC / ESC output (GPIO3)</label>
<label>Servo center (µs): <input type="number" name="servoCtr" value="%SERVOCTR%" min="1000" max="2000"/></label>
</fieldset>

<fieldset><legend>Controls</legend>
<label>Horn button: %HORNBUTTON%</label>
<label>Default drive mode: %DRVMODE%</label>
</fieldset>

<button type="submit">Save</button>
</form>

<p>Current status: %STATUS%</p>
</body>
</html>
)";

  html.replace("%REVSTEER%", cfg.reverseSteeringDC ? "checked" : "");
  html.replace("%REVDIRVE%", cfg.reverseDriveDC    ? "checked" : "");
  html.replace("%TANKL4%",   cfg.tankLeftIsU4      ? "checked" : "");
  html.replace("%ENSERVO%",  cfg.enableServo       ? "checked" : "");
  html.replace("%ENBLDC%",   cfg.enableBLDC        ? "checked" : "");
  html.replace("%SERVOCTR%", String(cfg.servoCenterUs));

  html.replace("%HORNBUTTON%", htmlSelectHorn(cfg.hornButton));
  html.replace("%DRVMODE%",    htmlSelectDriveMode(currentDriveMode));
  html.replace("%STATUS%",     controllerConnected ? "Controller connected" : "Waiting for controller...");

  server.send(200, "text/html", html);
}

void handleSave() {
  cfg.reverseSteeringDC = server.hasArg("revSteer");
  cfg.reverseDriveDC    = server.hasArg("revDrive");
  cfg.tankLeftIsU4      = server.hasArg("tankL4");
  cfg.enableServo       = server.hasArg("enServo");
  cfg.enableBLDC        = server.hasArg("enBLDC");

  if (server.hasArg("servoCtr")) {
    cfg.servoCenterUs = server.arg("servoCtr").toInt();
    cfg.servoCenterUs = constrain(cfg.servoCenterUs, 1000, 2000);
  }

  if (server.hasArg("hornBtn")) {
    cfg.hornButton = (HornButton)server.arg("hornBtn").toInt();
  }
  if (server.hasArg("drvMode")) {
    currentDriveMode = (DriveMode)server.arg("drvMode").toInt();
  }

  saveConfig();
  server.sendHeader("Location", "/");
  server.send(303);
}

void startDebugWeb() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(DEBUG_AP_SSID, DEBUG_AP_PASSWORD);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("Debug AP up at http://");
  Serial.println(ip);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
}
#endif  // ENABLE_DEBUG_WEBUI

// ==============================
//   BLUEPAD32 CALLBACKS & LOGIC
// ==============================

void onConnectedController(ControllerPtr ctl) {
  Serial.println("Controller connected");
  for (int i = 0; i < BP32_MAX_GAMEPADS; ++i) {
    if (controllers[i] == nullptr) {
      controllers[i] = ctl;
      break;
    }
  }
  primaryGamepad      = ctl;
  controllerConnected = true;
  lastInputMs         = millis();
  jingleConnected();
}

void onDisconnectedController(ControllerPtr ctl) {
  Serial.println("Controller disconnected");
  for (int i = 0; i < BP32_MAX_GAMEPADS; ++i) {
    if (controllers[i] == ctl) {
      controllers[i] = nullptr;
    }
  }
  primaryGamepad      = nullptr;
  controllerConnected = false;
  cmdDrive = cmdSteer = cmdTankL = cmdTankR = 0.0f;
}

bool isButtonPressed(ControllerPtr ctl, HornButton b) {
  switch (b) {
    case HORN_BTN_A: return ctl->a();
    case HORN_BTN_B: return ctl->b();
    case HORN_BTN_X: return ctl->x();
    case HORN_BTN_Y: return ctl->y();
    default:         return false;
  }
}

float normalizeAxis(int v, int deadzone) {
  if (abs(v) < deadzone) return 0.0f;
  float n = (float)v / (float)GAMEPAD_AXIS_MAX;  // approx -1..1
  n = constrain(n, -1.0f, 1.0f);
  return n;
}

float normalizeTrigger(int v) {
  if (v <= 0) return 0.0f;
  if (v >= GAMEPAD_TRIGGER_MAX) return 1.0f;
  return (float)v / (float)GAMEPAD_TRIGGER_MAX;
}

void processGamepad(ControllerPtr ctl) {
  if (!ctl || !ctl->isConnected()) return;

  lastInputMs = millis();

  // Read inputs
  int lx = ctl->axisX();         // left stick X
  int ly = ctl->axisY();         // left stick Y
  int ry = ctl->axisRY();        // right stick Y
  int tR = ctl->throttle();      // Right trigger (forward)
  int tL = ctl->brake();         // Left trigger (reverse)

  float fwd = normalizeTrigger(tR);
  float rev = normalizeTrigger(tL);

  // Forward/reverse from triggers: forward positive, reverse negative
  cmdDrive = fwd - rev;  // -1..+1-ish

  // Left stick X for steering
  cmdSteer = normalizeAxis(lx, AXIS_DEADZONE);

  // Tank mode: left & right sticks Y
  cmdTankL = -normalizeAxis(ly, AXIS_DEADZONE);  // invert so up = forward
  cmdTankR = -normalizeAxis(ry, AXIS_DEADZONE);

  // Horn & drive mode toggle
  bool hornNow = isButtonPressed(ctl, cfg.hornButton);
  updateHorn(hornNow);

  bool toggleNow = isButtonPressed(ctl, DRIVE_MODE_TOGGLE_BUTTON);
  if (toggleNow && !lastModeTogglePressed) {
    // rising edge: toggle drive mode
    currentDriveMode = (currentDriveMode == DRIVE_MODE_CAR)
                       ? DRIVE_MODE_TANK
                       : DRIVE_MODE_CAR;
    saveConfig(); // persist last used mode as default
  }
  lastModeTogglePressed = toggleNow;
}

void applyOutputs() {
  unsigned long now = millis();
  if (now - lastInputMs > DRIVE_IDLE_TIMEOUT_MS) {
    // Safety timeout: stop everything
    cmdDrive = cmdSteer = cmdTankL = cmdTankR = 0.0f;
  }

  if (currentDriveMode == DRIVE_MODE_CAR) {
    // Use drive & steer
    setDrv8876(cmdDrive, U5_PWM_CHANNEL, U5_PH_PIN, cfg.reverseDriveDC);     // drive DC motor (U5)
    setDrv8876(cmdSteer, U4_PWM_CHANNEL, U4_PH_PIN, cfg.reverseSteeringDC);  // steering DC motor (U4)

    setSteeringServo(cmdSteer);
    setBLDC(cmdDrive);
  } else { // TANK
    float left  = cmdTankL;
    float right = cmdTankR;

    if (cfg.tankLeftIsU4) {
      setDrv8876(left,  U4_PWM_CHANNEL, U4_PH_PIN, cfg.reverseSteeringDC);
      setDrv8876(right, U5_PWM_CHANNEL, U5_PH_PIN, cfg.reverseDriveDC);
    } else {
      setDrv8876(left,  U5_PWM_CHANNEL, U5_PH_PIN, cfg.reverseDriveDC);
      setDrv8876(right, U4_PWM_CHANNEL, U4_PH_PIN, cfg.reverseSteeringDC);
    }

    // In tank mode we keep servo centered and BLDC neutral:
    setSteeringServo(0.0f);
    setBLDC(0.0f);
  }
}

void processControllers() {
  for (int i = 0; i < BP32_MAX_GAMEPADS; ++i) {
    if (controllers[i] && controllers[i]->isConnected()) {
      processGamepad(controllers[i]);
      break; // use first connected as "primary"
    }
  }
}

// ==============================
//   SETUP & LOOP
// ==============================

void setupPWM() {
  // U4/U5 motor channels
  ledcSetup(U4_PWM_CHANNEL, MOTOR_PWM_FREQ, MOTOR_PWM_RES_BITS);
  ledcAttachPin(U4_EN_PIN, U4_PWM_CHANNEL);
  ledcWrite(U4_PWM_CHANNEL, 0);

  ledcSetup(U5_PWM_CHANNEL, MOTOR_PWM_FREQ, MOTOR_PWM_RES_BITS);
  ledcAttachPin(U5_EN_PIN, U5_PWM_CHANNEL);
  ledcWrite(U5_PWM_CHANNEL, 0);

  pinMode(U4_PH_PIN, OUTPUT);
  pinMode(U5_PH_PIN, OUTPUT);
  digitalWrite(U4_PH_PIN, LOW);
  digitalWrite(U5_PH_PIN, LOW);

  // Servo + BLDC channels
  ledcSetup(SERVO_PWM_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RES_BITS);
  ledcAttachPin(SERVO_PIN, SERVO_PWM_CHANNEL);
  ledcWrite(SERVO_PWM_CHANNEL, servoUsToDuty(DEFAULT_SERVO_CENTER_US));

  ledcSetup(BLDC_PWM_CHANNEL, SERVO_PWM_FREQ, SERVO_PWM_RES_BITS);
  ledcAttachPin(BLDC_ESC_PIN, BLDC_PWM_CHANNEL);
  ledcWrite(BLDC_PWM_CHANNEL, servoUsToDuty(1500)); // neutral
}

void setup() {
  Serial.begin(115200);
  delay(200);

  loadConfig();

  ledsInit();
  buzzerInit();
  jinglePowerOn();
  ledsBootAnimation();

  setupPWM();

#if ENABLE_DEBUG_WEBUI
  startDebugWeb();
#endif

  // Bluepad32 init
  BP32.setup(&onConnectedController, &onDisconnectedController);
  // Optional: force re-pair by forgetting keys
  // BP32.forgetBluetoothKeys();
  BP32.enableNewBluetoothConnections(true);

  Serial.println("CarKit ready. Pair your controller (Xbox over BLE).");
}

void loop() {
  bool dataUpdated = BP32.update();
  if (dataUpdated) {
    processControllers();
  }

  applyOutputs();
  ledsUpdate();

#if ENABLE_DEBUG_WEBUI
  server.handleClient();
#endif

  delay(1); // yield
}
