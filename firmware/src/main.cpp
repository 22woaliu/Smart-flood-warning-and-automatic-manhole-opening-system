/**
 * ============================================================
 *  IoT Roadside Water Drainage & Purification System
 *  Firmware  –  ESP32 / PlatformIO (Arduino Framework)
 * ============================================================
 *
 *  Hardware connections
 *  --------------------
 *  Component              ESP32 GPIO
 *  ─────────────────────────────────
 *  Rain Sensor  (D0)      GPIO 34   (digital, active LOW when raining)
 *  Ultrasonic   TRIG      GPIO 5
 *  Ultrasonic   ECHO      GPIO 18
 *  Flow Sensor  Signal    GPIO 19   (interrupt, rising edge)
 *  Servo Motor  PWM       GPIO 13
 *  Built-in LED           GPIO 2
 *
 *  Operation Summary
 *  -----------------
 *  1. Rain sensor → detect rainfall (boolean)
 *  2. Ultrasonic  → measure water level (distance in cm, lower = higher water)
 *  3. If distance < DANGER_DISTANCE_CM → open purifier (servo → 180°)
 *  4. If distance > SAFE_DISTANCE_CM   → close purifier (servo → 0°)
 *  5. Flow sensor → pulses/sec converted to L/min
 *  6. High water + low flow            → blockage detected
 *  7. WebSocket JSON broadcast every 2 s to connected dashboard clients
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// ─────────────────────────────────────────────────────────────
//  Compile-time constants (can be overridden in platformio.ini)
// ─────────────────────────────────────────────────────────────
#ifndef WIFI_SSID
  #define WIFI_SSID "DrainageSystem"
#endif
#ifndef WIFI_PASS
  #define WIFI_PASS "drainage123"
#endif
#ifndef DANGER_DISTANCE_CM
  #define DANGER_DISTANCE_CM 10    // water level danger threshold (cm from sensor)
#endif
#ifndef SAFE_DISTANCE_CM
  #define SAFE_DISTANCE_CM   20    // water level safe threshold
#endif
#ifndef FLOW_BLOCKAGE_THRESHOLD
  #define FLOW_BLOCKAGE_THRESHOLD 0.5f   // L/min below which blockage is suspected
#endif

// ─────────────────────────────────────────────────────────────
//  Pin Definitions
// ─────────────────────────────────────────────────────────────
constexpr uint8_t PIN_RAIN_SENSOR  = 34;  // digital input
constexpr uint8_t PIN_ULTRA_TRIG   = 5;
constexpr uint8_t PIN_ULTRA_ECHO   = 18;
constexpr uint8_t PIN_FLOW_SENSOR  = 19;  // interrupt input
constexpr uint8_t PIN_SERVO        = 13;
constexpr uint8_t PIN_LED          = 2;

// ─────────────────────────────────────────────────────────────
//  System constants
// ─────────────────────────────────────────────────────────────
constexpr unsigned long BROADCAST_INTERVAL_MS  = 2000;   // WebSocket broadcast
constexpr unsigned long FLOW_CALC_INTERVAL_MS  = 1000;   // flow rate recalculation
constexpr float         FLOW_PULSE_FACTOR      = 7.5f;   // pulses/sec per L/min (YF-S201)
constexpr unsigned long ULTRA_TIMEOUT_US       = 30000;  // max echo wait

// ─────────────────────────────────────────────────────────────
//  Globals
// ─────────────────────────────────────────────────────────────
WebSocketsServer wsServer(81);
Servo            purifierServo;

// Sensor readings (updated in loop / ISR)
volatile uint32_t flowPulseCount = 0;   // incremented by ISR
float    flowRateLPM   = 0.0f;          // litres per minute
float    waterLevelCm  = 0.0f;          // distance from sensor → water surface
bool     isRaining     = false;
bool     purifierOpen  = false;
bool     blockageDetected = false;

// Timing
unsigned long lastBroadcast  = 0;
unsigned long lastFlowCalc   = 0;

// ─────────────────────────────────────────────────────────────
//  ISR – flow sensor pulse counter
// ─────────────────────────────────────────────────────────────
void IRAM_ATTR flowPulseISR() {
  flowPulseCount++;
}

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────

/** Read ultrasonic distance in centimetres (-1 on timeout). */
float readUltrasonicCm() {
  // Trigger pulse
  digitalWrite(PIN_ULTRA_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_ULTRA_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_ULTRA_TRIG, LOW);

  long duration = pulseIn(PIN_ULTRA_ECHO, HIGH, ULTRA_TIMEOUT_US);
  if (duration == 0) return -1.0f;  // timeout / out of range
  return duration * 0.034f / 2.0f;  // cm
}

/** Read rain sensor (LOW = rain detected on most modules). */
bool readRainSensor() {
  return (digitalRead(PIN_RAIN_SENSOR) == LOW);
}

/** Set purifier/servo state – only moves servo when state changes. */
void setPurifier(bool open) {
  if (open == purifierOpen) return;  // no change needed
  purifierOpen = open;
  purifierServo.write(open ? 180 : 0);
  digitalWrite(PIN_LED, open ? HIGH : LOW);
  Serial.printf("[SERVO] Purifier %s\n", open ? "OPENED (180deg)" : "CLOSED (0deg)");
}

// ─────────────────────────────────────────────────────────────
//  WebSocket event handler
// ─────────────────────────────────────────────────────────────
void onWebSocketEvent(uint8_t clientId, WStype_t type,
                      uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("[WS] Client #%u connected\n", clientId);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("[WS] Client #%u disconnected\n", clientId);
      break;
    case WStype_TEXT:
      // Accept commands from the dashboard (future extension)
      Serial.printf("[WS] Message from #%u: %s\n", clientId, payload);
      break;
    default:
      break;
  }
}

// ─────────────────────────────────────────────────────────────
//  Build and broadcast JSON payload
// ─────────────────────────────────────────────────────────────
void broadcastSensorData() {
  StaticJsonDocument<256> doc;

  doc["rain"]          = isRaining;
  doc["waterLevelCm"]  = waterLevelCm;
  doc["dangerCm"]      = DANGER_DISTANCE_CM;
  doc["safeCm"]        = SAFE_DISTANCE_CM;
  doc["purifierOpen"]  = purifierOpen;
  doc["flowLPM"]       = flowRateLPM;
  doc["blockage"]      = blockageDetected;
  doc["timestamp"]     = millis();

  // Water level status string
  if (waterLevelCm < 0) {
    doc["levelStatus"] = "error";
  } else if (waterLevelCm <= DANGER_DISTANCE_CM) {
    doc["levelStatus"] = "danger";
  } else if (waterLevelCm <= SAFE_DISTANCE_CM) {
    doc["levelStatus"] = "warning";
  } else {
    doc["levelStatus"] = "safe";
  }

  // Flow status string
  doc["flowStatus"] = blockageDetected ? "blocked" : "normal";

  char buf[256];
  size_t n = serializeJson(doc, buf);
  wsServer.broadcastTXT(buf, n);

  // Also echo to Serial for debugging
  Serial.printf("[DATA] rain=%d level=%.1fcm flow=%.2fL/min purifier=%s blockage=%d\n",
    isRaining, waterLevelCm, flowRateLPM,
    purifierOpen ? "OPEN" : "CLOSED", blockageDetected);
}

// ─────────────────────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== IoT Drainage System Booting ===");

  // Pin modes
  pinMode(PIN_RAIN_SENSOR, INPUT);
  pinMode(PIN_ULTRA_TRIG, OUTPUT);
  pinMode(PIN_ULTRA_ECHO, INPUT);
  pinMode(PIN_FLOW_SENSOR, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);

  // Servo initialisation
  purifierServo.attach(PIN_SERVO, 500, 2400);  // 500–2400 µs pulse range
  purifierServo.write(0);                       // start closed
  Serial.println("[SERVO] Initialised at 0deg (closed)");

  // Flow sensor interrupt (rising edge = one pulse)
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW_SENSOR), flowPulseISR, RISING);

  // ── WiFi Access Point ──────────────────────────────────────
  // The ESP32 creates its own WiFi AP; no router required.
  // Connect your PC/phone to SSID "DrainageSystem" (pw: drainage123)
  // then open http://192.168.4.1 in the browser.
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[WiFi] AP started  SSID: %s  IP: %s\n", WIFI_SSID, ip.toString().c_str());

  // ── WebSocket Server ───────────────────────────────────────
  wsServer.begin();
  wsServer.onEvent(onWebSocketEvent);
  Serial.printf("[WS] Server listening on port 81\n");

  Serial.println("=== Boot complete. Monitoring… ===\n");
}

// ─────────────────────────────────────────────────────────────
//  Main loop
// ─────────────────────────────────────────────────────────────
void loop() {
  wsServer.loop();  // handle WebSocket events

  unsigned long now = millis();

  // ── 1. Calculate flow rate every second ───────────────────
  if (now - lastFlowCalc >= FLOW_CALC_INTERVAL_MS) {
    noInterrupts();
    uint32_t pulses = flowPulseCount;
    flowPulseCount = 0;
    interrupts();

    float elapsed_s = (now - lastFlowCalc) / 1000.0f;
    float pulsesPerSec = pulses / elapsed_s;
    flowRateLPM = pulsesPerSec / FLOW_PULSE_FACTOR;

    lastFlowCalc = now;
  }

  // ── 2. Read all sensors ────────────────────────────────────
  isRaining     = readRainSensor();
  float rawDist = readUltrasonicCm();

  // Simple outlier filter: ignore values > 400 cm (beyond sensor range)
  if (rawDist > 0 && rawDist < 400) {
    waterLevelCm = rawDist;
  }

  // ── 3. Purifier control logic ──────────────────────────────
  // Lower distance = higher water level
  if (waterLevelCm > 0 && waterLevelCm <= DANGER_DISTANCE_CM) {
    setPurifier(true);   // Open – danger level
  } else if (waterLevelCm > SAFE_DISTANCE_CM) {
    setPurifier(false);  // Close – water has subsided
  }
  // Between thresholds: maintain current state (hysteresis)

  // ── 4. Blockage detection ──────────────────────────────────
  // High water (small distance) + near-zero flow = suspected blockage
  bool highWater = (waterLevelCm > 0 && waterLevelCm <= SAFE_DISTANCE_CM);
  blockageDetected = highWater && (flowRateLPM < FLOW_BLOCKAGE_THRESHOLD);

  // ── 5. Broadcast sensor data via WebSocket ─────────────────
  if (now - lastBroadcast >= BROADCAST_INTERVAL_MS) {
    broadcastSensorData();
    lastBroadcast = now;
  }
}
