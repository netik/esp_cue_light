#pragma once

#define FIRMWARE_VERSION "1.1.4"

// NodeMCU pin labels in comments — change GPIO numbers here to rewire.
#define PIN_BTN_CUE1 5   // D1
#define PIN_BTN_CUE2 4   // D2

#define PIN_CUE1_RED 14    // D5
#define PIN_CUE1_GREEN 12  // D6
#define PIN_CUE2_RED 13    // D7
#define PIN_CUE2_GREEN 15  // D8

#ifndef LED_BUILTIN
#define PIN_STATUS_LED 2  // NodeMCU onboard LED (D4), active LOW
#else
#define PIN_STATUS_LED LED_BUILTIN
#endif

#define CUE_COUNT 2
#define CUE_NUMBER_1 1
#define CUE_NUMBER_2 2

#define CUE_STATE_RED 0
#define CUE_STATE_GREEN 1

#define DEFAULT_SYSTEM_ID 1
#define DEFAULT_CUE_GROUP 1

#define CUE_MDNS_SERVICE "cuelight"
#define CUE_HTTP_PORT 80

#define PEER_SYNC_MAX_PEERS 8
#define PEER_SYNC_POLL_INTERVAL_MS 500
#define PEER_SYNC_DISCOVERY_MS 15000
#define PEER_SYNC_HTTP_TIMEOUT_MS 1500
#define PEER_SYNC_PUSH_TIMEOUT_MS 800
#define PEER_SYNC_PEER_STALE_MS 120000

#define BTN_LOCKOUT_MS 150

#define AP_PASSWORD "123456789"

#define WIFI_CREDENTIALS_FILE "/credentials.bin"
#define WIFI_WIPE_HOLD_MS 3000

#define LINE_END "\r\n"
#define LINE_END_LEN 2
