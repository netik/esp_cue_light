#pragma once

#include <Arduino.h>

#ifdef ARDUINO_ARCH_ESP32
#include <HTTPClient.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <mdns.h>
#else
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#endif

// ESP32 Arduino 3.x WiFi.macAddress() reads the STA netif, which is NULL
// until the interface is up — so AP-SSID generation at boot gets 00:00:00:00:00:00.
inline void cueWifiMacAddress(uint8_t mac[6]) {
#ifdef ARDUINO_ARCH_ESP32
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    memset(mac, 0, 6);
  }
#else
  WiFi.macAddress(mac);
#endif
}

inline void cueWifiSetHostname(const char* hostname) {
#ifdef ARDUINO_ARCH_ESP32
  WiFi.setHostname(hostname);
#else
  WiFi.hostname(hostname);
#endif
}

inline void cueWifiDisableSleep() {
#ifdef ARDUINO_ARCH_ESP32
  WiFi.setSleep(false);
#else
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
}

inline bool cueHttpBegin(HTTPClient& http, WiFiClient& client, const char* url) {
#ifdef ARDUINO_ARCH_ESP32
  (void)client;
  return http.begin(url);
#else
  return http.begin(client, url);
#endif
}
