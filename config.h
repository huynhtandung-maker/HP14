#pragma once

#include <Arduino.h>

/*
  HP14 - CONFIGURATION
  ============================================================================
  v1.2.1 giữ nguyên toàn bộ GPIO đã khóa và bổ sung lớp bảo vệ hạn mức ThingsBoard.
  Chỉ chỉnh file này khi cần đổi cực tính còi, ngưỡng hoặc chu kỳ.
  Wi-Fi / ThingsBoard mặc định nằm trong secrets.h; người dùng có thể đổi Wi-Fi
  bằng portal HP14-SETUP sau khi giữ đồng thời hai nút 5 giây.
WiFi quét trước khi mở AP, dùng danh sách cache, lưu mạng mới ở vùng pending và duy trì kho 5 WiFi dùng gần nhất; chỉ commit sau khi kết nối thành công.
*/

// -----------------------------------------------------------------------------
// 1) Firmware
// -----------------------------------------------------------------------------
#define HP14_FIRMWARE_NAME    "HP14"
#define HP14_FIRMWARE_VERSION "1.2.1-quota-safe"

#include "secrets.h"

#ifndef WIFI_SSID
  #error "Thiếu WIFI_SSID trong secrets.h"
#endif
#ifndef WIFI_PASSWORD
  #error "Thiếu WIFI_PASSWORD trong secrets.h"
#endif
#ifndef TB_HOST
  #error "Thiếu TB_HOST trong secrets.h"
#endif
#ifndef TB_PORT
  #error "Thiếu TB_PORT trong secrets.h"
#endif
#ifndef TB_TOKEN
  #error "Thiếu TB_TOKEN trong secrets.h"
#endif

// -----------------------------------------------------------------------------
// 2) Feature switches
// -----------------------------------------------------------------------------
static constexpr bool FEATURE_OLED = true;
static constexpr bool FEATURE_TFT = true;
static constexpr bool FEATURE_EXTERNAL_LEDS = true;
static constexpr bool FEATURE_BOARD_RGB = true;
static constexpr bool FEATURE_BUZZER = true;
static constexpr bool FEATURE_BUTTON_DIAGNOSTICS = true;
static constexpr bool FEATURE_WIFI_PORTAL = true;
static constexpr bool TFT_BOOT_COLOR_TEST = false;

// -----------------------------------------------------------------------------
// 3) LOCKED GPIO MAP — KHÔNG ĐỔI DÂY
// -----------------------------------------------------------------------------
static constexpr uint8_t PIN_TFT_RST    = 0;
static constexpr uint8_t PIN_BTN_MENU   = 1;   // PAGE
static constexpr uint8_t PIN_MQ135_ADC  = 2;
static constexpr uint8_t PIN_DHT11      = 3;
static constexpr uint8_t PIN_OLED_SDA   = 6;
static constexpr uint8_t PIN_OLED_SCL   = 7;
static constexpr uint8_t PIN_RGB        = 8;
static constexpr uint8_t PIN_LED_RED    = 10;
static constexpr uint8_t PIN_BTN_ACK    = 11;  // FOCUS ACTION
static constexpr uint8_t PIN_BUZZER     = 15;
static constexpr uint8_t PIN_TFT_SCLK   = 18;
static constexpr uint8_t PIN_TFT_MOSI   = 19;
static constexpr uint8_t PIN_TFT_CS     = 20;
static constexpr uint8_t PIN_TFT_DC     = 21;
static constexpr uint8_t PIN_LED_GREEN  = 22;
static constexpr uint8_t PIN_LED_YELLOW = 23;

// -----------------------------------------------------------------------------
// 4) Buttons / chord UX
// -----------------------------------------------------------------------------
// Wiring: 3V3 -- 10k -- GPIO -- button -- GND. INPUT_PULLUP vẫn bật dự phòng.
static constexpr bool BUTTON_ACTIVE_LOW = true;
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 24UL;
static constexpr uint32_t BUTTON_RELEASE_DEBOUNCE_MS = 24UL;
static constexpr uint32_t BUTTON_MIN_VALID_PRESS_MS = 35UL;
// Chờ ngắn để phân biệt bấm đơn với giữ hai nút; vẫn cho cảm giác phản hồi nhanh.
static constexpr uint32_t BUTTON_CHORD_WINDOW_MS = 180UL;
static constexpr uint32_t BUTTON_DUAL_WIFI_HOLD_MS = 5000UL;

// -----------------------------------------------------------------------------
// 5) Displays
// -----------------------------------------------------------------------------
static constexpr uint8_t OLED_WIDTH = 128;
static constexpr uint8_t OLED_HEIGHT = 64;
static constexpr int8_t OLED_RESET = -1;
static constexpr uint8_t OLED_ADDRESS_PRIMARY = 0x3C;
static constexpr uint8_t OLED_ADDRESS_SECONDARY = 0x3D;

static constexpr int16_t TFT_WIDTH = 240;
static constexpr int16_t TFT_HEIGHT = 240;
static constexpr uint32_t TFT_SPI_HZ = 20000000UL;
static constexpr uint8_t TFT_ROTATION = 0;
static constexpr bool TFT_INVERT_COLORS = true;
static constexpr uint32_t OLED_LIVE_REFRESH_MS = 900UL;
static constexpr uint32_t TFT_LIVE_REFRESH_MS = 1200UL;
static constexpr uint32_t UI_HOLD_REFRESH_MS = 160UL;

// -----------------------------------------------------------------------------
// 5B) DHT11 stability
// -----------------------------------------------------------------------------
static constexpr uint32_t DHT_STALE_TIMEOUT_MS = 20000UL;
static constexpr uint8_t DHT_FAILURES_BEFORE_WARNING = 3;

// -----------------------------------------------------------------------------
// 6) LEDs and buzzer
// -----------------------------------------------------------------------------
static constexpr uint8_t HP14_RGB_LED_BRIGHTNESS = 32;

// Quan sát thực tế của hệ hiện tại: LOW làm còi kêu, HIGH làm còi im.
// Vì vậy v0.8.0 mặc định BUZZER_ACTIVE_HIGH=false (active-low).
// Nếu sau khi nạp còi hoạt động ngược, chỉ đổi duy nhất giá trị này thành true.
static constexpr bool BUZZER_IS_ACTIVE = true;
static constexpr bool BUZZER_ACTIVE_HIGH = false;
static constexpr uint16_t PASSIVE_BUZZER_FREQUENCY_HZ = 2300;
static constexpr uint16_t BUZZER_TEST_DURATION_MS = 90;
// Fail-safe: không cho một xung âm giữ lâu hơn giới hạn này dù logic bị lỗi.
static constexpr uint16_t BUZZER_HARD_MAX_ON_MS = 110;
static constexpr bool BEEP_ON_BUTTON_ACTION = true;
static constexpr bool AUDIBLE_ENVIRONMENT_ALERTS = false;

// -----------------------------------------------------------------------------
// 7) Wi-Fi setup portal inherited from HP13's proven flow
// -----------------------------------------------------------------------------
static constexpr char WIFI_SETUP_AP_PREFIX[] = "HP14-SETUP";
static constexpr char WIFI_SETUP_AP_PASSWORD[] = "12345678";
static constexpr uint8_t WIFI_SETUP_DNS_PORT = 53;
static constexpr uint16_t WIFI_SETUP_WEB_PORT = 80;
static constexpr uint8_t WIFI_SETUP_AP_CHANNEL = 6;
static constexpr uint8_t WIFI_SETUP_MAX_CLIENTS = 4;
static constexpr int WIFI_ACCEPTABLE_RSSI_DBM = -75;
static constexpr int WIFI_TOO_WEAK_RSSI_DBM = -83;
static constexpr uint32_t WIFI_PORTAL_SCAN_MS_PER_CHANNEL = 120UL;

// HP13-proven provisioning: save credentials, allow the browser response to
// finish, then restart once. No candidate test inside AP+STA and no forcePortal latch.
static constexpr uint32_t WIFI_SAVE_RESTART_DELAY_MS = 4500UL;
static constexpr uint32_t WIFI_CONNECT_ATTEMPT_TIMEOUT_MS = 30000UL;
static constexpr uint32_t WIFI_BOOT_RETRY_GAP_MS = 2500UL;
static constexpr uint8_t WIFI_BOOT_MAX_ATTEMPTS = 2;

// Kho WiFi MRU: luôn giữ tối đa 5 mạng đã KẾT NỐI THÀNH CÔNG gần nhất.
// Mạng vừa kết nối được đưa lên đầu; mạng thứ 6 sẽ thay mạng cũ nhất.
static constexpr uint8_t WIFI_SAVED_NETWORK_LIMIT = 5;
static constexpr uint8_t WIFI_VAULT_SCHEMA_VERSION = 1;
// Quét một lần trước khi kết nối để bỏ qua các mạng đã lưu nhưng không có mặt.
// Mạng ẩn vẫn có thể nhập qua portal; mặc định không chờ thử mạng ẩn để tránh boot lâu.
static constexpr bool WIFI_SCAN_SAVED_NETWORKS_ON_BOOT = true;
static constexpr bool WIFI_TRY_SAVED_HIDDEN_NETWORKS = false;
static constexpr uint8_t WIFI_SAVED_SCAN_MAX_ROUNDS = 2;  // Cho router thời gian khởi động lại sau mất điện.

// -----------------------------------------------------------------------------
// 8) Scheduling / network
// -----------------------------------------------------------------------------
static constexpr uint32_t SENSOR_INTERVAL_MS = 3000UL;
static constexpr uint32_t UI_SERVICE_INTERVAL_MS = 35UL;

// ThingsBoard QUOTA-SAFE policy
// - Local sensors/UI remain responsive; only the cloud upload is slowed down.
// - One compact JSON message contains 8 essential telemetry keys.
// - Default: one message every 10 minutes (about 34,560 data points/month/device).
// - Hard limiter: at most 8 telemetry messages in any one-hour window.
// - No immediate burst after MQTT reconnect and no rapid retry after publish failure.
static constexpr uint32_t TELEMETRY_INTERVAL_MS = 10UL * 60UL * 1000UL;
static constexpr uint32_t TELEMETRY_FIRST_SEND_DELAY_MS = 60UL * 1000UL;
static constexpr uint32_t TELEMETRY_RECONNECT_SETTLE_MS = 30UL * 1000UL;
static constexpr uint32_t TELEMETRY_MIN_GAP_MS = 60UL * 1000UL;
static constexpr uint32_t TELEMETRY_FAILURE_BACKOFF_MS = 15UL * 60UL * 1000UL;
static constexpr uint32_t TELEMETRY_RATE_WINDOW_MS = 60UL * 60UL * 1000UL;
static constexpr uint8_t TELEMETRY_MAX_MESSAGES_PER_WINDOW = 8;
static constexpr uint8_t TELEMETRY_KEYS_PER_MESSAGE = 8;

static constexpr uint32_t SERIAL_DATA_LOG_INTERVAL_MS = 15000UL;
static constexpr uint32_t WIFI_RETRY_MS = 20000UL;
// MQTT reconnect is deliberately conservative so a disabled/rate-limited tenant
// is not hammered by repeated connection attempts.
static constexpr uint32_t MQTT_RETRY_MIN_MS = 2UL * 60UL * 1000UL;
static constexpr uint32_t MQTT_RETRY_MAX_MS = 30UL * 60UL * 1000UL;
static constexpr uint16_t MQTT_KEEP_ALIVE_SEC = 60;
static constexpr uint16_t MQTT_SOCKET_TIMEOUT_SEC = 1;
static constexpr uint16_t MQTT_BUFFER_SIZE = 1024;

// -----------------------------------------------------------------------------
// 9) MQ-135 relative gas trend
// -----------------------------------------------------------------------------
static constexpr uint32_t MQ_OPERATIONAL_WARMUP_MS = 5UL * 60UL * 1000UL;
static constexpr uint16_t MQ_BASELINE_SAMPLE_COUNT = 60;
static constexpr uint8_t MQ_ADC_SAMPLE_COUNT = 24;
static constexpr float MQ_DIVIDER_RECONSTRUCTION_FACTOR = 2.0f;
static constexpr float MQ_FILTER_ALPHA = 0.18f;
static constexpr uint16_t MQ_BASELINE_SCHEMA_VERSION = 3;
static constexpr uint8_t MQ_BASELINE_MISMATCH_SAMPLES = 8;
static constexpr float MQ_BASELINE_MISMATCH_LOW = 0.45f;
static constexpr float MQ_BASELINE_MISMATCH_HIGH = 2.20f;
static constexpr uint32_t BASELINE_SAVE_MIN_INTERVAL_MS = 30UL * 60UL * 1000UL;
static constexpr float GAS_RATIO_GOOD_MAX = 1.10f;
static constexpr float GAS_RATIO_CAUTION_MAX = 1.25f;

// -----------------------------------------------------------------------------
// 10) Prototype thermal-comfort thresholds for hot-humid context
// -----------------------------------------------------------------------------
// Ngưỡng UX thử nghiệm, không phải chẩn đoán y khoa.
static constexpr float TEMP_TARGET_MIN_C = 24.0f;
static constexpr float TEMP_TARGET_MAX_C = 29.0f;
static constexpr float HUM_TARGET_MIN_PCT = 40.0f;
static constexpr float HUM_TARGET_MAX_PCT = 70.0f;
static constexpr float HEAT_INDEX_CAUTION_C = 35.0f;
static constexpr float HEAT_INDEX_POOR_C = 41.0f;