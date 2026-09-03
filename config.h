#pragma once

#include <Arduino.h>

#define FIRMWARE_VERSION "1.5.0"

// Heltec WiFi LoRa 32 V3 (ESP32-S3) is auto-selected when that FQBN/variant
// is used. Override with -DBOARD_HELTEC_V3 for a generic ESP32-S3 FQBN.
#if defined(BOARD_HELTEC_V3) || defined(WIFI_LoRa_32_V3) || \
    defined(ARDUINO_HELTEC_WIFI_LORA_32_V3)
#ifndef BOARD_HELTEC_V3
#define BOARD_HELTEC_V3 1
#endif
#define CUE_BOARD_HELTEC_V3 1
#endif

// Which cue(s) this board locally displays and buttons. Network sync still
// carries both cues. Integer values so #if works (string macros cannot).
//   CUE_LOCAL_ALL (0) — two boxes, cue 1 and cue 2
//   CUE_LOCAL_ONE (1) — one full-width box, cue 1 only
//   CUE_LOCAL_TWO (2) — one full-width box, cue 2 only (primary button → cue 2)
#define CUE_LOCAL_ALL 0
#define CUE_LOCAL_ONE 1
#define CUE_LOCAL_TWO 2

#ifndef CUE_LOCAL
#ifdef CUE_BOARD_HELTEC_V3
#define CUE_LOCAL CUE_LOCAL_ONE
#else
#define CUE_LOCAL CUE_LOCAL_ALL
#endif
#endif

#if CUE_LOCAL != CUE_LOCAL_ALL && CUE_LOCAL != CUE_LOCAL_ONE && \
    CUE_LOCAL != CUE_LOCAL_TWO
#error CUE_LOCAL must be CUE_LOCAL_ALL, CUE_LOCAL_ONE, or CUE_LOCAL_TWO
#endif

#ifdef CUE_BOARD_HELTEC_V3
// Heltec WiFi LoRa 32 V3 — do not use LoRa SPI (8–14), OLED I2C (17/18/21),
// UART0 (43/44), USB D+/D- (19/20), Vext (36), GPIO 1 (VBAT ADC), GPIO 37
// (ADC_CTRL), or strapping pins 45/46.
#define PIN_BTN_PRIMARY 0    // onboard PRG button (active LOW)
#define PIN_BTN_SECONDARY 2  // header; wire button to GND

#define PIN_CUE1_RED 4    // GPIO wired to the LED's red cathode
#define PIN_CUE1_GREEN 5  // GPIO wired to the LED's green cathode
#define PIN_CUE2_RED 6    // header
#define PIN_CUE2_GREEN 7  // header

#define PIN_STATUS_LED 35  // onboard LED, active HIGH
#define STATUS_LED_ACTIVE_LOW 0

#define CUE_HAS_OLED 1
#define PIN_OLED_SDA 17
#define PIN_OLED_SCL 18
#define PIN_OLED_RST 21
#define PIN_VEXT 36  // active LOW — powers OLED and Ve header pins
#define OLED_I2C_ADDR 0x3C

#define PIN_VBAT 1        // ADC1_CH0, battery voltage divider
#define PIN_ADC_CTRL 37   // drive LOW to connect the VBAT divider
#define VBAT_DIVIDER 4.9f // schematic: 100k / (100k+390k)

#define PIN_LORA_NSS 8
#define PIN_LORA_SCK 9
#define PIN_LORA_MOSI 10
#define PIN_LORA_MISO 11
#define PIN_LORA_RST 12
#define PIN_LORA_BUSY 13
#define PIN_LORA_DIO1 14

#define CUE_HAS_LORA 1
#define CUE_BOARD_LABEL "Heltec WiFi LoRa 32 V3"
#else
// NodeMCU v2 (ESP8266) — pin labels in comments.
#define PIN_BTN_PRIMARY 5    // D1
#define PIN_BTN_SECONDARY 4  // D2

#define PIN_CUE1_RED 14    // D5
#define PIN_CUE1_GREEN 12  // D6
#define PIN_CUE2_RED 13    // D7
#define PIN_CUE2_GREEN 15   // D8

#ifndef LED_BUILTIN
#define PIN_STATUS_LED 2  // NodeMCU onboard LED (D4), active LOW
#else
#define PIN_STATUS_LED LED_BUILTIN
#endif
#define STATUS_LED_ACTIVE_LOW 1

#define CUE_HAS_OLED 0
#define CUE_HAS_LORA 0
#define CUE_BOARD_LABEL "NodeMCU v2"
#endif

// CUE_LOCAL_TWO maps the physical primary button (Heltec PRG / NodeMCU D1)
// onto cue 2. WiFi wipe always uses PIN_BTN_PRIMARY, not the cue mapping.
#if CUE_LOCAL == CUE_LOCAL_TWO
#define PIN_BTN_CUE1 PIN_BTN_SECONDARY
#define PIN_BTN_CUE2 PIN_BTN_PRIMARY
#else
#define PIN_BTN_CUE1 PIN_BTN_PRIMARY
#define PIN_BTN_CUE2 PIN_BTN_SECONDARY
#endif

#if STATUS_LED_ACTIVE_LOW
#define STATUS_LED_ON LOW
#define STATUS_LED_OFF HIGH
#else
#define STATUS_LED_ON HIGH
#define STATUS_LED_OFF LOW
#endif

// Cue RGB lamps are common-anode: GPIO sinks current, LOW = color on.
// If OLED says RED but the LED shows green (or vice versa), swap the two
// PIN_CUE*_RED / PIN_CUE*_GREEN values for that cue — not LAMP_ON/LAMP_OFF.
#define LAMP_ON LOW
#define LAMP_OFF HIGH

#define CUE_COUNT 2
#define CUE_NUMBER_1 1
#define CUE_NUMBER_2 2

#if CUE_LOCAL == CUE_LOCAL_ALL
#define CUE_LOCAL_LABEL "ALL"
#define CUE_STATUS_NUMBER CUE_NUMBER_1
#elif CUE_LOCAL == CUE_LOCAL_TWO
#define CUE_LOCAL_LABEL "TWO"
#define CUE_STATUS_NUMBER CUE_NUMBER_2
#else
#define CUE_LOCAL_LABEL "ONE"
#define CUE_STATUS_NUMBER CUE_NUMBER_1
#endif

#define CUE_STATE_RED 0
#define CUE_STATE_GREEN 1

#define DEFAULT_SYSTEM_ID 1
#define DEFAULT_CUE_GROUP 1

#define CUE_MDNS_SERVICE "cuelight"
#define CUE_HTTP_PORT 80

#define PEER_SYNC_MAX_PEERS 8
#define PEER_SYNC_MDNS_EVENT_QUEUE 4
#define PEER_SYNC_POLL_INTERVAL_MS 500
#define CUE_PUSH_BODY_TIMEOUT_MS 2000
#define PEER_SYNC_DISCOVERY_MS 15000
#define PEER_SYNC_HTTP_TIMEOUT_MS 400
#define PEER_SYNC_PUSH_TIMEOUT_MS 400
#define PEER_SYNC_POLL_SUPPRESS_MS 800
#define PEER_SYNC_PEER_STALE_MS 30000
#ifdef ARDUINO_ARCH_ESP32
#define PEER_SYNC_MDNS_QUERY_MS 350
#endif

#define BTN_LOCKOUT_MS 150
#define BTN_RELEASE_ARM_MS 50

#define AP_PASSWORD "123456789"

#define WIFI_CREDENTIALS_FILE "/credentials.bin"
#define WIFI_WIPE_HOLD_MS 3000
#define WIFI_WIPE_WINDOW_MS 8000
#define POWER_OFF_HOLD_MS 3000
#define CUE_DFM_BLINK_MS 250

#if CUE_HAS_LORA
#define LORA_FREQUENCY_MHZ 915.0f
#define LORA_CHANNEL_STEP_MHZ 0.2f
#define LORA_CHANNEL_MAX 7
#define LORA_SF 7
#define LORA_BW_KHZ 125.0f
#define LORA_CR 5
#define LORA_SYNC_WORD 0xC1
#define LORA_POWER_DBM 14
#define LORA_TCXO_VOLTS 1.8f
#define LORA_PREAMBLE_LEN 8
#define LORA_BEACON_MS 5000
#define LORA_CAD_BACKOFF_MS 20
#define LORA_PACKET_LEN 20
#define LORA_MAGIC 0xC1
#define LORA_VERSION 2
#define LORA_HEARD_MAX 8
#define LORA_HEARD_STALE_MS 20000
#endif

#ifndef LORA_HEARD_MAX
#define LORA_HEARD_MAX 8
#endif

#define LINE_END "\r\n"
#define LINE_END_LEN 2
