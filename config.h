#pragma once

/*
  =============================================================================
  PCODE HP12 - DATA INTELLIGENCE EDITION (TROPICAL DEEP WORK CONFIG)
  =============================================================================
  Cấu hình tập trung cho HP12.

  Mục tiêu dự án:
  - Đánh giá chất lượng không gian phục vụ duy trì tập trung làm việc sâu.
  - Thích nghi khí hậu Việt Nam: nhiệt đới nóng ẩm, gió mùa, không lấy chuẩn
    "mát quanh năm" kiểu châu Âu làm ngưỡng duy nhất.
  - 3 LED phải hữu dụng:
      GREEN  = không gian còn hỗ trợ tập trung.
      YELLOW = bắt đầu hao năng lượng nhận thức, cần can thiệp nhẹ.
      RED    = stress nhiệt / lỗi cảm biến nghiêm trọng, cần xử lý ngay.
  - Còi ưu tiên chống "alarm fatigue": im ở cảnh báo vàng, chỉ bíp ngắn khi đỏ.

  Lưu ý:
  - File HP12.ino đọc toàn bộ ngưỡng từ file này.
  - Không hard-code WiFi/ThingsBoard token trong config.h; dùng secrets.h nếu cần.
  =============================================================================
*/

// ============================================================================
// 1. THÔNG TIN ĐỊNH DANH THIẾT BỊ
// ============================================================================
#define DEVICE_NAME        "HP12_Edge"
#define DEVICE_MODEL       "PCODE_DataNode_V1"
#define FIRMWARE_VERSION   "v1.13.19" // WiFi Identity + IP-location clarity on OLED/ThingsBoard


// ============================================================================
// 2. CẤU HÌNH CHÂN PHẦN CỨNG (PIN CONFIGURATION)
// ============================================================================

// --- Cảm biến Nhiệt Ẩm (DHT22) ---
#define DHT_PIN            14
#define DHT_TYPE           DHT22

// --- Màn hình OLED SSD1306 (I2C) ---
#define OLED_SDA_PIN       21
#define OLED_SCL_PIN       22
#define OLED_WIDTH         128
#define OLED_HEIGHT        64
#define OLED_ADDR          0x3C
#define OLED_RESET         -1

// --- Cảm biến Ánh sáng (LDR LM393) ---
#define LIGHT_SENSOR_ENABLED       1
#define LIGHT_AO_PIN               34
#define LIGHT_DO_PIN               35
#define LIGHT_USE_DIGITAL_DO       0
#define LIGHT_ADC_BRIGHT_IS_HIGH   0   // 0: module LM393 thường sáng mạnh -> ADC thấp

// --- Nút Bấm Vật Lý (Buttons) ---
#define BUTTON_NAV_PIN             27
#define BUTTON_NAV_ACTIVE_LOW      1
#define BUTTON_ADVICE_PIN          13
#define BUTTON_ADVICE_ACTIVE_LOW   1

// --- Còi & Đèn LED môi trường ---
#define BUZZER_PIN                 26
#define BUZZER_ACTIVE_LOW          0
#define LED_GREEN_PIN              25
#define LED_YELLOW_PIN             32
#define LED_RED_PIN                33
#define LED_ACTIVE_LOW             1

// --- LED tín hiệu bo mạch (System Heartbeat) ---
#define ONBOARD_BLUE_LED_ENABLED    1
#define ONBOARD_BLUE_LED_PIN        2
#define ONBOARD_BLUE_LED_ACTIVE_LOW 0
#define BLUE_LED_SELF_TEST_MS       8000UL


// ============================================================================
// 3. TẦN SUẤT ĐỌC & HIỂN THỊ
// ============================================================================
#define SENSOR_READ_INTERVAL_MS      2000UL   // DHT22 không nên đọc nhanh hơn 2s/lần
#define LIGHT_READ_INTERVAL_MS       500UL
#define OLED_UPDATE_INTERVAL_MS      160UL
#define LED_UPDATE_INTERVAL_MS       80UL
#define BLUE_LED_UPDATE_INTERVAL_MS  60UL
#define SCROLL_UPDATE_INTERVAL_MS    150UL

#define BUTTON_DEBOUNCE_MS           35UL
#define BUTTON_CLICK_WINDOW_MS       450UL
#define BUTTON_LONG_PRESS_MS         2500UL
#define HELP_PAGE_DURATION_MS        8000UL
#define ALARM_MUTE_MS                600000UL  // 10 phút


// ============================================================================
// 4. MẠNG & THỜI GIAN THỰC (WIFI, PORTAL, NTP)
// ============================================================================
// Timeout 60s để tương thích router Mesh/Doanh nghiệp và mạng cấp IP chậm.
#define WIFI_CONNECT_TIMEOUT_MS      45000UL
#define WIFI_RETRY_INTERVAL_MS       7000UL

#define WIFI_SETUP_PORTAL_ENABLED    1
#define WIFI_SETUP_AP_SSID           "HP12-SETUP"
#define WIFI_SETUP_AP_PASSWORD       "12345678"  // WPA2 AP setup: ổn định hơn với điện thoại; không dùng mật khẩu thật
#define WIFI_SETUP_DNS_PORT          53
#define WIFI_SETUP_PREF_NAMESPACE    "hp12wifi"
#define WIFI_SETUP_RESTART_MS        2500UL
#define WIFI_SETUP_TRIGGER_HOLD_MS   5500UL      // Giữ cả NAV + ADVICE để xóa WiFi cũ
#define WIFI_SETUP_SINGLE_HOLD_MS    8500UL      // Fallback optional: giữ riêng NAV hoặc ADVICE 8.5s để mở setup
#define WIFI_SETUP_SINGLE_HOLD_ENABLED 0          // 0 = giữ nguyên chức năng click/long-press của từng nút OLED; setup chính dùng NAV+ADVICE
#define WIFI_SERIAL_COMMAND_ENABLED  1           // Cho phép gõ SETUP / RESETWIFI trong Serial Monitor để mở portal

// WiFi hardening: tránh lỗi ESP32 "sta is connecting, cannot set config"
#define WIFI_STA_STOP_SETTLE_MS      700UL       // Dừng STA kỹ hơn rồi mới bật AP Portal
#define WIFI_SET_SLEEP_DISABLE       1           // Tắt WiFi sleep để giảm chập chờn MQTT/WiFi
#define WIFI_SET_TX_POWER_MAX        1           // Tăng công suất phát để ổn định RSSI ở vị trí xa router
#define WIFI_COUNTRY_VN_ENABLED      1           // VN dùng kênh WiFi 1-13; tránh mất SSID ở kênh 12/13
#define WIFI_SCAN_BEFORE_CONNECT     1           // Scan 2.4GHz trước khi begin để biết SSID cũ có thật sự hiện diện không
#define WIFI_FAST_PORTAL_IF_SSID_NOT_FOUND 1     // Mang sang nơi mới: không thấy SSID cũ -> mở HP12-SETUP nhanh
#define WIFI_SCAN_MIN_RSSI_DBM       -88         // Dưới mức này vẫn thử kết nối nhưng báo rất yếu
#define WIFI_WEAK_RSSI_PORTAL_DBM    -82         // Nếu boot chưa từng online + RSSI yếu hơn mức này -> mở portal nhanh
#define WIFI_OPEN_PORTAL_IMMEDIATELY_IF_WEAK 0     // v1.13.16: không tự mở setup chỉ vì RSSI yếu; tránh OTA xong mắc kẹt ở portal
#define WIFI_DHCP_GRACE_MS           12000UL     // AP đã bắt được nhưng chưa có IP thì chờ DHCP trước khi rejoin
#define WIFI_REJOIN_CLEAN_DISCONNECT_MS 250UL    // Dừng STA ngắn trước mỗi WiFi.begin để tránh trạng thái treo
#define WIFI_PORTAL_AFTER_BOOT_TIMEOUTS 2        // Boot không vào được WiFi cũ sau 2 lượt -> mở portal; OTA-safe boot sẽ suppress tạm thời
#define WIFI_PORTAL_AFTER_DROP_TIMEOUTS 0        // 0 = đã từng online thì KHÔNG tự bỏ STA sang portal; retry mãi để tránh mất cloud

// Phone-friendly captive portal hardening
#define WIFI_SETUP_FORCE_AP_ONLY     1           // Khi vào setup, chỉ phát AP HP12-SETUP, không chạy STA song song để điện thoại bám ổn định hơn
#define WIFI_SETUP_AP_CHANNEL        1           // Kênh AP cố định, tương thích tốt với điện thoại; ESP32 chỉ 2.4GHz
#define WIFI_SETUP_AP_MAX_CONNECTIONS 4
#define WIFI_SETUP_AP_SSID_DYNAMIC  1           // 1 = AP có hậu tố MAC, tránh điện thoại cache mạng HP12-SETUP cũ không internet
#define WIFI_SETUP_AP_PASSWORD_DEFAULT "12345678"
#define WIFI_PORTAL_SCAN_LIST_ENABLED 1          // Trang 192.168.4.1 hiển thị danh sách WiFi quét được để bấm chọn, không phải đổi WiFi trong cài đặt điện thoại
#define WIFI_PORTAL_SCAN_MAX_ITEMS   12
#define WIFI_PORTAL_USE_GET_SAVE     1           // Hỗ trợ lưu bằng GET + POST để captive browser trên điện thoại ít lỗi submit hơn
#define WIFI_PORTAL_PRINT_CLIENT_DIAGNOSTICS_MS 8000UL // In số điện thoại đang bám AP để chẩn đoán setup

// --- WiFi Doctor UX: chuẩn hóa trải nghiệm cấu hình WiFi cho người dùng phổ thông ---
// ESP32 cần sóng mạnh hơn điện thoại. Không cho lưu mặc định các WiFi quá yếu để tránh vòng lặp setup.
#define WIFI_IOT_EXCELLENT_RSSI_DBM       -55
#define WIFI_IOT_GOOD_RSSI_DBM            -67
#define WIFI_IOT_ACCEPTABLE_RSSI_DBM      -75
#define WIFI_IOT_WEAK_RSSI_DBM            -82
#define WIFI_IOT_BLOCK_RSSI_DBM           -83
#define WIFI_PORTAL_BLOCK_WEAK_WIFI       1
#define WIFI_PORTAL_ALLOW_FORCE_WEAK      1
#define WIFI_PORTAL_VALIDATE_SCAN_BEFORE_SAVE 1

// OLED WiFi UX v1.13.19
#define WIFI_OLED_SHOW_CONNECTED_SSID 1          // Hiển thị tên WiFi đang kết nối thay vì chỉ CONNECTED
#define WIFI_LOCATION_SHOW_IP_BASIS   1          // Gửi rõ locationBasis/locationConfidence/locationSummary lên ThingsBoard

// --- OTA-safe WiFi boot ---
// Sau khi OTA thành công, boot kế tiếp phải ưu tiên reconnect WiFi cũ để quay lại ThingsBoard,
// không mắc kẹt vào HP12-SETUP chỉ vì RSSI yếu hoặc DHCP chậm.
#define OTA_SAFE_WIFI_BOOT_ENABLED       1
#define OTA_SAFE_WIFI_GRACE_MS           180000UL
#define WIFI_AUTO_PORTAL_ON_WEAK_RSSI_AFTER_OTA 0

#define NTP_SERVER                   "pool.ntp.org"
#define GMT_OFFSET_SEC               (7 * 3600)
#define DAYLIGHT_OFFSET_SEC          0

// Location theo IP WiFi/Internet: KHÔNG tắt.
// Mục tiêu: khi thiết bị được mang sang địa điểm/WiFi mới, HP12 tự lấy lại vị trí
// theo public IP của mạng đang kết nối và đẩy lên ThingsBoard.
// Sửa lỗi HTTP -11 bằng cách: timeout dài hơn, thử luân phiên nhiều provider,
// không đánh dấu fetched nếu fail, và retry nền theo chu kỳ thay vì bỏ cuộc.
#define LOCATION_FETCH_ENABLED             1
#define LOCATION_HTTP_TIMEOUT_MS           7000UL
#define LOCATION_RETRY_INTERVAL_MS         45000UL
#define LOCATION_RETRY_SLOW_INTERVAL_MS    600000UL
#define LOCATION_MAX_FAST_ATTEMPTS         6
#define LOCATION_REFRESH_ON_WIFI_CHANGE    1
#define LOCATION_USE_DEFAULT               1
#define DEFAULT_CITY                       "Ho Chi Minh City"
#define DEFAULT_COUNTRY_CODE               "VN"
#define DEFAULT_LATITUDE                   10.82
#define DEFAULT_LONGITUDE                  106.63


// ============================================================================
// 5. CLOUD & OTA
// ============================================================================
#define MQTT_CLIENT_ID_PREFIX        "HP12_Edge_"
#define MQTT_BUFFER_SIZE             1536
#define MQTT_RETRY_INTERVAL_MS       7000UL
// MQTT diagnostics / recovery
#define MQTT_FORCE_IMMEDIATE_AFTER_WIFI_MS 2500UL
#define MQTT_TOKEN_WARN_INTERVAL_MS        30000UL

#define TELEMETRY_INTERVAL_MS        60000UL
#define TELEMETRY_INTERVAL_MIN_MS    30000UL
#define TELEMETRY_INTERVAL_MAX_MS    600000UL
#define FORCE_FIRST_SYNC_ENABLED     1
#define ATTRIBUTES_RESEND_MS         900000UL

#define RPC_ENABLED                  1
#define RPC_RESTART_DELAY_MS         1200UL
#define OTA_ENABLED                  1
#define OTA_REQUIRE_HTTPS            1
#define OTA_ALLOW_INSECURE_TLS       1
#define OTA_HTTP_TIMEOUT_MS          45000UL
#define OTA_FOLLOW_REDIRECTS_ENABLED 1
#define OTA_REDIRECT_LIMIT           10
#define OTA_OLED_SUCCESS_HOLD_MS     2500UL
#define OTA_OLED_ERROR_HOLD_MS       2500UL


// ============================================================================
// 6. NGƯỠNG Y SINH DÀNH CHO DEEP WORK (NHIỆT ĐỚI/VIỆT NAM)
// ============================================================================
// Tinh thần: GREEN không có nghĩa là "lạnh/mát kiểu châu Âu" mà là
// "không gian vẫn còn hỗ trợ tập trung" trong bối cảnh nóng ẩm Việt Nam.

// --- Vùng XANH: hỗ trợ làm việc sâu trong bối cảnh nhiệt đới ---
#define TEMP_GOOD_MIN      24.5   // Dưới mức này dễ lạnh/khô do máy lạnh kéo dài
#define TEMP_GOOD_MAX      30.8   // Nới vùng xanh thực dụng; >31.5 mới cảnh báo rõ
#define HUM_GOOD_MIN       42.0   // Quá khô dễ khó chịu mắt/mũi khi dùng máy lạnh lâu
#define HUM_GOOD_MAX       76.0   // Nhiệt đới: 70-75% vẫn thường gặp; chỉ cảnh báo khi kèm nóng/HI cao

// --- Vùng VÀNG: vẫn làm việc được nhưng bắt đầu hao năng lượng nhận thức ---
#define TEMP_WARN          31.5   // Bắt đầu nóng rõ; nên quạt/thông gió/giảm tải nhiệt
#define HUM_WARN           82.0   // Ẩm cao rõ; tránh vàng giả ở nền khí hậu VN
#define HEAT_INDEX_WARN    36.0   // Cảm nhận nhiệt bắt đầu kéo giảm sự tỉnh táo rõ trong bối cảnh nhiệt đới

// --- Vùng ĐỎ: stress nhiệt rõ, cần hành động ngay ---
#define TEMP_DANGER        35.5   // Đỏ chỉ dành cho stress nhiệt rõ, tránh còi/đỏ quá sớm
#define HUM_DANGER         92.0   // Rất ẩm; cảnh báo mạnh, đặc biệt khi đi kèm nhiệt cao
#define HEAT_INDEX_DANGER  42.0   // Đỏ khi thật sự stress nhiệt; 38-41°C là vàng mạnh

// --- Mốc tham chiếu tính Focus Score ---
#define HEAT_INDEX_MED_CAUTION_C     35.0
#define HEAT_INDEX_MED_DANGER_C      42.0
#define HEAT_INDEX_MED_EXTREME_C     54.0

// --- Ánh sáng cho làm việc tập trung ---
#define LIGHT_DARK_PCT       22.0
#define LIGHT_FOCUS_MIN_PCT  30.0
#define LIGHT_FOCUS_MAX_PCT  82.0
#define LIGHT_GLARE_PCT      92.0

// --- Bộ lọc nhiễu cảm biến ---
#define SENSOR_TEMP_MIN    -10.0
#define SENSOR_TEMP_MAX    60.0
#define SENSOR_HUM_MIN     0.0
#define SENSOR_HUM_MAX     100.0
#define SENSOR_HUM_SUSPECT 99.9


// ============================================================================
// 7. ÂM HỌC & NHỊP ĐIỆU (ACOUSTIC & VISUAL RHYTHM)
// ============================================================================
#define BUZZER_PROFILE_START         1  // 0=Im lặng, 1=Văn phòng, 2=Cảnh báo mạnh

// --- Profile Vàng: mặc định im lặng để tránh phá Deep Work ---
#define BUZZER_WARN_ENABLED          0
#define BUZZER_WARN_PERIOD_MS        60000UL
#define BUZZER_WARN_ON_MS            35UL

// --- Profile Đỏ: văn phòng, chống alarm fatigue ---
#define BUZZER_NORMAL_PERIOD_MS      22000UL
#define BUZZER_NORMAL_ON_MS          55UL
#define BUZZER_NORMAL_GAP_MS         130UL
#define BUZZER_NORMAL_COUNT          2

// --- Profile Đỏ mạnh: stress nhiệt rõ / cần xử lý ngay ---
#define BUZZER_STRONG_PERIOD_MS      8000UL
#define BUZZER_STRONG_ON_MS          75UL
#define BUZZER_STRONG_GAP_MS         110UL
#define BUZZER_STRONG_COUNT          3

// --- Nhịp điệu đèn LED môi trường ---
#define LED_START_BLINK_MS           500UL
#define LED_WARN_BLINK_MS            1100UL  // Vàng chậm, mềm, không gây hoảng
#define LED_DANGER_BLINK_MS          220UL   // Đỏ nhanh, rõ, cần hành động
#define LED_SENSOR_BLINK_MS          800UL

// --- Nhịp tim Server (Onboard Blue LED) ---
#define BLUE_LED_ALIVE_PERIOD_MS     4000UL
#define BLUE_LED_PULSE_ON_MS         50UL
#define BLUE_LED_PULSE_GAP_MS        100UL


// ============================================================================
// 8. OLED LINE-GAUGE SCALE
// ============================================================================
#define HEAT_BAR_MIN_C               24.0
#define HEAT_BAR_MAX_C               52.0
#define TEMP_BAR_MIN_C               18.0
#define TEMP_BAR_MAX_C               38.0
#define HUM_BAR_MIN_PCT              20.0
#define HUM_BAR_MAX_PCT              100.0
#define LIGHT_BAR_MIN_PCT            0.0
#define LIGHT_BAR_MAX_PCT            100.0

#define FOCUS_SCORE_MIN              0.0
#define FOCUS_SCORE_MAX              100.0
#define FOCUS_SCORE_WARN             60.0
#define FOCUS_SCORE_DANGER           35.0

#define OLED_SAFE_X                  2
#define OLED_SAFE_W                  116
#define OLED_SAFE_RIGHT              (OLED_SAFE_X + OLED_SAFE_W - 1)
#define OLED_GAUGE_X                 6
#define OLED_GAUGE_W                 108
