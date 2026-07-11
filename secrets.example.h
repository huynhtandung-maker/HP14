#pragma once

// Fallback first-boot credentials. HP14 can later be reconfigured by phone:
// hold PAGE + FOCUS together for 5 seconds, connect to HP14-SETUP-xxxx,
// then open http://192.168.4.1
#define WIFI_SSID     ""
#define WIFI_PASSWORD ""

#define TB_HOST  "mqtt.thingsboard.cloud"
#define TB_PORT  1883
#define TB_TOKEN "YOUR_THINGSBOARD_DEVICE_TOKEN"
