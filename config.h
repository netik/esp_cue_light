#pragma once

#define FIRMWARE_VERSION "1.0.0"

// NodeMCU pin labels in comments — change GPIO numbers here to rewire.
#define PIN_BTN_CUE1 5   // D1
#define PIN_BTN_CUE2 4   // D2

#define PIN_CUE1_RED 14    // D5
#define PIN_CUE1_GREEN 12  // D6
#define PIN_CUE2_RED 13    // D7
#define PIN_CUE2_GREEN 15  // D8

#define CUE_COUNT 2
#define CUE_NUMBER_1 1
#define CUE_NUMBER_2 2

#define CUE_STATE_RED 0
#define CUE_STATE_GREEN 1

#define DEFAULT_SYSTEM_ID 1
#define DEFAULT_CUE_GROUP 1

#define CUE_UDP_PORT 45271

#define BTN_DEBOUNCE_MS 50

#define LINE_END "\r\n"
#define LINE_END_LEN 2
