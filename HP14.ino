/*
  HP14 v1.2.1 QUOTA-SAFE - Deep Work Environment Monitor
  Board: ESP32-C6 DevKitC-1

  FILE STRUCTURE
  -----------------------------------------------------------------------------
  HP14.ino          : program logic
  config.h          : GPIO, timings, thresholds, feature switches
  secrets.h         : real Wi-Fi / ThingsBoard credentials
  secrets.example.h : safe template only

  FINAL PIN MAP — DO NOT CHANGE BETWEEN RELEASES
  -----------------------------------------------------------------------------
  This release permanently locks the GPIO map in config.h.
  Do not move wires unless config.h is deliberately changed.

  Modern two-button UX:
  GPIO1  -> PAGE: next screen.
  GPIO11 -> FOCUS: start/stop a local Deep Work session.
  Hold both buttons 5 seconds -> open HP14-SETUP portal.
Wi-Fi provisioning uses cached scanning and pending credentials: HTTP handlers never scan; new Wi-Fi is committed only after successful connection. The device retains the 5 most recently successful Wi-Fi networks and roams through them automatically. ThingsBoard telemetry is compact, rate-limited and reconnect-safe.
  A short chord window prevents the combined hold from accidentally changing page/session.

  Current TFT wiring:
  RST=GPIO0, CLK=GPIO18, MOSI=GPIO19, CS=GPIO20, DC=GPIO21,
  VCC=3V3, BLK=3V3, GND=GND.
*/

#include <Arduino.h>
#include "config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SPI.h>
#include <Preferences.h>
#include <math.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GC9A01A.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_Sensor.h>
#include <DHT_U.h>
#include <DHT.h>

// =============================================================================
// 1) Objects
// =============================================================================

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
SPIClass tftSpi(FSPI);
Adafruit_GC9A01A tft(&tftSpi, PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_RST);
GFXcanvas16 tftCanvas(200, 146);
Adafruit_NeoPixel boardRgb(1, PIN_RGB, NEO_GRB + NEO_KHZ800);
DHT dht(PIN_DHT11, DHT11);

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Preferences preferences;
WebServer portalServer(WIFI_SETUP_WEB_PORT);
DNSServer dnsServer;

// =============================================================================
// 2) Data model
// =============================================================================

enum EnvironmentState : uint8_t {
  STATE_BOOT,
  STATE_WARMUP,
  STATE_GOOD,
  STATE_CAUTION,
  STATE_POOR,
  STATE_DEGRADED,
  STATE_SENSOR_ERROR
};

struct Measurements {
  float temperatureC = NAN;
  float humidityPct = NAN;
  float heatIndexC = NAN;
  bool dhtOk = false;
  bool dhtFresh = false;
  uint8_t dhtFailureCount = 0;
  uint32_t lastDhtGoodMs = 0;

  uint16_t mqRaw = 0;
  float mqAdcMv = 0.0f;
  float mqSensorMv = 0.0f;
  float mqFilteredMv = NAN;
  float mqBaselineMv = 0.0f;
  bool mqElectricalOk = false;
  bool mqWarm = false;
  bool mqCalibrating = false;
  bool mqReady = false;
  float gasRatio = NAN;
  float gasChangePct = NAN;

  int thermalScore = 0;
  int experimentalScore = 0;
  bool scoreValid = false;
  EnvironmentState state = STATE_BOOT;
  uint32_t sampleCounter = 0;
};

Measurements data;


// Runtime network settings. Preferences override secrets.h; secrets.h remains
// the first-boot fallback only.
String activeWifiSsid;
String activeWifiPassword;
String activeTbHost;
String activeTbToken;
uint16_t activeTbPort = TB_PORT;
bool forceWifiPortalOnBoot = false;
bool portalActive = false;
bool portalRoutesReady = false;
String portalSsid;
String portalReason;

struct PortalNetworkEntry {
  String ssid;
  int rssi = -127;
  bool open = false;
};
static constexpr uint8_t PORTAL_NETWORK_CACHE_MAX = 20;
PortalNetworkEntry portalNetworks[PORTAL_NETWORK_CACHE_MAX];
uint8_t portalNetworkCount = 0;
uint32_t portalScanCompletedMs = 0;

String savedWifiSsid;
String savedWifiPassword;
String pendingWifiSsid;
String pendingWifiPassword;
bool pendingWifiValid = false;
bool tryingPendingWifi = false;

// Five-network MRU vault. Index 0 is the most recently successful network.
struct SavedWifiEntry {
  String ssid;
  String password;
};
SavedWifiEntry wifiVault[WIFI_SAVED_NETWORK_LIMIT];
uint8_t wifiVaultCount = 0;
uint8_t wifiVaultNextIndex = 0;
int8_t wifiActiveVaultIndex = -1;
String lastRememberedConnectedSsid;
uint8_t wifiVaultCandidateOrder[WIFI_SAVED_NETWORK_LIMIT] = {};
uint8_t wifiVaultCandidateCount = 0;
uint8_t wifiVaultCandidateCursor = 0;
bool wifiVaultCandidateOrderReady = false;
uint8_t wifiSavedScanRound = 0;

void setWifiVaultCandidateOrderMru();
void prepareWifiVaultCandidateOrder();

// Wi-Fi provisioning UI state. v1.0 follows HP13's proven save-and-restart model.
// The phase values are retained for display/telemetry compatibility only.
enum WifiProvisionPhase : uint8_t {
  WIFI_PROVISION_IDLE,
  WIFI_PROVISION_READY,
  WIFI_PROVISION_TESTING,
  WIFI_PROVISION_SUCCESS,
  WIFI_PROVISION_FAILED,
  WIFI_PROVISION_CANCELING
};

WifiProvisionPhase wifiProvisionPhase = WIFI_PROVISION_IDLE;
String wifiProvisionMessage;
String candidateWifiSsid;
String candidateWifiPassword;
String candidateTbToken;
uint32_t wifiProvisionStartedMs = 0;
uint32_t wifiCandidateConnectedSinceMs = 0;
uint32_t wifiProvisionFinishedMs = 0;
uint32_t portalCloseAtMs = 0;
uint32_t wifiRestartAtMs = 0;

// Wi-Fi connection attempt state outside the portal.
bool wifiAttemptActive = false;
bool wifiEverConnected = false;
uint8_t wifiBootAttemptCount = 0;
uint32_t wifiAttemptStartedMs = 0;
uint32_t wifiBootWindowStartedMs = 0;
uint32_t wifiCycleRetryNotBeforeMs = 0;

// Deep Work local focus-session state — the ACTION button now has a useful,
// non-duplicate role instead of merely moving backward through pages.
bool focusSessionActive = false;
uint32_t focusSessionStartedMs = 0;
uint32_t focusSessionCount = 0;

// Short modern UI toast.
String uiToast;
uint32_t uiToastUntilMs = 0;

// Combined-button Wi-Fi-reset interaction.
bool dualHoldActive = false;
bool dualHoldTriggered = false;
uint32_t dualHoldStartedMs = 0;
uint8_t dualHoldLastSecond = 255;
uint8_t wifiHoldProgressPct = 0;

// MQ learning state.
float mqBaselineAccumulator = 0.0f;
uint16_t mqBaselineSamples = 0;
uint8_t mqMismatchCounter = 0;
uint32_t lastBaselineSaveMs = 0;

// UI state.
constexpr uint8_t PAGE_COUNT = 4;
uint8_t currentPage = 0;
bool alertAcknowledged = false;
bool alertsMuted = true;
bool uiDirty = true;
bool oledOk = false;
bool tftInitialized = false;
uint8_t oledAddress = 0;
uint8_t lastTftPage = 255;
uint32_t lastUiServiceMs = 0;
uint32_t lastOledRefreshMs = 0;
uint32_t lastTftRefreshMs = 0;
uint32_t feedbackUntilMs = 0;
uint8_t feedbackR = 0;
uint8_t feedbackG = 0;
uint8_t feedbackB = 0;

// Rolling history used by the TFT air-trend page.
static constexpr uint8_t GAS_HISTORY_COUNT = 48;
float gasRatioHistory[GAS_HISTORY_COUNT] = {};
uint8_t gasHistoryUsed = 0;
uint8_t gasHistoryHead = 0;

// Scheduler / network.
uint32_t lastSensorMs = 0;
uint32_t lastTelemetryMs = 0;          // last successful ThingsBoard publish
uint32_t lastTelemetryAttemptMs = 0;   // hard anti-hammer guard
uint32_t telemetryNextAttemptNotBeforeMs = 0;
uint32_t telemetryWindowStartedMs = 0;
uint8_t telemetryMessagesThisWindow = 0;
bool telemetryQuotaLogPrinted = false;
uint32_t mqttConnectedSinceMs = 0;
uint32_t lastWifiAttemptMs = 0;
uint32_t lastMqttAttemptMs = 0;
uint32_t mqttRetryMs = MQTT_RETRY_MIN_MS;
uint8_t mqttPublishFailures = 0;
wl_status_t previousWifiStatus = WL_NO_SHIELD;
bool previousMqttConnected = false;
uint32_t lastButtonDiagnosticMs = 0;
uint32_t lastAlertReminderMs = 0;
uint32_t lastDataLogMs = 0;
uint32_t lastUserActionMs = 0;

// RGB cache.
uint8_t lastRgbR = 255;
uint8_t lastRgbG = 255;
uint8_t lastRgbB = 255;

// =============================================================================
// 3) Button engine — stable edges + short chord window
// =============================================================================

class StableButton {
 public:
  StableButton(uint8_t pin, bool activeLow) : pin_(pin), activeLow_(activeLow) {}

  void begin() {
    pinMode(pin_, activeLow_ ? INPUT_PULLUP : INPUT_PULLDOWN);
    rawLevel_ = digitalRead(pin_);
    stableLevel_ = rawLevel_;
    lastRawChangeMs_ = millis();
  }

  void update(uint32_t now) {
    const int level = digitalRead(pin_);
    if (level != rawLevel_) {
      rawLevel_ = level;
      lastRawChangeMs_ = now;
    }
    const uint32_t debounce = levelMeansPressed(rawLevel_)
                                  ? BUTTON_DEBOUNCE_MS
                                  : BUTTON_RELEASE_DEBOUNCE_MS;
    if ((now - lastRawChangeMs_) < debounce || stableLevel_ == rawLevel_) return;

    stableLevel_ = rawLevel_;
    if (levelMeansPressed(stableLevel_)) {
      pressedEdge_ = true;
      pressedSinceMs_ = now;
    } else {
      releasedEdge_ = true;
      lastPressDurationMs_ = pressedSinceMs_ ? (now - pressedSinceMs_) : 0;
      pressedSinceMs_ = 0;
    }
  }

  bool consumePressedEdge() { bool e = pressedEdge_; pressedEdge_ = false; return e; }
  bool consumeReleasedEdge() { bool e = releasedEdge_; releasedEdge_ = false; return e; }
  bool isPressed() const { return levelMeansPressed(stableLevel_); }
  uint32_t pressedFor(uint32_t now) const { return isPressed() && pressedSinceMs_ ? now - pressedSinceMs_ : 0; }
  uint32_t lastPressDuration() const { return lastPressDurationMs_; }
  int rawLevel() const { return digitalRead(pin_); }

 private:
  bool levelMeansPressed(int level) const { return activeLow_ ? level == LOW : level == HIGH; }
  uint8_t pin_;
  bool activeLow_;
  int rawLevel_ = HIGH;
  int stableLevel_ = HIGH;
  bool pressedEdge_ = false;
  bool releasedEdge_ = false;
  uint32_t lastRawChangeMs_ = 0;
  uint32_t pressedSinceMs_ = 0;
  uint32_t lastPressDurationMs_ = 0;
};

StableButton menuButton(PIN_BTN_MENU, BUTTON_ACTIVE_LOW);
StableButton ackButton(PIN_BTN_ACK, BUTTON_ACTIVE_LOW);

bool pageActionPending = false;
bool focusActionPending = false;
uint32_t pagePendingSinceMs = 0;
uint32_t focusPendingSinceMs = 0;

// =============================================================================
// 4) Buzzer — short one-shot feedback only, with hard fail-safe
// =============================================================================

enum BeepPattern : uint8_t {
  BEEP_NONE,
  BEEP_PAGE,
  BEEP_FOCUS_START,
  BEEP_FOCUS_STOP,
  BEEP_WIFI_READY,
  BEEP_TEST
};

struct BuzzerRuntime {
  BeepPattern pattern = BEEP_NONE;
  uint8_t step = 0;
  uint32_t stepStartedMs = 0;
  uint32_t outputStartedMs = 0;
  bool outputOn = false;
  bool pwmAttached = false;
};

BuzzerRuntime buzzer;

void buzzerWrite(bool on) {
  if (!FEATURE_BUZZER) return;
  if (BUZZER_IS_ACTIVE) {
    const bool high = BUZZER_ACTIVE_HIGH ? on : !on;
    digitalWrite(PIN_BUZZER, high ? HIGH : LOW);
  } else {
    if (!buzzer.pwmAttached) return;
    ledcWriteTone(PIN_BUZZER, on ? PASSIVE_BUZZER_FREQUENCY_HZ : 0);
  }
  buzzer.outputOn = on;
  if (on) buzzer.outputStartedMs = millis();
}

void stopBuzzer() {
  buzzerWrite(false);
  buzzer.pattern = BEEP_NONE;
  buzzer.step = 0;
}

void startBeep(BeepPattern pattern) {
  if (!FEATURE_BUZZER || pattern == BEEP_NONE) return;
  stopBuzzer();
  buzzer.pattern = pattern;
  buzzer.step = 0;
  buzzer.stepStartedMs = millis();
  buzzerWrite(true);
}

uint16_t beepStepDuration(BeepPattern pattern, uint8_t step) {
  // Even steps ON, odd steps OFF. Xung rất ngắn để tránh tiếng inh ỏi.
  switch (pattern) {
    case BEEP_PAGE: {
      static const uint16_t p[] = {28};
      return step < 1 ? p[step] : 0;
    }
    case BEEP_FOCUS_START: {
      static const uint16_t p[] = {35, 52, 38};
      return step < 3 ? p[step] : 0;
    }
    case BEEP_FOCUS_STOP: {
      static const uint16_t p[] = {72};
      return step < 1 ? p[step] : 0;
    }
    case BEEP_WIFI_READY: {
      static const uint16_t p[] = {35, 45, 35, 45, 45};
      return step < 5 ? p[step] : 0;
    }
    case BEEP_TEST: {
      static const uint16_t p[] = {BUZZER_TEST_DURATION_MS};
      return step < 1 ? p[step] : 0;
    }
    default: return 0;
  }
}

void serviceBuzzer(uint32_t now) {
  // Absolute safety: no ON pulse can survive longer than the hard limit.
  if (buzzer.outputOn && (now - buzzer.outputStartedMs) >= BUZZER_HARD_MAX_ON_MS) {
    stopBuzzer();
    Serial.println(F("[BUZZER] Hard timeout forced OFF."));
    return;
  }
  if (!FEATURE_BUZZER || buzzer.pattern == BEEP_NONE) {
    if (buzzer.outputOn) buzzerWrite(false);
    return;
  }

  const uint16_t duration = beepStepDuration(buzzer.pattern, buzzer.step);
  if (duration == 0 || (now - buzzer.stepStartedMs) < duration) return;

  ++buzzer.step;
  const uint16_t nextDuration = beepStepDuration(buzzer.pattern, buzzer.step);
  if (nextDuration == 0) {
    stopBuzzer();
    return;
  }
  buzzer.stepStartedMs = now;
  buzzerWrite((buzzer.step % 2U) == 0U);
}

// =============================================================================
// 5) Helpers
// =============================================================================

const char *stateCode(EnvironmentState state) {
  switch (state) {
    case STATE_BOOT:         return "BOOT";
    case STATE_WARMUP:       return "WARMUP";
    case STATE_GOOD:         return "GOOD";
    case STATE_CAUTION:      return "CAUTION";
    case STATE_POOR:         return "POOR";
    case STATE_DEGRADED:     return "DEGRADED";
    case STATE_SENSOR_ERROR: return "SENSOR_ERROR";
    default:                 return "UNKNOWN";
  }
}

const char *stateLabel(EnvironmentState state) {
  switch (state) {
    case STATE_BOOT:         return "STARTING";
    case STATE_WARMUP:       return "SENSOR WARMUP";
    case STATE_GOOD:         return "DEEP WORK READY";
    case STATE_CAUTION:      return "ADJUST THE ROOM";
    case STATE_POOR:         return "TAKE ACTION NOW";
    case STATE_DEGRADED:     return "LIMITED SENSOR DATA";
    case STATE_SENSOR_ERROR: return "CHECK SENSOR";
    default:                 return "UNKNOWN";
  }
}

uint16_t stateColor(EnvironmentState state) {
  switch (state) {
    case STATE_GOOD:         return GC9A01A_GREEN;
    case STATE_CAUTION:      return GC9A01A_YELLOW;
    case STATE_POOR:         return GC9A01A_RED;
    case STATE_DEGRADED:     return GC9A01A_YELLOW;
    case STATE_SENSOR_ERROR: return GC9A01A_MAGENTA;
    case STATE_WARMUP:       return 0xFD20; // orange
    default:                 return GC9A01A_CYAN;
  }
}

bool isPlaceholder(const String &value) {
  String v = value;
  v.trim();
  String lower = v;
  lower.toLowerCase();
  return v.length() == 0 || lower.indexOf("your_") >= 0 ||
         lower.indexOf("placeholder") >= 0 || lower.indexOf("paste_") >= 0;
}

bool wifiCredentialsConfigured() {
  return !isPlaceholder(activeWifiSsid);
}

bool cloudCredentialsConfigured() {
  return !isPlaceholder(activeTbHost) && !isPlaceholder(activeTbToken) && activeTbPort > 0;
}

bool credentialsConfigured() {
  return wifiCredentialsConfigured() && cloudCredentialsConfigured();
}

void wifiVaultKey(char *out, size_t outSize, char kind, uint8_t index) {
  snprintf(out, outSize, "wv%c%u", kind, index);
}

int findWifiVaultIndex(const String &ssid) {
  for (uint8_t i = 0; i < wifiVaultCount; ++i) {
    if (wifiVault[i].ssid == ssid) return static_cast<int>(i);
  }
  return -1;
}

void clearWifiVaultRam() {
  for (uint8_t i = 0; i < WIFI_SAVED_NETWORK_LIMIT; ++i) {
    wifiVault[i].ssid = "";
    wifiVault[i].password = "";
  }
  wifiVaultCount = 0;
}

bool persistWifiVault() {
  preferences.putUShort("wvSchema", WIFI_VAULT_SCHEMA_VERSION);
  preferences.putUShort("wvCount", wifiVaultCount);

  for (uint8_t i = 0; i < WIFI_SAVED_NETWORK_LIMIT; ++i) {
    char ssidKey[8];
    char passKey[8];
    wifiVaultKey(ssidKey, sizeof(ssidKey), 'S', i);
    wifiVaultKey(passKey, sizeof(passKey), 'P', i);
    if (i < wifiVaultCount) {
      preferences.putString(ssidKey, wifiVault[i].ssid);
      preferences.putString(passKey, wifiVault[i].password);
    } else {
      preferences.remove(ssidKey);
      preferences.remove(passKey);
    }
  }

  // Compatibility mirror for older HP14 releases and external diagnostics.
  if (wifiVaultCount > 0) {
    preferences.putString("wifiSsid", wifiVault[0].ssid);
    preferences.putString("wifiPass", wifiVault[0].password);
    preferences.putString("ssid", wifiVault[0].ssid);
    preferences.putString("pass", wifiVault[0].password);
  }
  delay(20);

  const uint8_t verifyCount = static_cast<uint8_t>(preferences.getUShort("wvCount", 0));
  if (verifyCount != wifiVaultCount) return false;
  if (wifiVaultCount > 0) {
    const String verifySsid = preferences.getString("wvS0", "");
    if (verifySsid != wifiVault[0].ssid) return false;
  }
  return true;
}

bool promoteWifiInVault(const String &ssidInput, const String &passwordInput,
                        bool writeToFlash = true) {
  String ssid = ssidInput;
  ssid.trim();
  if (isPlaceholder(ssid)) return false;

  int existing = findWifiVaultIndex(ssid);
  String password = passwordInput;
  if (existing >= 0 && password.length() == 0 &&
      wifiVault[existing].password.length() > 0) {
    password = wifiVault[existing].password;
  }

  // Already the newest entry with unchanged credentials: avoid needless NVS writes.
  if (existing == 0 && wifiVault[0].password == password) {
    savedWifiSsid = wifiVault[0].ssid;
    savedWifiPassword = wifiVault[0].password;
    wifiActiveVaultIndex = 0;
    setWifiVaultCandidateOrderMru();
    wifiVaultCandidateCursor = wifiVaultCount > 0 ? 1 : 0;
    return true;
  }

  if (existing >= 0) {
    for (uint8_t i = static_cast<uint8_t>(existing); i + 1U < wifiVaultCount; ++i) {
      wifiVault[i] = wifiVault[i + 1U];
    }
    --wifiVaultCount;
  } else if (wifiVaultCount >= WIFI_SAVED_NETWORK_LIMIT) {
    // Drop the oldest entry before inserting the newly successful network.
    wifiVaultCount = WIFI_SAVED_NETWORK_LIMIT - 1U;
  }

  for (uint8_t i = wifiVaultCount; i > 0; --i) {
    wifiVault[i] = wifiVault[i - 1U];
  }
  wifiVault[0].ssid = ssid;
  wifiVault[0].password = password;
  if (wifiVaultCount < WIFI_SAVED_NETWORK_LIMIT) ++wifiVaultCount;

  savedWifiSsid = wifiVault[0].ssid;
  savedWifiPassword = wifiVault[0].password;
  wifiActiveVaultIndex = 0;
  wifiVaultNextIndex = wifiVaultCount > 0 ? 1 : 0;
  setWifiVaultCandidateOrderMru();
  wifiVaultCandidateCursor = wifiVaultCount > 0 ? 1 : 0;

  if (!writeToFlash) return true;
  const bool ok = persistWifiVault();
  Serial.printf("[WiFi] Vault %s: %s (%u/%u saved)\n",
                ok ? "updated" : "write FAILED", ssid.c_str(),
                wifiVaultCount, WIFI_SAVED_NETWORK_LIMIT);
  return ok;
}

void loadWifiVault() {
  clearWifiVaultRam();
  const uint8_t storedRawCount = static_cast<uint8_t>(preferences.getUShort("wvCount", 0));
  const uint8_t storedCount = storedRawCount < WIFI_SAVED_NETWORK_LIMIT
                                  ? storedRawCount
                                  : WIFI_SAVED_NETWORK_LIMIT;

  for (uint8_t i = 0; i < storedCount; ++i) {
    char ssidKey[8];
    char passKey[8];
    wifiVaultKey(ssidKey, sizeof(ssidKey), 'S', i);
    wifiVaultKey(passKey, sizeof(passKey), 'P', i);
    String ssid = preferences.getString(ssidKey, "");
    String password = preferences.getString(passKey, "");
    ssid.trim();
    if (isPlaceholder(ssid) || findWifiVaultIndex(ssid) >= 0) continue;
    wifiVault[wifiVaultCount].ssid = ssid;
    wifiVault[wifiVaultCount].password = password;
    ++wifiVaultCount;
  }

  // One-time migration from v1.1 single-network keys / secrets.h fallback.
  const String legacySsid = preferences.getString(
      "wifiSsid", preferences.getString("ssid", WIFI_SSID));
  const String legacyPassword = preferences.getString(
      "wifiPass", preferences.getString("pass", WIFI_PASSWORD));
  if (wifiVaultCount == 0 && !isPlaceholder(legacySsid)) {
    promoteWifiInVault(legacySsid, legacyPassword, true);
    Serial.printf("[WiFi] Migrated legacy WiFi into 5-network vault: %s\n",
                  legacySsid.c_str());
  } else if (wifiVaultCount > 0) {
    savedWifiSsid = wifiVault[0].ssid;
    savedWifiPassword = wifiVault[0].password;
  }

  Serial.printf("[WiFi] Vault loaded: %u/%u network(s).\n",
                wifiVaultCount, WIFI_SAVED_NETWORK_LIMIT);
  for (uint8_t i = 0; i < wifiVaultCount; ++i) {
    Serial.printf("[WiFi]   MRU #%u: %s\n", i + 1U, wifiVault[i].ssid.c_str());
  }
}

void setWifiVaultCandidateOrderMru() {
  wifiVaultCandidateCount = 0;
  wifiVaultCandidateCursor = 0;
  for (uint8_t i = 0; i < wifiVaultCount; ++i) {
    wifiVaultCandidateOrder[wifiVaultCandidateCount++] = i;
  }
  wifiVaultCandidateOrderReady = true;
}

void prepareWifiVaultCandidateOrder() {
  wifiVaultCandidateCount = 0;
  wifiVaultCandidateCursor = 0;
  wifiVaultCandidateOrderReady = true;
  if (wifiVaultCount == 0) return;

  if (!WIFI_SCAN_SAVED_NETWORKS_ON_BOOT) {
    setWifiVaultCandidateOrderMru();
    return;
  }

  if (wifiSavedScanRound < 255) ++wifiSavedScanRound;
  Serial.printf("[WiFi] Scanning round %u/%u for %u saved network(s)...\n",
                wifiSavedScanRound, WIFI_SAVED_SCAN_MAX_ROUNDS, wifiVaultCount);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  delay(60);
  const int scanCount = WiFi.scanNetworks(false, true, false,
                                           WIFI_PORTAL_SCAN_MS_PER_CHANNEL);
  if (scanCount < 0) {
    Serial.printf("[WiFi] Saved-network scan failed (%d); falling back to MRU order.\n",
                  scanCount);
    WiFi.scanDelete();
    setWifiVaultCandidateOrderMru();
    return;
  }

  int bestRssi[WIFI_SAVED_NETWORK_LIMIT];
  bool selected[WIFI_SAVED_NETWORK_LIMIT];
  for (uint8_t i = 0; i < WIFI_SAVED_NETWORK_LIMIT; ++i) {
    bestRssi[i] = -127;
    selected[i] = false;
  }

  for (int scanIndex = 0; scanIndex < scanCount; ++scanIndex) {
    const String scannedSsid = WiFi.SSID(scanIndex);
    const int vaultIndex = findWifiVaultIndex(scannedSsid);
    if (vaultIndex >= 0 && WiFi.RSSI(scanIndex) > bestRssi[vaultIndex]) {
      bestRssi[vaultIndex] = WiFi.RSSI(scanIndex);
    }
  }

  // Strongest visible saved network first; MRU order breaks equal-RSSI ties.
  while (wifiVaultCandidateCount < wifiVaultCount) {
    int chosen = -1;
    int chosenRssi = -128;
    for (uint8_t i = 0; i < wifiVaultCount; ++i) {
      if (selected[i] || bestRssi[i] <= -127) continue;
      if (chosen < 0 || bestRssi[i] > chosenRssi) {
        chosen = static_cast<int>(i);
        chosenRssi = bestRssi[i];
      }
    }
    if (chosen < 0) break;
    selected[chosen] = true;
    wifiVaultCandidateOrder[wifiVaultCandidateCount++] = static_cast<uint8_t>(chosen);
  }

  if (WIFI_TRY_SAVED_HIDDEN_NETWORKS) {
    for (uint8_t i = 0; i < wifiVaultCount; ++i) {
      if (!selected[i]) wifiVaultCandidateOrder[wifiVaultCandidateCount++] = i;
    }
  }

  // First scan after boot: if the router is still starting after a power cut,
  // give the most recently used network one normal connection window, then rescan.
  if (wifiVaultCandidateCount == 0 && wifiSavedScanRound == 1 && wifiVaultCount > 0) {
    wifiVaultCandidateOrder[wifiVaultCandidateCount++] = 0;
    Serial.printf("[WiFi] No saved SSID visible yet; grace attempt for MRU: %s\n",
                  wifiVault[0].ssid.c_str());
  }

  WiFi.scanDelete();
  Serial.printf("[WiFi] Visible saved candidates: %u/%u.\n",
                wifiVaultCandidateCount, wifiVaultCount);
  for (uint8_t i = 0; i < wifiVaultCandidateCount; ++i) {
    const uint8_t index = wifiVaultCandidateOrder[i];
    Serial.printf("[WiFi]   Candidate #%u: %s RSSI=%d dBm\n",
                  i + 1U, wifiVault[index].ssid.c_str(), bestRssi[index]);
  }
}

bool activateWifiVaultIndex(uint8_t index) {
  if (index >= wifiVaultCount || isPlaceholder(wifiVault[index].ssid)) return false;
  activeWifiSsid = wifiVault[index].ssid;
  activeWifiPassword = wifiVault[index].password;
  wifiActiveVaultIndex = static_cast<int8_t>(index);
  wifiBootAttemptCount = 0;
  wifiAttemptActive = false;
  lastWifiAttemptMs = 0;
  wifiCycleRetryNotBeforeMs = 0;
  Serial.printf("[WiFi] Selected saved network %u/%u: %s\n",
                index + 1U, wifiVaultCount, activeWifiSsid.c_str());
  return true;
}

void resetWifiVaultCandidateCycle() {
  wifiVaultNextIndex = 0;
  wifiActiveVaultIndex = -1;
  wifiVaultCandidateCount = 0;
  wifiVaultCandidateCursor = 0;
  wifiVaultCandidateOrderReady = false;
  wifiBootAttemptCount = 0;
  wifiAttemptActive = false;
  lastWifiAttemptMs = 0;
}

bool activateNextSavedWifi() {
  if (!wifiVaultCandidateOrderReady) prepareWifiVaultCandidateOrder();
  while (wifiVaultCandidateCursor < wifiVaultCandidateCount) {
    const uint8_t index = wifiVaultCandidateOrder[wifiVaultCandidateCursor++];
    wifiVaultNextIndex = wifiVaultCandidateCursor;
    if (activateWifiVaultIndex(index)) return true;
  }
  return false;
}

void resetWifiCycleToMostRecent() {
  resetWifiVaultCandidateCycle();
  setWifiVaultCandidateOrderMru();
  activateNextSavedWifi();
}

void rememberConnectedWifi() {
  const String connectedSsid = WiFi.SSID();
  if (connectedSsid.length() == 0 || connectedSsid == lastRememberedConnectedSsid) return;
  if (promoteWifiInVault(connectedSsid, activeWifiPassword, true)) {
    lastRememberedConnectedSsid = connectedSsid;
    activeWifiSsid = wifiVault[0].ssid;
    activeWifiPassword = wifiVault[0].password;
    Serial.printf("[WiFi] MRU promoted after successful connection: %s\n",
                  connectedSsid.c_str());
  }
}

void printWifiVault() {
  Serial.printf("[WiFi] Saved vault: %u/%u (newest successful first)\n",
                wifiVaultCount, WIFI_SAVED_NETWORK_LIMIT);
  for (uint8_t i = 0; i < wifiVaultCount; ++i) {
    Serial.printf("[WiFi]   #%u %s\n", i + 1U, wifiVault[i].ssid.c_str());
  }
}

void loadNetworkSettings() {
  loadWifiVault();

  pendingWifiSsid = preferences.getString("pendingSsid", "");
  pendingWifiPassword = preferences.getString("pendingPass", "");
  pendingWifiValid = preferences.getBool("pendingValid", false) &&
                     pendingWifiSsid.length() > 0;

  resetWifiVaultCandidateCycle();
  tryingPendingWifi = pendingWifiValid;
  if (tryingPendingWifi) {
    activeWifiSsid = pendingWifiSsid;
    activeWifiPassword = pendingWifiPassword;
    Serial.printf("[WiFi] Pending candidate found: %s\n", activeWifiSsid.c_str());
  } else if (wifiVaultCount > 0) {
    prepareWifiVaultCandidateOrder();
    activateNextSavedWifi();
  } else {
    activeWifiSsid = "";
    activeWifiPassword = "";
  }

  activeTbHost = preferences.getString("tbHost", TB_HOST);
  activeTbToken = preferences.getString("tbToken", TB_TOKEN);
  activeTbPort = preferences.getUShort("tbPort", TB_PORT);

  if (preferences.isKey("forcePortal")) preferences.remove("forcePortal");
  forceWifiPortalOnBoot = false;
}

uint32_t focusElapsedSec() {
  return focusSessionActive && focusSessionStartedMs ?
         (millis() - focusSessionStartedMs) / 1000UL : 0UL;
}

void showToast(const String &message, uint32_t durationMs = 1700UL) {
  uiToast = message;
  uiToastUntilMs = millis() + durationMs;
  uiDirty = true;
}

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

String formatDuration(uint32_t seconds) {
  const uint32_t hours = seconds / 3600UL;
  const uint32_t minutes = (seconds % 3600UL) / 60UL;
  char buffer[20];
  if (hours > 0) snprintf(buffer, sizeof(buffer), "%luh%02lum",
                          static_cast<unsigned long>(hours),
                          static_cast<unsigned long>(minutes));
  else snprintf(buffer, sizeof(buffer), "%lum",
                static_cast<unsigned long>(minutes));
  return String(buffer);
}

// =============================================================================
// 6) LED control
// =============================================================================

void setExternalLeds(bool green, bool yellow, bool red) {
  if (!FEATURE_EXTERNAL_LEDS) return;
  digitalWrite(PIN_LED_GREEN, green ? HIGH : LOW);
  digitalWrite(PIN_LED_YELLOW, yellow ? HIGH : LOW);
  digitalWrite(PIN_LED_RED, red ? HIGH : LOW);
}

void setBoardRgb(uint8_t r, uint8_t g, uint8_t b) {
  if (!FEATURE_BOARD_RGB) return;
  if (r == lastRgbR && g == lastRgbG && b == lastRgbB) return;
  lastRgbR = r;
  lastRgbG = g;
  lastRgbB = b;
  boardRgb.setPixelColor(0, boardRgb.Color(r, g, b));
  boardRgb.show();
}

void flashInteraction(uint8_t r, uint8_t g, uint8_t b, uint16_t durationMs = 180) {
  feedbackR = r;
  feedbackG = g;
  feedbackB = b;
  feedbackUntilMs = millis() + durationMs;
}

void startupSelfTest() {
  if (FEATURE_EXTERNAL_LEDS) {
    setExternalLeds(true, false, false);
    delay(180);
    setExternalLeds(false, true, false);
    delay(180);
    setExternalLeds(false, false, true);
    delay(180);
    setExternalLeds(false, false, false);
  }

  if (FEATURE_BOARD_RGB) {
    setBoardRgb(0, 0, 20);
    delay(100);
    setBoardRgb(0, 0, 0);
  }
}

void updateStatusLeds(uint32_t now) {
  const bool slow = ((now / 700UL) % 2UL) == 0;
  const bool fast = ((now / 220UL) % 2UL) == 0;

  if (portalActive) {
    setExternalLeds(false, slow, false);
  } else switch (data.state) {
    case STATE_GOOD:
      setExternalLeds(true, false, false);
      break;
    case STATE_CAUTION:
      setExternalLeds(false, alertAcknowledged ? true : slow, false);
      break;
    case STATE_POOR:
      setExternalLeds(false, false, alertAcknowledged ? true : fast);
      break;
    case STATE_DEGRADED:
      setExternalLeds(false, slow, !slow);
      break;
    case STATE_SENSOR_ERROR:
      setExternalLeds(false, fast, !fast);
      break;
    case STATE_WARMUP:
      setExternalLeds(false, slow, false);
      break;
    default:
      setExternalLeds(false, false, false);
      break;
  }

  // Short interaction flash has priority over connection status.
  if (now < feedbackUntilMs) {
    setBoardRgb(feedbackR, feedbackG, feedbackB);
    return;
  }

  // On-board RGB communicates technical connection state.
  if (portalActive) {
    setBoardRgb(0, slow ? 18 : 5, slow ? 18 : 5); // cyan = setup portal
  } else if (!wifiCredentialsConfigured()) {
    setBoardRgb(14, 0, 14);      // purple = no WiFi
  } else if (WiFi.status() != WL_CONNECTED) {
    setBoardRgb(0, 0, slow ? 18 : 0); // blue blink
  } else if (!mqttClient.connected()) {
    setBoardRgb(slow ? 18 : 0, slow ? 9 : 0, 0); // amber blink
  } else {
    setBoardRgb(0, 16, 0);       // green = ThingsBoard online
  }
}

// =============================================================================
// 7) MQ-135 acquisition and baseline
// =============================================================================

void saveMqBaseline(float baselineMv) {
  if (!isfinite(baselineMv) || baselineMv < 50.0f || baselineMv > 5000.0f) return;
  preferences.putFloat("mqbase", baselineMv);
  preferences.putUShort("mqschema", MQ_BASELINE_SCHEMA_VERSION);
  lastBaselineSaveMs = millis();
  Serial.printf("[MQ] Baseline saved: %.1f mV\n", baselineMv);
}

void startMqCalibration(const char *reason) {
  mqBaselineAccumulator = 0.0f;
  mqBaselineSamples = 0;
  mqMismatchCounter = 0;
  data.mqCalibrating = true;
  data.mqReady = false;
  data.gasRatio = NAN;
  data.gasChangePct = NAN;
  uiDirty = true;
  Serial.printf("[MQ] Baseline learning started (%s).\n", reason);
}

void readMq135() {
  uint32_t rawSum = 0;
  uint32_t mvSum = 0;

  for (uint8_t i = 0; i < MQ_ADC_SAMPLE_COUNT; ++i) {
    rawSum += analogRead(PIN_MQ135_ADC);
    mvSum += analogReadMilliVolts(PIN_MQ135_ADC);
    delayMicroseconds(160);
  }

  data.mqRaw = static_cast<uint16_t>(rawSum / MQ_ADC_SAMPLE_COUNT);
  data.mqAdcMv = static_cast<float>(mvSum) / MQ_ADC_SAMPLE_COUNT;
  data.mqSensorMv = data.mqAdcMv * MQ_DIVIDER_RECONSTRUCTION_FACTOR;

  if (!isfinite(data.mqFilteredMv)) data.mqFilteredMv = data.mqSensorMv;
  else data.mqFilteredMv += MQ_FILTER_ALPHA * (data.mqSensorMv - data.mqFilteredMv);

  data.mqElectricalOk = data.mqAdcMv > 20.0f && data.mqAdcMv < 2540.0f;
  data.mqWarm = millis() >= MQ_OPERATIONAL_WARMUP_MS;

  if (!data.mqElectricalOk || !data.mqWarm) {
    data.mqReady = false;
    data.gasRatio = NAN;
    data.gasChangePct = NAN;
    return;
  }

  if (!data.mqCalibrating && data.mqBaselineMv < 50.0f) {
    startMqCalibration("no valid baseline");
  }

  if (data.mqCalibrating) {
    mqBaselineAccumulator += data.mqFilteredMv;
    ++mqBaselineSamples;

    if (mqBaselineSamples >= MQ_BASELINE_SAMPLE_COUNT) {
      data.mqBaselineMv = mqBaselineAccumulator / mqBaselineSamples;
      data.mqCalibrating = false;
      data.mqReady = true;
      saveMqBaseline(data.mqBaselineMv);
      Serial.printf("[MQ] Baseline ready: %.1f mV\n", data.mqBaselineMv);
    }
    return;
  }

  if (data.mqBaselineMv > 50.0f) {
    const float provisionalRatio = data.mqFilteredMv / data.mqBaselineMv;

    if (provisionalRatio < MQ_BASELINE_MISMATCH_LOW ||
        provisionalRatio > MQ_BASELINE_MISMATCH_HIGH) {
      if (mqMismatchCounter < 255) ++mqMismatchCounter;
    } else {
      mqMismatchCounter = 0;
    }

    if (mqMismatchCounter >= MQ_BASELINE_MISMATCH_SAMPLES) {
      data.mqBaselineMv = 0.0f;
      preferences.remove("mqbase");
      startMqCalibration("stored baseline mismatch");
      return;
    }

    data.mqReady = true;
    data.gasRatio = provisionalRatio;
    data.gasChangePct = (data.gasRatio - 1.0f) * 100.0f;

    // Track clean-room drift very slowly, but never learn during a spike.
    if (data.gasRatio > 0.92f && data.gasRatio < 1.04f) {
      data.mqBaselineMv = data.mqBaselineMv * 0.9995f +
                          data.mqFilteredMv * 0.0005f;
      if ((millis() - lastBaselineSaveMs) >= BASELINE_SAVE_MIN_INTERVAL_MS) {
        saveMqBaseline(data.mqBaselineMv);
      }
    }
  }
}

// =============================================================================
// 8) DHT11 acquisition
// =============================================================================

void readDht11() {
  const float humidity = dht.readHumidity();
  const float temperature = dht.readTemperature();

  const bool valid = isfinite(humidity) && isfinite(temperature) &&
                     humidity >= 0.0f && humidity <= 100.0f &&
                     temperature > -10.0f && temperature < 60.0f;

  if (valid) {
    data.humidityPct = humidity;
    data.temperatureC = temperature;
    data.heatIndexC = dht.computeHeatIndex(temperature, humidity, false);
    data.dhtOk = true;
    data.dhtFresh = true;
    data.dhtFailureCount = 0;
    data.lastDhtGoodMs = millis();
    return;
  }

  if (data.dhtFailureCount < 255) ++data.dhtFailureCount;
  data.dhtFresh = false;

  // Keep the last good sample through short DHT11 glitches.
  if (data.lastDhtGoodMs > 0 &&
      (millis() - data.lastDhtGoodMs) <= DHT_STALE_TIMEOUT_MS) {
    data.dhtOk = true;
  } else {
    data.dhtOk = false;
  }

  if (data.dhtFailureCount == DHT_FAILURES_BEFORE_WARNING) {
    Serial.println(F("[DHT] Repeated read failures; keeping last good value temporarily."));
  }
}

// =============================================================================
// 9) Environment assessment
// =============================================================================

int calculateThermalScore() {
  if (!data.dhtOk) return 0;

  int score = 100;

  if (data.temperatureC < TEMP_TARGET_MIN_C) {
    score -= static_cast<int>(min(35.0f,
        (TEMP_TARGET_MIN_C - data.temperatureC) * 6.0f));
  } else if (data.temperatureC > TEMP_TARGET_MAX_C) {
    score -= static_cast<int>(min(45.0f,
        (data.temperatureC - TEMP_TARGET_MAX_C) * 7.0f));
  }

  if (data.humidityPct < HUM_TARGET_MIN_PCT) {
    score -= static_cast<int>(min(25.0f,
        (HUM_TARGET_MIN_PCT - data.humidityPct) * 0.8f));
  } else if (data.humidityPct > HUM_TARGET_MAX_PCT) {
    score -= static_cast<int>(min(35.0f,
        (data.humidityPct - HUM_TARGET_MAX_PCT) * 1.2f));
  }

  if (isfinite(data.heatIndexC)) {
    if (data.heatIndexC >= HEAT_INDEX_POOR_C) score -= 20;
    else if (data.heatIndexC >= HEAT_INDEX_CAUTION_C) score -= 10;
  }

  return constrain(score, 0, 100);
}

void evaluateEnvironment() {
  data.thermalScore = calculateThermalScore();

  const bool thermalAvailable = data.dhtOk;
  const bool gasElectricalAvailable = data.mqElectricalOk;

  if (!thermalAvailable && !gasElectricalAvailable) {
    data.experimentalScore = 0;
    data.scoreValid = false;
    data.state = STATE_SENSOR_ERROR;
    return;
  }

  // One sensor may be temporarily unavailable. Keep the system usable and quiet.
  if (!thermalAvailable || !gasElectricalAvailable) {
    data.experimentalScore = thermalAvailable ? data.thermalScore : 50;
    data.scoreValid = false;
    data.state = STATE_DEGRADED;
    return;
  }

  if (!data.mqWarm || data.mqCalibrating || !data.mqReady) {
    data.experimentalScore = data.thermalScore;
    data.scoreValid = false;
    data.state = STATE_WARMUP;
    return;
  }

  int score = data.thermalScore;

  if (isfinite(data.gasRatio)) {
    if (data.gasRatio > GAS_RATIO_CAUTION_MAX) {
      score -= static_cast<int>(min(60.0f,
          35.0f + (data.gasRatio - GAS_RATIO_CAUTION_MAX) * 50.0f));
    } else if (data.gasRatio > GAS_RATIO_GOOD_MAX) {
      score -= static_cast<int>(15.0f +
          (data.gasRatio - GAS_RATIO_GOOD_MAX) /
          (GAS_RATIO_CAUTION_MAX - GAS_RATIO_GOOD_MAX) * 20.0f);
    }
  }

  data.experimentalScore = constrain(score, 0, 100);
  data.scoreValid = true;

  if (data.experimentalScore >= 80 && data.gasRatio <= GAS_RATIO_GOOD_MAX) {
    data.state = STATE_GOOD;
  } else if (data.experimentalScore >= 60 &&
             data.gasRatio <= GAS_RATIO_CAUTION_MAX) {
    data.state = STATE_CAUTION;
  } else {
    data.state = STATE_POOR;
  }
}

void notifyStateTransition(EnvironmentState oldState, EnvironmentState newState) {
  if (oldState == newState) return;

  if (newState == STATE_CAUTION || newState == STATE_POOR) {
    alertAcknowledged = alertsMuted;
    lastAlertReminderMs = millis();
  }

  Serial.printf("[STATE] %s -> %s\n", stateCode(oldState), stateCode(newState));
}

void pushGasHistory() {
  if (!data.mqReady || !isfinite(data.gasRatio)) return;
  gasRatioHistory[gasHistoryHead] = data.gasRatio;
  gasHistoryHead = (gasHistoryHead + 1U) % GAS_HISTORY_COUNT;
  if (gasHistoryUsed < GAS_HISTORY_COUNT) ++gasHistoryUsed;
}

void sampleSensors() {
  const EnvironmentState previousState = data.state;

  readDht11();
  readMq135();
  evaluateEnvironment();
  notifyStateTransition(previousState, data.state);

  ++data.sampleCounter;
  pushGasHistory();
  uiDirty = true;

  const uint32_t now = millis();
  if (lastDataLogMs == 0 || (now - lastDataLogMs) >= SERIAL_DATA_LOG_INTERVAL_MS) {
    lastDataLogMs = now;
    Serial.printf(
        "[DATA] T=%.1fC H=%.1f%% HI=%.1f MQraw=%u AO=%.0fmV filt=%.0fmV "
        "base=%.0fmV ratio=%.3f score=%d valid=%d dhtFresh=%d state=%s\n",
        data.temperatureC,
        data.humidityPct,
        data.heatIndexC,
        data.mqRaw,
        data.mqSensorMv,
        data.mqFilteredMv,
        data.mqBaselineMv,
        data.gasRatio,
        data.experimentalScore,
        data.scoreValid ? 1 : 0,
        data.dhtFresh ? 1 : 0,
        stateCode(data.state));
  }
}

// =============================================================================
// 10) ThingsBoard / MQTT — compact, quota-safe and reconnect-safe
// =============================================================================

String buildMqttClientId() {
  const uint64_t chipId = ESP.getEfuseMac();
  char id[32];
  snprintf(id, sizeof(id), "HP14-%04X%08X",
           static_cast<uint16_t>(chipId >> 32),
           static_cast<uint32_t>(chipId));
  return String(id);
}

void serviceMqtt(uint32_t now) {
  if (portalActive || !cloudCredentialsConfigured() ||
      WiFi.status() != WL_CONNECTED) {
    if (mqttClient.connected()) mqttClient.disconnect();
    if (previousMqttConnected) {
      previousMqttConnected = false;
      mqttConnectedSinceMs = 0;
      uiDirty = true;
    }
    return;
  }

  if (mqttClient.connected()) {
    mqttClient.loop();
    if (!previousMqttConnected) {
      previousMqttConnected = true;
      mqttConnectedSinceMs = now;
      uiDirty = true;
      Serial.println(F("[MQTT] ThingsBoard connected; quota-safe settle timer started."));
    }
    return;
  }

  if (previousMqttConnected) {
    previousMqttConnected = false;
    mqttConnectedSinceMs = 0;
    uiDirty = true;
  }

  if (lastMqttAttemptMs > 0 && (now - lastMqttAttemptMs) < mqttRetryMs) return;
  if (menuButton.isPressed() || ackButton.isPressed() || dualHoldActive) return;
  if ((now - lastUserActionMs) < 2500UL) return;

  lastMqttAttemptMs = now;
  const String clientId = buildMqttClientId();
  Serial.printf("[MQTT] Connecting to %s:%u ...\n",
                activeTbHost.c_str(), activeTbPort);

  if (mqttClient.connect(clientId.c_str(), activeTbToken.c_str(), nullptr)) {
    mqttRetryMs = MQTT_RETRY_MIN_MS;
    mqttPublishFailures = 0;
    previousMqttConnected = true;
    mqttConnectedSinceMs = now;
    uiDirty = true;
    Serial.println(F("[MQTT] Connected. No reconnect telemetry burst will be sent."));
  } else {
    Serial.printf("[MQTT] Connect failed, state=%d; next retry is backed off.\n",
                  mqttClient.state());
    mqttRetryMs = min(mqttRetryMs * 2UL, MQTT_RETRY_MAX_MS);
  }
}

void formatJsonFloat(float value, char *buffer, size_t size, uint8_t decimals) {
  if (!isfinite(value)) {
    snprintf(buffer, size, "null");
    return;
  }
  snprintf(buffer, size, "%.*f", decimals, value);
}

void resetTelemetryRateWindowIfNeeded(uint32_t now) {
  if (telemetryWindowStartedMs == 0 ||
      (now - telemetryWindowStartedMs) >= TELEMETRY_RATE_WINDOW_MS) {
    telemetryWindowStartedMs = now;
    telemetryMessagesThisWindow = 0;
    telemetryQuotaLogPrinted = false;
  }
}

bool telemetryRatePermit(uint32_t now) {
  resetTelemetryRateWindowIfNeeded(now);

  if (telemetryMessagesThisWindow >= TELEMETRY_MAX_MESSAGES_PER_WINDOW) {
    if (!telemetryQuotaLogPrinted) {
      telemetryQuotaLogPrinted = true;
      Serial.printf("[TB] Hourly safety cap reached (%u messages). Cloud upload paused until next window.\n",
                    TELEMETRY_MAX_MESSAGES_PER_WINDOW);
    }
    return false;
  }

  if (lastTelemetryAttemptMs > 0 &&
      (now - lastTelemetryAttemptMs) < TELEMETRY_MIN_GAP_MS) {
    return false;
  }

  if (telemetryNextAttemptNotBeforeMs > 0 &&
      static_cast<int32_t>(now - telemetryNextAttemptNotBeforeMs) < 0) {
    return false;
  }

  return true;
}

bool publishTelemetry() {
  if (!mqttClient.connected()) return false;

  char t[20], h[20], hi[20], ratio[20];
  formatJsonFloat(data.temperatureC, t, sizeof(t), 1);
  formatJsonFloat(data.humidityPct, h, sizeof(h), 1);
  formatJsonFloat(data.heatIndexC, hi, sizeof(hi), 1);
  formatJsonFloat(data.gasRatio, ratio, sizeof(ratio), 3);

  // Exactly 8 essential keys. High-volume diagnostics stay local on Serial/TFT/OLED.
  char payload[512];
  const int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127;
  const int written = snprintf(
      payload, sizeof(payload),
      "{"
      "\"temperature\":%s,"
      "\"humidity\":%s,"
      "\"heatIndex\":%s,"
      "\"dwScoreExperimental\":%d,"
      "\"dwScoreValid\":%s,"
      "\"environmentState\":\"%s\","
      "\"gasRatioRelative\":%s,"
      "\"wifiRssi\":%d"
      "}",
      t, h, hi, data.experimentalScore,
      data.scoreValid ? "true" : "false",
      stateCode(data.state), ratio, rssi);

  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    Serial.println(F("[TB] Compact payload overflow; telemetry not sent."));
    return false;
  }

  const bool ok = mqttClient.publish("v1/devices/me/telemetry", payload, false);
  if (ok) {
    mqttPublishFailures = 0;
    Serial.printf("[TB] Quota-safe telemetry published: %u keys, %d bytes.\n",
                  TELEMETRY_KEYS_PER_MESSAGE, written);
  } else {
    if (mqttPublishFailures < 255) ++mqttPublishFailures;
    Serial.printf("[TB] Publish failed; backing off for %lu minutes without reconnect hammering.\n",
                  static_cast<unsigned long>(TELEMETRY_FAILURE_BACKOFF_MS / 60000UL));
  }
  return ok;
}

void serviceTelemetry(uint32_t now) {
  if (!mqttClient.connected() || data.sampleCounter == 0) return;

  // Always let the MQTT session settle after connect/reconnect.
  if (mqttConnectedSinceMs == 0 ||
      (now - mqttConnectedSinceMs) < TELEMETRY_RECONNECT_SETTLE_MS) {
    return;
  }

  const bool firstSendDue = (lastTelemetryMs == 0) &&
                            (now >= TELEMETRY_FIRST_SEND_DELAY_MS);
  const bool periodicSendDue = (lastTelemetryMs > 0) &&
                               ((now - lastTelemetryMs) >= TELEMETRY_INTERVAL_MS);
  if (!firstSendDue && !periodicSendDue) return;
  if (!telemetryRatePermit(now)) return;

  // Mark the attempt before publishing so a failure can never create a tight loop.
  lastTelemetryAttemptMs = now;
  if (publishTelemetry()) {
    lastTelemetryMs = now;
    telemetryNextAttemptNotBeforeMs = now + TELEMETRY_INTERVAL_MS;
    if (telemetryMessagesThisWindow < 255) ++telemetryMessagesThisWindow;
    Serial.printf("[TB] Window usage: %u/%u messages; next normal upload in %lu minutes.\n",
                  telemetryMessagesThisWindow,
                  TELEMETRY_MAX_MESSAGES_PER_WINDOW,
                  static_cast<unsigned long>(TELEMETRY_INTERVAL_MS / 60000UL));
  } else {
    telemetryNextAttemptNotBeforeMs = now + TELEMETRY_FAILURE_BACKOFF_MS;
  }
}

// =============================================================================
// 11) Wi-Fi setup portal + network
// =============================================================================

String htmlEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else out += c;
  }
  return out;
}

String jsEscape(const String &in) {
  String out;
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if (c == '\\' || c == '"') { out += '\\'; out += c; }
    else out += c;
  }
  return out;
}

String wifiQualityText(int rssi) {
  if (rssi >= -55) return F("Rất mạnh");
  if (rssi >= -67) return F("Tốt");
  if (rssi >= WIFI_ACCEPTABLE_RSSI_DBM) return F("Dùng được");
  if (rssi >= WIFI_TOO_WEAK_RSSI_DBM) return F("Yếu");
  return F("Quá yếu");
}


void clearPendingWifi() {
  preferences.remove("pendingSsid");
  preferences.remove("pendingPass");
  preferences.putBool("pendingValid", false);
  pendingWifiSsid = "";
  pendingWifiPassword = "";
  pendingWifiValid = false;
  tryingPendingWifi = false;
}

bool commitPendingWifi() {
  if (!tryingPendingWifi || !pendingWifiValid || pendingWifiSsid.length() == 0) return true;

  const String committedSsid = pendingWifiSsid;
  const String committedPassword = pendingWifiPassword;
  if (!promoteWifiInVault(committedSsid, committedPassword, true)) {
    Serial.println(F("[WiFi] Candidate connected but 5-network vault commit failed."));
    return false;
  }

  activeWifiSsid = committedSsid;
  activeWifiPassword = committedPassword;
  clearPendingWifi();
  lastRememberedConnectedSsid = committedSsid;
  preferences.putString("lastWifiFail", "connected-new-wifi");
  Serial.printf("[WiFi] New WiFi committed to MRU vault: %s (%u/%u)\n",
                activeWifiSsid.c_str(), wifiVaultCount, WIFI_SAVED_NETWORK_LIMIT);
  return true;
}

void cachePortalNetworks() {
  portalNetworkCount = 0;
  portalScanCompletedMs = 0;

  Serial.println(F("[PORTAL] Scanning WiFi before AP starts..."));
  const int n = WiFi.scanNetworks(false, true, false,
                                  WIFI_PORTAL_SCAN_MS_PER_CHANNEL);
  if (n <= 0) {
    Serial.printf("[PORTAL] Scan returned %d; manual SSID entry remains available.\n", n);
    WiFi.scanDelete();
    return;
  }

  for (int i = 0; i < n; ++i) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length()) continue;
    const int rssi = WiFi.RSSI(i);
    const bool isOpen = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;

    int existing = -1;
    for (uint8_t j = 0; j < portalNetworkCount; ++j) {
      if (portalNetworks[j].ssid == ssid) {
        existing = j;
        break;
      }
    }

    if (existing >= 0) {
      if (rssi > portalNetworks[existing].rssi) {
        portalNetworks[existing].rssi = rssi;
        portalNetworks[existing].open = isOpen;
      }
      continue;
    }

    if (portalNetworkCount < PORTAL_NETWORK_CACHE_MAX) {
      portalNetworks[portalNetworkCount].ssid = ssid;
      portalNetworks[portalNetworkCount].rssi = rssi;
      portalNetworks[portalNetworkCount].open = isOpen;
      ++portalNetworkCount;
    }
  }

  for (uint8_t i = 0; i + 1 < portalNetworkCount; ++i) {
    for (uint8_t j = i + 1; j < portalNetworkCount; ++j) {
      if (portalNetworks[j].rssi > portalNetworks[i].rssi) {
        PortalNetworkEntry temp = portalNetworks[i];
        portalNetworks[i] = portalNetworks[j];
        portalNetworks[j] = temp;
      }
    }
  }

  WiFi.scanDelete();
  portalScanCompletedMs = millis();
  Serial.printf("[PORTAL] Cached %u WiFi networks.\n", portalNetworkCount);
}

String portalNetworksHtml() {
  String html;
  html.reserve(5200);

  if (portalNetworkCount == 0) {
    return F("<div class='warn'>Không quét thấy WiFi 2.4GHz. Hãy nhập SSID bằng tay; portal vẫn hoạt động bình thường.</div>");
  }

  html += F("<div class='nets'>");
  for (uint8_t i = 0; i < portalNetworkCount; ++i) {
    const PortalNetworkEntry &net = portalNetworks[i];
    html += F("<button type='button' class='net' data-ssid='");
    html += htmlEscape(net.ssid);
    html += F("' onclick='pick(this.dataset.ssid)'><b>");
    html += htmlEscape(net.ssid);
    html += F("</b><span>");
    html += String(net.rssi);
    html += F(" dBm · ");
    html += net.open ? F("OPEN · ") : F("SECURED · ");
    html += wifiQualityText(net.rssi);
    html += F("</span></button>");
  }
  html += F("</div>");
  return html;
}

const char *wifiProvisionPhaseCode() {
  switch (wifiProvisionPhase) {
    case WIFI_PROVISION_READY: return "ready";
    case WIFI_PROVISION_TESTING: return "testing";
    case WIFI_PROVISION_SUCCESS: return "success";
    case WIFI_PROVISION_FAILED: return "failed";
    case WIFI_PROVISION_CANCELING: return "canceling";
    default: return "idle";
  }
}

String portalPage(const String &message = "") {
  String page;
  page.reserve(12000);
  page += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>HP14 WiFi Setup</title><style>"
            "body{margin:0;background:#052b2f;color:#fff;font-family:Arial,Helvetica,sans-serif}"
            ".wrap{max-width:760px;margin:auto;padding:18px}.card{background:linear-gradient(145deg,#007566,#073842);border:1px solid rgba(255,255,255,.22);border-radius:24px;padding:22px;box-shadow:0 20px 60px rgba(0,0,0,.28)}"
            "h1{font-size:32px;margin:0 0 8px}.sub{font-size:16px;line-height:1.5;color:#dff}.pill{display:inline-block;border:1px solid rgba(255,255,255,.25);border-radius:999px;padding:9px 13px;margin:6px 6px 6px 0;background:rgba(255,255,255,.08)}"
            ".info{background:rgba(170,255,218,.14);border-radius:18px;padding:14px;margin:14px 0;line-height:1.5}.warn{background:rgba(255,205,72,.16);border:1px solid rgba(255,205,72,.55);border-radius:18px;padding:14px;margin:14px 0;line-height:1.5}"
            ".nets{display:grid;gap:8px;margin:14px 0}.net{width:100%;border:0;border-radius:16px;padding:13px 15px;background:#eafff8;color:#062c30;text-align:left;font-size:16px}.net span{float:right;color:#406066}"
            "label{font-weight:700;display:block;margin:16px 0 7px}input{box-sizing:border-box;width:100%;font-size:18px;padding:14px 16px;border-radius:15px;border:0;background:#fff;color:#111}"
            ".btn{width:100%;border:0;border-radius:17px;padding:16px;font-size:18px;font-weight:800;margin-top:16px;background:#73ffd2;color:#062c30}.btn2{background:#eafff8}.small{font-size:14px;color:#dff;line-height:1.5}"
            "@media(max-width:520px){.net span{float:none;display:block;margin-top:4px}}</style>"
            "<script>function pick(s){document.getElementById('ssid').value=s;document.getElementById('pass').focus()}function togglePass(){var p=document.getElementById('pass');p.type=p.type==='password'?'text':'password'}</script>"
            "</head><body><div class='wrap'><div class='card'>");
  page += F("<h1>HP14 WiFi Setup</h1>");
  page += F("<div class='sub'>HP14 giữ tối đa 5 WiFi đã kết nối thành công gần nhất. Mạng mới chỉ được đưa vào bộ nhớ sau khi kiểm tra kết nối thành công.</div>");
  page += F("<div><span class='pill'>AP: "); page += htmlEscape(portalSsid);
  page += F("</span><span class='pill'>IP: 192.168.4.1</span><span class='pill'>Pass AP: ");
  page += WIFI_SETUP_AP_PASSWORD; page += F("</span><span class='pill'>Đã nhớ: "); page += String(wifiVaultCount); page += F("/5 WiFi</span></div>");
  if (message.length()) page += String(F("<div class='info'>")) + htmlEscape(message) + F("</div>");
  page += F("<div class='warn'><b>Nếu điện thoại báo không có Internet:</b><br>chọn <b>Vẫn kết nối / Use without Internet / Keep WiFi connection</b>. Không chuyển điện thoại sang mạng khác trong lúc điền biểu mẫu.</div>");
  page += F("<div class='info'><b>Cách vận hành:</b><br>1. Chọn WiFi 2.4GHz hoặc nhập SSID.<br>2. Nhập mật khẩu.<br>3. Nhấn Lưu & kiểm tra mạng mới.<br>4. Nếu thành công, mạng này lên vị trí số 1 trong kho 5 WiFi; mạng thứ 6 sẽ thay mạng cũ nhất.<br>5. Nếu thất bại, kho WiFi cũ không bị thay đổi.</div>");
  page += F("<h2>WiFi 2.4GHz quét được</h2>");
  page += portalNetworksHtml();
  page += F("<form method='POST' action='/save'>");
  page += F("<label>Tên WiFi mới / SSID</label><input id='ssid' name='ssid' required value='");
  page += htmlEscape(activeWifiSsid);
  page += F("' placeholder='Chọn mạng ở trên hoặc nhập tay'>");
  page += F("<label>Mật khẩu WiFi mới</label><input id='pass' name='pass' type='password' placeholder='Để trống nếu WiFi mở'>");
  page += F("<button class='btn btn2' type='button' onclick='togglePass()'>Hiện / ẩn mật khẩu</button>");
  page += F("<label>ThingsBoard token (không bắt buộc)</label><input name='token' placeholder='Để trống để giữ token hiện tại'>");
  page += F("<button class='btn' type='submit'>Lưu & kiểm tra WiFi mới</button></form>");
  page += F("<form method='POST' action='/cancel'><button class='btn btn2' type='submit'>Hủy · quay lại WiFi đã lưu</button></form>");
  page += F("");
  page += F("<p class='small'>HP14 tự thử lần lượt các WiFi đã nhớ khi khởi động hoặc khi đổi địa điểm. Danh sách quét chỉ dùng cho portal và không làm trang bị treo. Có thể nhập SSID bằng tay nếu mạng ẩn.</p>");
  page += F("</div></div></body></html>");
  return page;
}

String portalProgressPage() {
  String page;
  page.reserve(3600);
  page += F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>HP14 WiFi Saved</title><style>body{margin:0;background:#052b2f;color:#fff;font-family:Arial,Helvetica,sans-serif}.wrap{max-width:620px;margin:auto;padding:22px}.card{margin-top:8vh;background:linear-gradient(145deg,#007566,#073842);border:1px solid rgba(255,255,255,.22);border-radius:24px;padding:26px;text-align:center;box-shadow:0 20px 60px rgba(0,0,0,.28)}.ok{width:64px;height:64px;border-radius:50%;margin:0 auto 16px;background:#73ffd2;color:#052b2f;font-size:42px;line-height:64px;font-weight:900}h1{font-size:28px}.msg{color:#dff;line-height:1.6}.small{font-size:14px;color:#bfe7e5;margin-top:18px}</style></head><body><div class='wrap'><div class='card'><div class='ok'>✓</div><h1>Đã nhận WiFi mới</h1><div class='msg'>");
  page += htmlEscape(wifiProvisionMessage);
  page += F("</div><div class='small'>HP14 sẽ khởi động lại và thử mạng mới. Chỉ khi kết nối thành công, mạng mới được đưa lên đầu kho 5 WiFi; nếu thất bại, danh sách cũ vẫn nguyên vẹn.</div></div></div></body></html>");
  return page;
}

void handlePortalRoot() {
  Serial.printf("[PORTAL] Root served clients=%u cachedNets=%u\n",
                WiFi.softAPgetStationNum(), portalNetworkCount);
  portalServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  portalServer.sendHeader("Connection", "close");
  portalServer.send(200, "text/html; charset=utf-8", portalPage(wifiProvisionMessage));
}

void handlePortalStatus() {
  String json = F("{\"phase\":\"");
  json += wifiProvisionPhaseCode();
  json += F("\",\"ssid\":\"");
  json += jsEscape(candidateWifiSsid.length() ? candidateWifiSsid : activeWifiSsid);
  json += F("\",\"message\":\"");
  json += jsEscape(wifiProvisionMessage);
  json += F("\",\"ip\":\"");
  if (WiFi.status() == WL_CONNECTED) json += WiFi.localIP().toString();
  json += F("\"}");
  portalServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  portalServer.send(200, "application/json; charset=utf-8", json);
}

bool saveWifiCredentialsHp13Style(const String &ssid, const String &pass, const String &token) {
  const size_t ssidBytes = preferences.putString("pendingSsid", ssid);
  const size_t passBytes = preferences.putString("pendingPass", pass);
  preferences.putBool("pendingValid", true);
  if (token.length() >= 10) preferences.putString("tbToken", token);
  preferences.putString("lastWifiFail", "pending-new-wifi");
  if (preferences.isKey("forcePortal")) preferences.remove("forcePortal");
  delay(20);

  const String verifySsid = preferences.getString("pendingSsid", "");
  const bool verifyValid = preferences.getBool("pendingValid", false);
  if (ssidBytes == 0 || !verifyValid || verifySsid != ssid) {
    Serial.println(F("[PORTAL] Pending WiFi save verification failed."));
    preferences.putBool("pendingValid", false);
    (void)passBytes;
    return false;
  }

  pendingWifiSsid = ssid;
  pendingWifiPassword = pass;
  pendingWifiValid = true;
  if (token.length() >= 10) activeTbToken = token;
  mqttClient.setServer(activeTbHost.c_str(), activeTbPort);
  (void)passBytes;
  return true;
}

void handlePortalSave() {
  Serial.printf("[PORTAL] /save received method=%s args=%d\n",
                portalServer.method() == HTTP_POST ? "POST" : "GET",
                portalServer.args());

  String ssid = portalServer.hasArg("ssid") ? portalServer.arg("ssid") : "";
  String pass = portalServer.hasArg("pass") ? portalServer.arg("pass") : "";
  String token = portalServer.hasArg("token") ? portalServer.arg("token") : "";
  ssid.trim();
  token.trim();

  if (!ssid.length()) {
    wifiProvisionPhase = WIFI_PROVISION_FAILED;
    wifiProvisionMessage = F("SSID đang trống. Hãy chọn hoặc nhập tên WiFi.");
    portalServer.sendHeader("Connection", "close");
    portalServer.send(400, "text/html; charset=utf-8", portalPage(wifiProvisionMessage));
    return;
  }

  const int knownWifiIndex = findWifiVaultIndex(ssid);
  if (pass.length() == 0 && knownWifiIndex >= 0 &&
      wifiVault[knownWifiIndex].password.length() > 0) {
    pass = wifiVault[knownWifiIndex].password;
    Serial.printf("[PORTAL] Reusing stored password for known SSID=%s.\n", ssid.c_str());
  }

  if (!saveWifiCredentialsHp13Style(ssid, pass, token)) {
    wifiProvisionPhase = WIFI_PROVISION_FAILED;
    wifiProvisionMessage = F("Không thể ghi cấu hình WiFi chờ vào bộ nhớ. Hãy thử lại.");
    portalServer.sendHeader("Connection", "close");
    portalServer.send(500, "text/html; charset=utf-8", portalPage(wifiProvisionMessage));
    uiDirty = true;
    return;
  }

  wifiProvisionPhase = WIFI_PROVISION_SUCCESS;
  wifiProvisionMessage = String(F("Đã lưu WiFi chờ: ")) + ssid +
                         F(". HP14 sẽ khởi động lại để kiểm tra; kho 5 WiFi cũ chưa bị thay đổi.");
  showToast(F("WIFI RECEIVED · TESTING"), 3200);
  stopBuzzer();
  uiDirty = true;
  lastTftPage = 255;

  portalServer.sendHeader("Cache-Control", "no-store");
  portalServer.sendHeader("Connection", "close");
  portalServer.send(200, "text/html; charset=utf-8", portalProgressPage());
  wifiRestartAtMs = millis() + WIFI_SAVE_RESTART_DELAY_MS;
  Serial.printf("[PORTAL] Pending SSID=%s saved; restart in %lums.\n",
                ssid.c_str(), static_cast<unsigned long>(WIFI_SAVE_RESTART_DELAY_MS));
}

void handlePortalCancel() {
  wifiProvisionPhase = WIFI_PROVISION_CANCELING;
  wifiProvisionMessage = F("Đang đóng portal và quay lại WiFi đã lưu.");
  portalCloseAtMs = millis() + 900UL;
  portalServer.sendHeader("Cache-Control", "no-store");
  portalServer.send(200, "text/html; charset=utf-8", portalPage(wifiProvisionMessage));
  uiDirty = true;
}

bool commitCandidateWifi() {
  // Compatibility stub retained so Arduino auto-prototype generation remains
  // predictable. v1.0 uses HP13's save-and-restart flow instead.
  return false;
}

void stopSetupPortal(bool reconnectSavedWifi) {
  if (!portalActive) return;
  portalServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  portalActive = false;
  wifiProvisionPhase = WIFI_PROVISION_IDLE;
  wifiProvisionMessage = "";
  candidateWifiSsid = "";
  candidateWifiPassword = "";
  candidateTbToken = "";
  portalCloseAtMs = 0;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  previousWifiStatus = WL_NO_SHIELD;
  uiDirty = true;
  lastTftPage = 255;

  if (reconnectSavedWifi && wifiVaultCount > 0) {
    tryingPendingWifi = false;
    resetWifiCycleToMostRecent();
    beginWifiAttempt();
  } else {
    wifiAttemptActive = false;
  }
  Serial.println(F("[PORTAL] Closed."));
}

void serviceWifiProvisioning(uint32_t now) {
  if (!portalActive) return;

  if (wifiRestartAtMs > 0 && static_cast<int32_t>(now - wifiRestartAtMs) >= 0) {
    Serial.println(F("[PORTAL] Restarting now to connect saved WiFi..."));
    stopBuzzer();
    delay(120);
    ESP.restart();
    return;
  }

  if (wifiProvisionPhase == WIFI_PROVISION_CANCELING &&
      portalCloseAtMs > 0 && static_cast<int32_t>(now - portalCloseAtMs) >= 0) {
    stopSetupPortal(true);
  }
}

void startSetupPortal(const String &reason) {
  if (!FEATURE_WIFI_PORTAL || portalActive) return;

  if (preferences.isKey("forcePortal")) preferences.remove("forcePortal");
  forceWifiPortalOnBoot = false;
  portalActive = true;
  portalReason = reason;
  wifiProvisionPhase = WIFI_PROVISION_READY;
  wifiProvisionMessage = (reason == "candidate-wifi-failed")
      ? F("WiFi mới không kết nối được. Kho 5 WiFi cũ vẫn được giữ; hãy kiểm tra lại mật khẩu.")
      : F("Chọn WiFi 2.4GHz mới. HP14 vẫn giữ các mạng đã lưu trước đó.");
  candidateWifiSsid = "";
  candidateWifiPassword = "";
  candidateTbToken = "";
  wifiRestartAtMs = 0;
  portalCloseAtMs = 0;
  wifiAttemptActive = false;
  stopBuzzer();

  if (mqttClient.connected()) mqttClient.disconnect();

  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  WiFi.disconnect(true, false);
  delay(120);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  delay(80);
  cachePortalNetworks();

  WiFi.disconnect(true, false);
  delay(100);
  WiFi.mode(WIFI_AP);
  delay(80);

  const IPAddress apIp(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, gateway, subnet);

  const uint64_t chipId = ESP.getEfuseMac();
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X", static_cast<uint16_t>(chipId & 0xFFFF));
  portalSsid = String(WIFI_SETUP_AP_PREFIX) + "-" + suffix;
  const bool ok = WiFi.softAP(portalSsid.c_str(), WIFI_SETUP_AP_PASSWORD,
                              WIFI_SETUP_AP_CHANNEL, false, WIFI_SETUP_MAX_CLIENTS);
  delay(180);

  dnsServer.stop();
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(WIFI_SETUP_DNS_PORT, "*", apIp);

  if (!portalRoutesReady) {
    portalServer.on("/", HTTP_GET, handlePortalRoot);
    portalServer.on("/save", HTTP_POST, handlePortalSave);
    portalServer.on("/save", HTTP_GET, []() {
      if (portalServer.hasArg("ssid")) handlePortalSave(); else handlePortalRoot();
    });
    portalServer.on("/cancel", HTTP_POST, handlePortalCancel);
    portalServer.on("/status", HTTP_GET, handlePortalStatus);
    portalServer.on("/generate_204", HTTP_GET, handlePortalRoot);
    portalServer.on("/gen_204", HTTP_GET, handlePortalRoot);
    portalServer.on("/hotspot-detect.html", HTTP_GET, handlePortalRoot);
    portalServer.on("/fwlink", HTTP_GET, handlePortalRoot);
    portalServer.on("/connecttest.txt", HTTP_GET, handlePortalRoot);
    portalServer.on("/ncsi.txt", HTTP_GET, handlePortalRoot);
    portalServer.onNotFound([]() {
      portalServer.sendHeader("Location", "http://192.168.4.1/", true);
      portalServer.send(302, "text/plain", "");
    });
    portalRoutesReady = true;
  }

  portalServer.stop();
  portalServer.begin();
  showToast(F("WIFI SETUP · 192.168.4.1"), 4000);
  lastTftPage = 255;
  uiDirty = true;
  Serial.printf("[PORTAL] Stable AP started reason=%s SSID=%s PASS=%s IP=%s nets=%u ok=%s\n",
                reason.c_str(), portalSsid.c_str(), WIFI_SETUP_AP_PASSWORD,
                WiFi.softAPIP().toString().c_str(), portalNetworkCount,
                ok ? "YES" : "NO");
}

void servicePortal() {
  if (!portalActive) return;
  dnsServer.processNextRequest();
  portalServer.handleClient();
}

void openWifiSetupPortal() {
  startSetupPortal(F("dual-buttons-5s"));
}

void beginWifiAttempt() {
  if (portalActive || !wifiCredentialsConfigured() || wifiAttemptActive) return;
  if (wifiBootWindowStartedMs == 0) wifiBootWindowStartedMs = millis();
  ++wifiBootAttemptCount;
  Serial.printf("[WiFi] Attempt %u/%u connecting to %s ...\n",
                wifiBootAttemptCount, WIFI_BOOT_MAX_ATTEMPTS, activeWifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.disconnect(false, false);
  delay(80);
  WiFi.begin(activeWifiSsid.c_str(), activeWifiPassword.c_str());
  wifiAttemptStartedMs = millis();
  lastWifiAttemptMs = wifiAttemptStartedMs;
  wifiAttemptActive = true;
}

void serviceWifi(uint32_t now) {
  if (portalActive) return;
  const wl_status_t status = WiFi.status();

  if (status != previousWifiStatus) {
    previousWifiStatus = status;
    uiDirty = true;
  }

  if (status == WL_CONNECTED) {
    const String connectedSsid = WiFi.SSID();
    if (!wifiEverConnected || connectedSsid != lastRememberedConnectedSsid) {
      Serial.printf("[WiFi] Connected SSID=%s IP=%s RSSI=%d dBm\n",
                    connectedSsid.c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }

    if (tryingPendingWifi) {
      if (!commitPendingWifi()) {
        Serial.println(F("[WiFi] Commit failed; keeping connection for this session."));
      }
    } else {
      rememberConnectedWifi();
    }

    wifiEverConnected = true;
    wifiSavedScanRound = 0;
    wifiCycleRetryNotBeforeMs = 0;
    wifiAttemptActive = false;
    wifiBootAttemptCount = 0;
    setWifiVaultCandidateOrderMru();
    wifiVaultCandidateCursor = wifiVaultCount > 0 ? 1 : 0;
    wifiVaultNextIndex = wifiVaultCandidateCursor;
    wifiActiveVaultIndex = wifiVaultCount > 0 ? 0 : -1;
    mqttRetryMs = MQTT_RETRY_MIN_MS;
    lastMqttAttemptMs = 0;
    return;
  }

  if (!wifiCredentialsConfigured()) {
    if (activateNextSavedWifi()) {
      beginWifiAttempt();
    } else {
      startSetupPortal(F("no-wifi-credentials"));
    }
    return;
  }

  if (wifiAttemptActive && (now - wifiAttemptStartedMs) >= WIFI_CONNECT_ATTEMPT_TIMEOUT_MS) {
    Serial.printf("[WiFi] Attempt timeout SSID=%s status=%d\n",
                  activeWifiSsid.c_str(), static_cast<int>(status));
    WiFi.disconnect(false, false);
    wifiAttemptActive = false;
  }

  // Give the current candidate the configured number of attempts.
  if (wifiCycleRetryNotBeforeMs > 0 &&
      static_cast<int32_t>(now - wifiCycleRetryNotBeforeMs) < 0) {
    return;
  }
  if (wifiCycleRetryNotBeforeMs > 0) wifiCycleRetryNotBeforeMs = 0;

  if (!wifiAttemptActive && wifiBootAttemptCount < WIFI_BOOT_MAX_ATTEMPTS &&
      (lastWifiAttemptMs == 0 || (now - lastWifiAttemptMs) >= WIFI_BOOT_RETRY_GAP_MS)) {
    beginWifiAttempt();
    return;
  }

  if (wifiAttemptActive || wifiBootAttemptCount < WIFI_BOOT_MAX_ATTEMPTS) return;

  // The pending network is never inserted until it succeeds. If it fails,
  // discard only the pending record and continue through all five known WiFis.
  if (tryingPendingWifi) {
    Serial.printf("[WiFi] Pending candidate failed: %s. Trying saved vault.\n",
                  activeWifiSsid.c_str());
    clearPendingWifi();
    resetWifiVaultCandidateCycle();
    prepareWifiVaultCandidateOrder();
  }

  if (activateNextSavedWifi()) {
    beginWifiAttempt();
    return;
  }

  if (!wifiEverConnected) {
    if (wifiSavedScanRound < WIFI_SAVED_SCAN_MAX_ROUNDS) {
      Serial.println(F("[WiFi] Saved candidates failed; rescanning once before portal."));
      resetWifiVaultCandidateCycle();
      prepareWifiVaultCandidateOrder();
      if (activateNextSavedWifi()) {
        beginWifiAttempt();
        return;
      }
    }
    Serial.println(F("[WiFi] All saved WiFi networks unavailable; opening setup portal without deleting the vault."));
    startSetupPortal(F("saved-wifi-unreachable"));
    return;
  }

  // Runtime roaming: after all saved networks fail, wait and cycle again.
  // Temporary router outages never delete credentials or force the portal.
  resetWifiCycleToMostRecent();
  lastWifiAttemptMs = now;
  wifiCycleRetryNotBeforeMs = now + WIFI_RETRY_MS;
  Serial.printf("[WiFi] All %u saved networks unavailable; retry cycle in %lus.\n",
                wifiVaultCount, static_cast<unsigned long>(WIFI_RETRY_MS / 1000UL));
}

// =============================================================================
// 12) OLED UI — compact and synchronized
// =============================================================================

bool initOledAtAddress(uint8_t address) {
  if (!oled.begin(SSD1306_SWITCHCAPVCC, address)) return false;
  oledAddress = address;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(2);
  oled.setCursor(34, 24);
  oled.print(F("HP14"));
  oled.display();
  return true;
}

void oledCenter(const String &text, int16_t y, uint8_t size, bool inverse = false) {
  oled.setTextSize(size);
  int16_t x1, y1; uint16_t w, h;
  oled.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = max((int16_t)0, (int16_t)((OLED_WIDTH - static_cast<int16_t>(w)) / 2));
  if (inverse) {
    oled.fillRoundRect(0, y - 2, OLED_WIDTH, h + 4, 3, SSD1306_WHITE);
    oled.setTextColor(SSD1306_BLACK);
  } else oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(x, y); oled.print(text);
  oled.setTextColor(SSD1306_WHITE);
}

void oledHeader(const __FlashStringHelper *title) {
  oled.fillRoundRect(0, 0, 128, 11, 3, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  oled.setTextSize(1);
  oled.setCursor(4, 2); oled.print(title);
  oled.setCursor(108, 2); oled.printf("%u/4", currentPage + 1U);
  oled.setTextColor(SSD1306_WHITE);
}

void drawOledPortal() {
  oledHeader(F("WIFI SETUP"));
  if (wifiProvisionPhase == WIFI_PROVISION_TESTING) {
    oledCenter(F("TESTING NEW WIFI"), 17, 1, true);
    oledCenter(candidateWifiSsid, 31, 1);
    oledCenter(String((millis() - wifiProvisionStartedMs) / 1000UL) + F("s"), 44, 2);
    oled.setTextSize(1); oled.setCursor(11, 57); oled.print(F("OLD WIFI IS SAFE"));
    return;
  }
  if (wifiProvisionPhase == WIFI_PROVISION_SUCCESS) {
    oledCenter(F("CONNECTED"), 17, 1, true);
    oledCenter(activeWifiSsid, 31, 1);
    oledCenter(WiFi.localIP().toString(), 44, 1);
    oled.setTextSize(1); oled.setCursor(18, 57); oled.print(F("SAVING COMPLETE"));
    return;
  }
  if (wifiProvisionPhase == WIFI_PROVISION_FAILED) {
    oledCenter(F("CONNECT FAILED"), 17, 1, true);
    oledCenter(F("OLD WIFI KEPT"), 31, 1);
    oledCenter(F("OPEN 192.168.4.1"), 44, 1);
    oled.setTextSize(1); oled.setCursor(24, 57); oled.print(F("CHECK PASSWORD"));
    return;
  }
  oledCenter(portalSsid, 17, 1);
  oledCenter(F("PASS 12345678"), 30, 1);
  oledCenter(F("192.168.4.1"), 43, 1, true);
  oled.setTextSize(1); oled.setCursor(35, 56); oled.printf("CLIENTS %u", WiFi.softAPgetStationNum());
}

void drawOledHold() {
  oledHeader(F("CHANGE WIFI"));
  oledCenter(F("KEEP HOLDING"), 17, 1);
  const uint32_t elapsed = millis() - dualHoldStartedMs;
  const uint32_t remain = elapsed >= BUTTON_DUAL_WIFI_HOLD_MS ? 0 :
                          (BUTTON_DUAL_WIFI_HOLD_MS - elapsed + 999UL) / 1000UL;
  oledCenter(String(remain), 27, 3);
  oled.drawRoundRect(8, 54, 112, 8, 3, SSD1306_WHITE);
  oled.fillRoundRect(10, 56, (108 * wifiHoldProgressPct) / 100, 4, 2, SSD1306_WHITE);
}

void drawOledSummary() {
  oledHeader(F("DEEP WORK"));
  String state = data.state == STATE_GOOD ? F("READY") :
                 data.state == STATE_CAUTION ? F("ADJUST") :
                 data.state == STATE_POOR ? F("POOR") :
                 data.state == STATE_WARMUP ? F("WARMUP") : F("CHECK");
  oledCenter(state, 15, 1, true);
  oledCenter(String(data.scoreValid ? data.experimentalScore : data.thermalScore), 28, 3);
  oled.setTextSize(1);
  oled.setCursor(0, 56);
  if (focusSessionActive) oled.print(String(F("FOCUS ")) + formatDuration(focusElapsedSec()));
  else oled.print(F("ACTION: START FOCUS"));
}

void drawOledThermal() {
  oledHeader(F("THERMAL"));
  if (!data.dhtOk) { oledCenter(F("DHT11 OFFLINE"), 28, 1, true); return; }
  oled.setTextSize(2); oled.setCursor(0, 20); oled.printf("%.1fC", data.temperatureC);
  oled.setCursor(72, 20); oled.printf("%.0f%%", data.humidityPct);
  oled.drawFastVLine(65, 17, 28, SSD1306_WHITE);
  oled.setTextSize(1); oled.setCursor(0, 53); oled.printf("FEELS %.1fC", data.heatIndexC);
}

void drawOledGas() {
  oledHeader(F("AIR TREND"));
  if (!data.mqElectricalOk) { oledCenter(F("CHECK MQ135 AO"), 28, 1, true); return; }
  if (!data.mqWarm) {
    const uint8_t pct = static_cast<uint8_t>((min(millis(), MQ_OPERATIONAL_WARMUP_MS) * 100UL) / MQ_OPERATIONAL_WARMUP_MS);
    oledCenter(F("WARMUP"), 17, 2);
    oled.drawRoundRect(8, 48, 112, 10, 3, SSD1306_WHITE);
    oled.fillRoundRect(10, 50, (108 * pct) / 100, 6, 2, SSD1306_WHITE);
    return;
  }
  if (!data.mqReady) { oledCenter(F("CALIBRATING"), 18, 1, true); oledCenter(String(mqBaselineSamples)+"/"+String(MQ_BASELINE_SAMPLE_COUNT), 36, 2); return; }
  oledCenter(String(data.gasRatio, 2) + F("x"), 20, 3);
  oledCenter(String(data.gasChangePct, 1) + F("% VS BASE"), 54, 1);
}

void drawOledSystem() {
  oledHeader(F("SYSTEM"));
  oled.setTextSize(1);
  oled.setCursor(0, 17); oled.print(F("WIFI  ")); oled.print(WiFi.status() == WL_CONNECTED ? F("ONLINE") : F("OFFLINE"));
  oled.setCursor(0, 29); oled.print(F("CLOUD ")); oled.print(mqttClient.connected() ? F("ONLINE") : F("WAIT"));
  oled.setCursor(0, 41); oled.print(F("PAGE / FOCUS"));
  oled.setCursor(0, 53); oled.print(F("BOTH 5s: WIFI SETUP"));
}

void drawOled() {
  if (!FEATURE_OLED || !oledOk) return;
  oled.clearDisplay();
  if (portalActive) drawOledPortal();
  else if (dualHoldActive) drawOledHold();
  else {
    switch (currentPage) {
      case 0: drawOledSummary(); break;
      case 1: drawOledThermal(); break;
      case 2: drawOledGas(); break;
      default: drawOledSystem(); break;
    }
    if (uiToast.length() && millis() < uiToastUntilMs) {
      oled.fillRoundRect(4, 48, 120, 15, 4, SSD1306_WHITE);
      oled.setTextColor(SSD1306_BLACK); oled.setTextSize(1);
      int16_t x1,y1; uint16_t w,h; oled.getTextBounds(uiToast,0,0,&x1,&y1,&w,&h);
      oled.setCursor(max((int16_t)6, (int16_t)((128-static_cast<int16_t>(w))/2)),52); oled.print(uiToast);
      oled.setTextColor(SSD1306_WHITE);
    }
  }
  oled.display();
}

// =============================================================================
// 12) TFT UI — modern hierarchy, stable hardware-SPI canvas
// =============================================================================

static constexpr uint16_t UI_BG      = 0x0841; // deep navy
static constexpr uint16_t UI_PANEL   = 0x10E3;
static constexpr uint16_t UI_PANEL_2 = 0x1945;
static constexpr uint16_t UI_MUTED   = 0x7BEF;
static constexpr uint16_t UI_CYAN    = 0x05FF;
static constexpr uint16_t UI_MINT    = 0x5FEA;
static constexpr uint16_t UI_AMBER   = 0xFD20;
static constexpr uint16_t UI_SOFTRED = 0xF986;

void tftCenteredText(const String &text, int16_t y, uint8_t size, uint16_t color) {
  tft.setTextSize(size); tft.setTextColor(color); tft.setTextWrap(false);
  int16_t x1,y1; uint16_t w,h; tft.getTextBounds(text,0,y,&x1,&y1,&w,&h);
  tft.setCursor(max((int16_t)0, (int16_t)((TFT_WIDTH-static_cast<int16_t>(w))/2)),y); tft.print(text);
}

void canvasCenteredText(const String &text, int16_t y, uint8_t size, uint16_t color) {
  tftCanvas.setTextSize(size); tftCanvas.setTextColor(color); tftCanvas.setTextWrap(false);
  int16_t x1,y1; uint16_t w,h; tftCanvas.getTextBounds(text,0,y,&x1,&y1,&w,&h);
  tftCanvas.setCursor(max((int16_t)0, (int16_t)((tftCanvas.width()-static_cast<int16_t>(w))/2)),y); tftCanvas.print(text);
}

void drawCanvasPill(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint16_t fill, const String &text, uint16_t color) {
  tftCanvas.fillRoundRect(x,y,w,h,h/2,fill);
  tftCanvas.setTextSize(1); tftCanvas.setTextColor(color);
  int16_t x1,y1; uint16_t tw,th; tftCanvas.getTextBounds(text,0,0,&x1,&y1,&tw,&th);
  tftCanvas.setCursor(x+(w-static_cast<int16_t>(tw))/2, y+(h-static_cast<int16_t>(th))/2); tftCanvas.print(text);
}

void initTft() {
  if (!FEATURE_TFT) return;
  Serial.printf("[TFT] Init HW-SPI RST=%u CLK=%u MOSI=%u CS=%u DC=%u @ %lu Hz\n",
                PIN_TFT_RST, PIN_TFT_SCLK, PIN_TFT_MOSI, PIN_TFT_CS, PIN_TFT_DC,
                static_cast<unsigned long>(TFT_SPI_HZ));
  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_RST, HIGH); delay(10);
  digitalWrite(PIN_TFT_RST, LOW); delay(30);
  digitalWrite(PIN_TFT_RST, HIGH); delay(120);
  tftSpi.begin(PIN_TFT_SCLK,-1,PIN_TFT_MOSI,PIN_TFT_CS);
  tft.begin(TFT_SPI_HZ);
  tft.setRotation(TFT_ROTATION);
  tft.invertDisplay(TFT_INVERT_COLORS);
  tft.setTextWrap(false);
  tftInitialized = true;
  tft.fillScreen(UI_BG);
  tft.fillCircle(120,120,112,UI_PANEL);
  tft.fillCircle(120,120,106,UI_BG);
  tftCenteredText(F("HP14"),78,4,UI_CYAN);
  tftCenteredText(F("DEEP WORK MONITOR"),128,1,GC9A01A_WHITE);
  tftCenteredText(F("MODERN UX"),150,1,UI_MUTED);
  delay(350);
  lastTftPage=255;
  Serial.println(F("[TFT] Modern HW-SPI UI ready; controller reset occurs only at boot."));
}

String pageTitle() {
  switch(currentPage){case 0:return F("DEEP WORK");case 1:return F("THERMAL");case 2:return F("AIR TREND");default:return F("SYSTEM");}
}

void drawTftFrame() {
  tft.fillScreen(UI_BG);
  tft.drawCircle(120,120,118,UI_PANEL_2);
  tft.drawCircle(120,120,116,UI_PANEL);
  tft.setTextSize(1); tft.setTextColor(UI_MUTED); tft.setCursor(50,18); tft.print(F("HP14 / SPACE QUALITY"));
  const String title = portalActive ? String(F("WIFI SETUP")) :
                       dualHoldActive ? String(F("CHANGE WIFI")) : pageTitle();
  tftCenteredText(title,36,2,GC9A01A_WHITE);
  tft.fillRoundRect(58,59,124,3,2,portalActive ? UI_MINT : dualHoldActive ? UI_AMBER : UI_CYAN);
  if (!portalActive && !dualHoldActive) {
    for(uint8_t i=0;i<PAGE_COUNT;++i){int16_t x=102+i*12; if(i==currentPage)tft.fillCircle(x,224,3,UI_CYAN); else tft.fillCircle(x,224,2,UI_PANEL_2);}
  }
}

void renderCanvasSummary() {
  const uint16_t color = stateColor(data.state);
  const String state = data.state==STATE_GOOD?F("READY"):data.state==STATE_CAUTION?F("ADJUST"):data.state==STATE_POOR?F("POOR"):data.state==STATE_WARMUP?F("WARMUP"):F("CHECK");
  drawCanvasPill(52,2,96,20,UI_PANEL_2,state,color);
  const int score=data.scoreValid?data.experimentalScore:data.thermalScore;
  canvasCenteredText(String(score),27,6,color);
  canvasCenteredText(F("DEEP WORK SCORE"),84,1,UI_MUTED);
  tftCanvas.fillRoundRect(7,105,88,34,10,UI_PANEL);
  tftCanvas.fillRoundRect(105,105,88,34,10,UI_PANEL);
  tftCanvas.setTextColor(UI_MUTED); tftCanvas.setTextSize(1);
  tftCanvas.setCursor(17,112); tftCanvas.print(F("TEMP"));
  tftCanvas.setCursor(115,112); tftCanvas.print(F("HUMIDITY"));
  tftCanvas.setTextColor(GC9A01A_WHITE); tftCanvas.setTextSize(2);
  tftCanvas.setCursor(16,123); if(data.dhtOk)tftCanvas.printf("%.1fC",data.temperatureC);else tftCanvas.print(F("--"));
  tftCanvas.setCursor(124,123); if(data.dhtOk)tftCanvas.printf("%.0f%%",data.humidityPct);else tftCanvas.print(F("--"));
  if(focusSessionActive) drawCanvasPill(45,88,110,15,0x0320,String(F("FOCUS "))+formatDuration(focusElapsedSec()),UI_MINT);
  else drawCanvasPill(42,88,116,15,UI_PANEL_2,F("ACTION: START FOCUS"),UI_CYAN);
}

void renderCanvasThermal() {
  if(!data.dhtOk){canvasCenteredText(F("DHT11 OFFLINE"),48,2,UI_SOFTRED);canvasCenteredText(F("CHECK GPIO3 / 3V3 / GND"),82,1,UI_MUTED);return;}
  tftCanvas.setTextColor(UI_CYAN); tftCanvas.setTextSize(5); tftCanvas.setCursor(26,8); tftCanvas.printf("%.1f",data.temperatureC);
  tftCanvas.setTextColor(UI_MUTED); tftCanvas.setTextSize(2); tftCanvas.setCursor(151,28); tftCanvas.print(F("C"));
  tftCanvas.setTextSize(1); tftCanvas.setCursor(64,58); tftCanvas.print(F("AIR TEMPERATURE"));
  tftCanvas.fillRoundRect(8,79,88,53,12,UI_PANEL); tftCanvas.fillRoundRect(104,79,88,53,12,UI_PANEL);
  tftCanvas.setTextSize(1); tftCanvas.setTextColor(UI_MUTED); tftCanvas.setCursor(20,89); tftCanvas.print(F("HUMIDITY")); tftCanvas.setCursor(116,89); tftCanvas.print(F("FEELS LIKE"));
  tftCanvas.setTextSize(2); tftCanvas.setTextColor(GC9A01A_WHITE); tftCanvas.setCursor(27,108); tftCanvas.printf("%.0f%%",data.humidityPct);
  const uint16_t hiColor=data.heatIndexC>=HEAT_INDEX_POOR_C?UI_SOFTRED:data.heatIndexC>=HEAT_INDEX_CAUTION_C?UI_AMBER:UI_MINT;
  tftCanvas.setTextColor(hiColor); tftCanvas.setCursor(116,108); tftCanvas.printf("%.1fC",data.heatIndexC);
}

void drawGasSparklineModern() {
  const int16_t x0=10,y0=100,w=180,h=38;
  tftCanvas.fillRoundRect(x0,y0,w,h,9,UI_PANEL);
  tftCanvas.drawFastHLine(x0+8,y0+h/2,w-16,UI_PANEL_2);
  if(gasHistoryUsed<2)return;
  float minV=999,maxV=-999;
  for(uint8_t i=0;i<gasHistoryUsed;++i){uint8_t idx=(gasHistoryHead+GAS_HISTORY_COUNT-gasHistoryUsed+i)%GAS_HISTORY_COUNT;float v=gasRatioHistory[idx];if(isfinite(v)){minV=min(minV,v);maxV=max(maxV,v);}}
  minV=min(minV,0.95f);maxV=max(maxV,1.10f);if(maxV-minV<0.05f)maxV=minV+0.05f;
  int16_t px=-1,py=-1;
  for(uint8_t i=0;i<gasHistoryUsed;++i){uint8_t idx=(gasHistoryHead+GAS_HISTORY_COUNT-gasHistoryUsed+i)%GAS_HISTORY_COUNT;float v=gasRatioHistory[idx];if(!isfinite(v))continue;int16_t x=x0+8+(i*(w-16))/max(1, (int)gasHistoryUsed-1);int16_t y=y0+h-7-static_cast<int16_t>((v-minV)*(h-14)/(maxV-minV));if(px>=0)tftCanvas.drawLine(px,py,x,y,UI_CYAN);tftCanvas.fillCircle(x,y,1,UI_CYAN);px=x;py=y;}
}

void renderCanvasGas() {
  if(!data.mqElectricalOk){canvasCenteredText(F("MQ135 SIGNAL ERROR"),42,2,UI_SOFTRED);canvasCenteredText(F("CHECK AO / DIVIDER / GPIO2"),79,1,UI_MUTED);return;}
  if(!data.mqWarm){const uint32_t remain=(MQ_OPERATIONAL_WARMUP_MS-min(millis(),MQ_OPERATIONAL_WARMUP_MS)+59999UL)/60000UL;drawCanvasPill(53,4,94,20,UI_PANEL_2,F("SENSOR WARMUP"),UI_AMBER);canvasCenteredText(String(remain)+F(" MIN"),38,4,UI_AMBER);canvasCenteredText(String(F("AO "))+String(data.mqFilteredMv,0)+F(" mV"),82,1,UI_MUTED);return;}
  if(!data.mqReady){drawCanvasPill(48,4,104,20,UI_PANEL_2,F("CALIBRATING"),UI_AMBER);canvasCenteredText(String(mqBaselineSamples)+F(" / ")+String(MQ_BASELINE_SAMPLE_COUNT),42,3,GC9A01A_WHITE);canvasCenteredText(F("KEEP AIR STABLE"),80,1,UI_MUTED);return;}
  const uint16_t color=data.gasRatio<=GAS_RATIO_GOOD_MAX?UI_MINT:data.gasRatio<=GAS_RATIO_CAUTION_MAX?UI_AMBER:UI_SOFTRED;
  drawCanvasPill(63,2,74,18,UI_PANEL_2,F("RELATIVE AIR"),UI_MUTED);
  canvasCenteredText(String(data.gasRatio,2)+F("x"),25,5,color);
  canvasCenteredText(String(data.gasChangePct,1)+F("% VS BASELINE"),77,1,GC9A01A_WHITE);
  drawGasSparklineModern();
}

void renderCanvasSystem() {
  const bool wifiOk=WiFi.status()==WL_CONNECTED, cloudOk=mqttClient.connected();
  tftCanvas.fillRoundRect(8,5,88,52,12,UI_PANEL); tftCanvas.fillRoundRect(104,5,88,52,12,UI_PANEL);
  tftCanvas.fillRoundRect(8,65,184,42,12,UI_PANEL); tftCanvas.fillRoundRect(8,115,184,25,10,UI_PANEL_2);
  tftCanvas.setTextSize(1); tftCanvas.setTextColor(UI_MUTED); tftCanvas.setCursor(20,15); tftCanvas.print(F("WIFI")); tftCanvas.setCursor(116,15); tftCanvas.print(F("CLOUD"));
  tftCanvas.setTextSize(2); tftCanvas.setTextColor(wifiOk?UI_MINT:UI_SOFTRED); tftCanvas.setCursor(20,34); tftCanvas.print(wifiOk?F("ONLINE"):F("OFF"));
  tftCanvas.setTextColor(cloudOk?UI_MINT:UI_AMBER); tftCanvas.setCursor(116,34); tftCanvas.print(cloudOk?F("ONLINE"):F("WAIT"));
  tftCanvas.setTextSize(1); tftCanvas.setTextColor(UI_MUTED); tftCanvas.setCursor(20,75); tftCanvas.print(F("FOCUS SESSION"));
  tftCanvas.setTextSize(2); tftCanvas.setTextColor(focusSessionActive?UI_CYAN:GC9A01A_WHITE); tftCanvas.setCursor(20,89); tftCanvas.print(focusSessionActive?formatDuration(focusElapsedSec()):F("READY"));
  tftCanvas.setTextSize(1); tftCanvas.setTextColor(UI_CYAN); tftCanvas.setCursor(22,123); tftCanvas.print(F("HOLD BOTH 5s  ·  CHANGE WIFI"));
}

void renderCanvasWifiHold() {
  tftCanvas.fillScreen(UI_BG);
  drawCanvasPill(46,4,108,20,UI_PANEL_2,F("CHANGE WIFI"),UI_AMBER);
  const uint32_t elapsed=millis()-dualHoldStartedMs;
  const uint32_t remain=elapsed>=BUTTON_DUAL_WIFI_HOLD_MS?0:(BUTTON_DUAL_WIFI_HOLD_MS-elapsed+999UL)/1000UL;
  canvasCenteredText(String(remain),34,6,GC9A01A_WHITE);
  canvasCenteredText(F("KEEP BOTH BUTTONS PRESSED"),91,1,UI_MUTED);
  tftCanvas.drawRoundRect(18,119,164,14,7,UI_PANEL_2);
  tftCanvas.fillRoundRect(21,122,(158*wifiHoldProgressPct)/100,8,4,UI_AMBER);
}

void renderCanvasPortal() {
  tftCanvas.fillScreen(UI_BG);
  drawCanvasPill(45,2,110,20,0x0320,F("WIFI SETUP"),UI_MINT);

  if (wifiProvisionPhase == WIFI_PROVISION_TESTING) {
    canvasCenteredText(F("TESTING"),31,3,GC9A01A_WHITE);
    canvasCenteredText(candidateWifiSsid,68,1,UI_CYAN);
    canvasCenteredText(String((millis()-wifiProvisionStartedMs)/1000UL)+F(" sec"),88,2,UI_AMBER);
    canvasCenteredText(F("OLD WIFI KEPT SAFE"),126,1,UI_MUTED);
    return;
  }
  if (wifiProvisionPhase == WIFI_PROVISION_SUCCESS) {
    canvasCenteredText(F("CONNECTED"),31,3,UI_MINT);
    canvasCenteredText(activeWifiSsid,69,1,GC9A01A_WHITE);
    tftCanvas.fillRoundRect(28,87,144,34,10,UI_CYAN);
    canvasCenteredText(WiFi.localIP().toString(),96,2,UI_BG);
    canvasCenteredText(F("SETUP COMPLETE"),130,1,UI_MUTED);
    return;
  }
  if (wifiProvisionPhase == WIFI_PROVISION_FAILED) {
    canvasCenteredText(F("TRY AGAIN"),31,3,UI_AMBER);
    canvasCenteredText(F("OLD WIFI WAS NOT ERASED"),72,1,UI_MUTED);
    tftCanvas.fillRoundRect(28,91,144,30,10,UI_CYAN);
    canvasCenteredText(F("192.168.4.1"),98,2,UI_BG);
    canvasCenteredText(F("CHECK PASSWORD / 2.4GHz"),132,1,UI_MUTED);
    return;
  }

  canvasCenteredText(portalSsid,33,2,GC9A01A_WHITE);
  canvasCenteredText(F("PASS 12345678"),66,1,UI_MUTED);
  tftCanvas.fillRoundRect(28,87,144,34,10,UI_CYAN);
  canvasCenteredText(F("192.168.4.1"),96,2,UI_BG);
  canvasCenteredText(String(F("CLIENTS "))+String(WiFi.softAPgetStationNum()),130,1,UI_MUTED);
}

void renderTftDynamicCanvas() {
  if(!tftCanvas.getBuffer())return;
  tftCanvas.fillScreen(UI_BG);
  if(portalActive) renderCanvasPortal();
  else if(dualHoldActive) renderCanvasWifiHold();
  else {switch(currentPage){case 0:renderCanvasSummary();break;case 1:renderCanvasThermal();break;case 2:renderCanvasGas();break;default:renderCanvasSystem();break;}}
  if(!portalActive && !dualHoldActive && uiToast.length() && millis()<uiToastUntilMs){tftCanvas.fillRoundRect(25,121,150,22,11,UI_CYAN);tftCanvas.setTextColor(UI_BG);tftCanvas.setTextSize(1);int16_t x1,y1;uint16_t w,h;tftCanvas.getTextBounds(uiToast,0,0,&x1,&y1,&w,&h);tftCanvas.setCursor(max((int16_t)28, (int16_t)((200-static_cast<int16_t>(w))/2)),128);tftCanvas.print(uiToast);}
  tft.drawRGBBitmap(20,66,tftCanvas.getBuffer(),200,146);
}

void drawTft() {
  if(!FEATURE_TFT||!tftInitialized)return;
  const uint8_t visualPage=portalActive?250:(dualHoldActive?251:currentPage);
  if(visualPage!=lastTftPage){drawTftFrame();lastTftPage=visualPage;}
  renderTftDynamicCanvas();
}

// =============================================================================
// 13) Button actions — PAGE, FOCUS, and dual-hold Wi-Fi setup
// =============================================================================

void goToNextPage(const char *source) {
  currentPage=(currentPage+1U)%PAGE_COUNT;
  lastTftPage=255; uiDirty=true; lastUserActionMs=millis();
  flashInteraction(0,20,20,180);
  if(BEEP_ON_BUTTON_ACTION)startBeep(BEEP_PAGE);
  showToast(String(F("PAGE "))+String(currentPage+1U)+F(" / 4"),900);
  Serial.printf("[BTN] %s -> page %u/%u\n",source,currentPage+1U,PAGE_COUNT);
}

void toggleFocusSession(const char *source) {
  const uint32_t completedSec = focusSessionActive ? focusElapsedSec() : 0UL;
  focusSessionActive=!focusSessionActive;
  if(focusSessionActive){focusSessionStartedMs=millis();++focusSessionCount;if(BEEP_ON_BUTTON_ACTION)startBeep(BEEP_FOCUS_START);showToast(F("FOCUS SESSION START"),1600);}
  else{if(BEEP_ON_BUTTON_ACTION)startBeep(BEEP_FOCUS_STOP);showToast(String(F("FOCUS DONE · "))+formatDuration(completedSec),1800);focusSessionStartedMs=0;}
  uiDirty=true;lastUserActionMs=millis();
  flashInteraction(focusSessionActive?0:18,focusSessionActive?22:8,focusSessionActive?20:0,260);
  Serial.printf("[BTN] %s -> focus %s count=%lu\n",source,focusSessionActive?"START":"STOP",static_cast<unsigned long>(focusSessionCount));
}

void beginDualHold(uint32_t now) {
  dualHoldActive=true;dualHoldTriggered=false;dualHoldStartedMs=now;dualHoldLastSecond=255;wifiHoldProgressPct=0;
  pageActionPending=false;focusActionPending=false;stopBuzzer();uiDirty=true;lastTftPage=255;
  Serial.println(F("[BTN] Dual hold detected; keep holding 5s for WiFi setup."));
}

void serviceButtons(uint32_t now) {
  menuButton.update(now);ackButton.update(now);
  const bool pagePressedEdge=menuButton.consumePressedEdge();
  const bool focusPressedEdge=ackButton.consumePressedEdge();
  const bool pageReleasedEdge=menuButton.consumeReleasedEdge();
  const bool focusReleasedEdge=ackButton.consumeReleasedEdge();

  if(pagePressedEdge){pageActionPending=true;pagePendingSinceMs=now;}
  if(focusPressedEdge){focusActionPending=true;focusPendingSinceMs=now;}

  if(menuButton.isPressed()&&ackButton.isPressed()){
    if(!dualHoldActive)beginDualHold(now);
    const uint32_t elapsed=now-dualHoldStartedMs;
    wifiHoldProgressPct=static_cast<uint8_t>(min((uint32_t)100UL, (uint32_t)((elapsed*100UL)/BUTTON_DUAL_WIFI_HOLD_MS)));
    const uint8_t sec=static_cast<uint8_t>(elapsed/1000UL);
    if(sec!=dualHoldLastSecond){dualHoldLastSecond=sec;uiDirty=true;}
    if(!dualHoldTriggered&&elapsed>=BUTTON_DUAL_WIFI_HOLD_MS){dualHoldTriggered=true;wifiHoldProgressPct=100;openWifiSetupPortal();}
    return;
  }

  if(dualHoldActive){
    // Sau một chord, chờ cả hai nút nhả hẳn; không phát sinh PAGE/FOCUS ngoài ý muốn.
    if(!menuButton.isPressed()&&!ackButton.isPressed()){dualHoldActive=false;dualHoldTriggered=false;wifiHoldProgressPct=0;lastTftPage=255;uiDirty=true;}
    return;
  }

  // Quick tap released before the chord window still counts as one action.
  if(pageReleasedEdge&&pageActionPending){
    if((now-pagePendingSinceMs)>=BUTTON_MIN_VALID_PRESS_MS){goToNextPage("PAGE");}
    pageActionPending=false;
  }
  if(focusReleasedEdge&&focusActionPending){
    if((now-focusPendingSinceMs)>=BUTTON_MIN_VALID_PRESS_MS){toggleFocusSession("FOCUS");}
    focusActionPending=false;
  }

  // Normal hold fires once after the short chord window, without waiting for release.
  if(pageActionPending&&menuButton.isPressed()&&!ackButton.isPressed()&&
     (now-pagePendingSinceMs)>=BUTTON_CHORD_WINDOW_MS){goToNextPage("PAGE");pageActionPending=false;}
  if(focusActionPending&&ackButton.isPressed()&&!menuButton.isPressed()&&
     (now-focusPendingSinceMs)>=BUTTON_CHORD_WINDOW_MS){toggleFocusSession("FOCUS");focusActionPending=false;}

  if(FEATURE_BUTTON_DIAGNOSTICS&&(now-lastButtonDiagnosticMs)>=12000UL){lastButtonDiagnosticMs=now;Serial.printf("[BTN] raw PAGE=%d FOCUS=%d page=%u focus=%d\n",menuButton.rawLevel(),ackButton.rawLevel(),currentPage+1U,focusSessionActive?1:0);}
}

void serviceAlertReminder(uint32_t now){(void)now;/* Environment alerts intentionally silent. */}

// =============================================================================
// 15) Serial diagnostic console
// =============================================================================

void runTftColorTest(){if(!FEATURE_TFT||!tftInitialized)return;Serial.println(F("[TEST] TFT colors -> UI"));tft.fillScreen(GC9A01A_RED);delay(180);tft.fillScreen(GC9A01A_GREEN);delay(180);tft.fillScreen(GC9A01A_BLUE);delay(180);lastTftPage=255;uiDirty=true;}
void printDiagnosticHelp(){Serial.println(F("[CMD] n=PAGE, a=FOCUS, b=BUZZER, l=LED, t=TFT, c=MQ baseline, w=WIFI portal, v=WIFI vault, p=pins"));}

void serviceSerialConsole(){
  while(Serial.available()>0){char command=static_cast<char>(Serial.read());if(command>='A'&&command<='Z')command=command-'A'+'a';
    switch(command){
      case 'n':case 'm':goToNextPage("serial PAGE");break;
      case 'a':toggleFocusSession("serial FOCUS");break;
      case 'b':startBeep(BEEP_TEST);Serial.printf("[CMD] Buzzer pulse %ums polarity=%s.\n",BUZZER_TEST_DURATION_MS,BUZZER_ACTIVE_HIGH?"ACTIVE-HIGH":"ACTIVE-LOW");break;
      case 'l':startupSelfTest();uiDirty=true;break;
      case 't':runTftColorTest();break;
      case 'c':if(data.mqWarm&&data.mqElectricalOk){data.mqBaselineMv=0;preferences.remove("mqbase");startMqCalibration("serial command");currentPage=2;uiDirty=true;}else Serial.println(F("[CMD] MQ calibration blocked until warm-up."));break;
      case 'w':startSetupPortal(F("serial-command"));break;
      case 'v':printWifiVault();break;
      case 'p':Serial.println(F("[PINMAP] TFT RST=0 PAGE=1 MQ=2 DHT=3 OLED=6/7 RGB=8"));Serial.println(F("[PINMAP] RED=10 FOCUS=11 BUZZER=15 TFT=18/19/20/21 GREEN=22 YELLOW=23"));break;
      case '\r':case '\n':case ' ':break;
      default:printDiagnosticHelp();break;
    }
  }
}

// =============================================================================
// 16) Setup and loop
// =============================================================================

void setup(){
  // Observed hardware is active-low: HIGH is the silent idle level.
  const uint8_t buzzerOffLevel=BUZZER_ACTIVE_HIGH?LOW:HIGH;
  digitalWrite(PIN_BUZZER,buzzerOffLevel);pinMode(PIN_BUZZER,OUTPUT);digitalWrite(PIN_BUZZER,buzzerOffLevel);buzzer.outputOn=false;

  Serial.begin(115200);delay(250);Serial.println();Serial.println(F("========================================"));
  Serial.printf("%s %s starting\n",HP14_FIRMWARE_NAME,HP14_FIRMWARE_VERSION);Serial.println(F("========================================"));
  printDiagnosticHelp();
  Serial.println(F("[PINMAP] LOCKED: TFT RST=0 PAGE=1 MQ=2 DHT=3 OLED=6/7 RGB=8"));
  Serial.println(F("[PINMAP] RED=10 FOCUS=11 BUZZER=15 TFT=18/19/20/21 GREEN=22 YELLOW=23"));
  Serial.printf("[BUZZER] Polarity=%s; hard ON limit=%ums; environment alerts OFF.\n",BUZZER_ACTIVE_HIGH?"ACTIVE-HIGH":"ACTIVE-LOW",BUZZER_HARD_MAX_ON_MS);

  if(FEATURE_EXTERNAL_LEDS){pinMode(PIN_LED_GREEN,OUTPUT);pinMode(PIN_LED_YELLOW,OUTPUT);pinMode(PIN_LED_RED,OUTPUT);setExternalLeds(false,false,false);}
  menuButton.begin();ackButton.begin();
  Serial.printf("[BTN] Boot PAGE(GPIO%u)=%d FOCUS(GPIO%u)=%d; both 5s opens WiFi setup; vault keeps 5 newest successful networks.\n",PIN_BTN_MENU,menuButton.rawLevel(),PIN_BTN_ACK,ackButton.rawLevel());

  if(FEATURE_BOARD_RGB){boardRgb.begin();boardRgb.setBrightness(HP14_RGB_LED_BRIGHTNESS);setBoardRgb(8,0,8);}
  if(FEATURE_BUZZER&&!BUZZER_IS_ACTIVE){buzzer.pwmAttached=ledcAttach(PIN_BUZZER,PASSIVE_BUZZER_FREQUENCY_HZ,8);ledcWriteTone(PIN_BUZZER,0);}
  startupSelfTest();

  analogReadResolution(12);analogSetPinAttenuation(PIN_MQ135_ADC,ADC_11db);
  preferences.begin("hp14",false);
  loadNetworkSettings();
  const uint16_t storedSchema=preferences.getUShort("mqschema",0);
  if(storedSchema!=MQ_BASELINE_SCHEMA_VERSION){preferences.remove("mqbase");preferences.putUShort("mqschema",MQ_BASELINE_SCHEMA_VERSION);data.mqBaselineMv=0;}
  else data.mqBaselineMv=preferences.getFloat("mqbase",0);
  if(data.mqBaselineMv>=50&&data.mqBaselineMv<=5000)Serial.printf("[MQ] Stored baseline loaded: %.1f mV\n",data.mqBaselineMv);else{data.mqBaselineMv=0;Serial.println(F("[MQ] No valid baseline; learning starts after warm-up."));}

  pinMode(PIN_DHT11,INPUT_PULLUP);dht.begin();
  if(FEATURE_OLED){Wire.begin(PIN_OLED_SDA,PIN_OLED_SCL);oledOk=initOledAtAddress(OLED_ADDRESS_PRIMARY);if(!oledOk)oledOk=initOledAtAddress(OLED_ADDRESS_SECONDARY);Serial.printf("[OLED] %s",oledOk?"Ready":"Not detected");if(oledOk)Serial.printf(" at 0x%02X",oledAddress);Serial.println();}
  initTft();

  WiFi.setAutoReconnect(false);
  mqttClient.setServer(activeTbHost.c_str(),activeTbPort);
  mqttClient.setKeepAlive(MQTT_KEEP_ALIVE_SEC);mqttClient.setSocketTimeout(MQTT_SOCKET_TIMEOUT_SEC);mqttClient.setBufferSize(MQTT_BUFFER_SIZE);

  wifiBootWindowStartedMs = millis();
  if(!wifiCredentialsConfigured()) {
    startSetupPortal(F("no-wifi-credentials"));
  } else {
    beginWifiAttempt();
  }

  const uint32_t bootNow = millis();
  lastSensorMs=bootNow;lastAlertReminderMs=bootNow;data.state=STATE_BOOT;uiDirty=true;stopBuzzer();
  telemetryWindowStartedMs=bootNow;
  telemetryNextAttemptNotBeforeMs=bootNow+TELEMETRY_FIRST_SEND_DELAY_MS;
  Serial.printf("[TB] QUOTA-SAFE: compact=%u keys, interval=%lumin, hard cap=%u messages/hour.\n",
                TELEMETRY_KEYS_PER_MESSAGE,
                static_cast<unsigned long>(TELEMETRY_INTERVAL_MS/60000UL),
                TELEMETRY_MAX_MESSAGES_PER_WINDOW);
}

void loop(){
  const uint32_t now=millis();
  serviceSerialConsole();serviceBuzzer(now);serviceButtons(now);servicePortal();serviceWifiProvisioning(now);serviceWifi(now);serviceMqtt(now);serviceBuzzer(millis());

  if((now-lastSensorMs)>=SENSOR_INTERVAL_MS){lastSensorMs=now;sampleSensors();}
  serviceTelemetry(now);
  serviceAlertReminder(now);updateStatusLeds(now);

  if((now-lastUiServiceMs)>=UI_SERVICE_INTERVAL_MS){lastUiServiceMs=now;const bool tftPageChanged=(portalActive?250:(dualHoldActive?251:currentPage))!=lastTftPage;
    const uint32_t oledPeriod=dualHoldActive?UI_HOLD_REFRESH_MS:OLED_LIVE_REFRESH_MS;
    const uint32_t tftPeriod=dualHoldActive?UI_HOLD_REFRESH_MS:TFT_LIVE_REFRESH_MS;
    if(uiDirty||(now-lastOledRefreshMs)>=oledPeriod){drawOled();lastOledRefreshMs=now;}
    if(tftPageChanged||uiDirty||(now-lastTftRefreshMs)>=tftPeriod){drawTft();lastTftRefreshMs=now;}
    uiDirty=false;
  }
  delay(2);
}
