#pragma once

// OPTIONAL fallback WiFi. HP12 also stores WiFi entered through HP12-SETUP portal.
#define FALLBACK_WIFI_SSID     "Your_2_4GHz_WiFi"
#define FALLBACK_WIFI_PASSWORD "Your_WiFi_Password"

// ThingsBoard Cloud - copy EXACT Access token from device HP12.
// Devices > HP12 > Manage credentials > Copy access token.
#define TB_HOST  "thingsboard.cloud"
#define TB_PORT  1883
#define TB_TOKEN "PASTE_HP12_DEVICE_ACCESS_TOKEN_HERE"
