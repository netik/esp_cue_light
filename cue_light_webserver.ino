#define ESP_FS_WS_MDNS 0

#include <AsyncFsWebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "CueIO.h"
#include "DashboardHtml.h"
#include "UdpCue.h"
#include "config.h"

#define FILESYSTEM LittleFS
AsyncFsWebServer server(FILESYSTEM, 80);

uint16_t systemId = DEFAULT_SYSTEM_ID;
uint16_t cueGroup = DEFAULT_CUE_GROUP;

void buildApSsid(char* ssid, size_t size) {
  WiFi.mode(WIFI_STA);
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  snprintf(ssid, size, "CueLight-%02X%02X", mac[4], mac[5]);
}

void loadNetworkConfig() {
  if (FILESYSTEM.exists(server.getConfiFileName())) {
    server.getOptionValue("System ID", systemId);
    server.getOptionValue("Cue Group", cueGroup);
  }
  udpCue.setNetworkFilter(systemId, cueGroup);
  Serial.printf_P(PSTR("\r\n\r\nUDP: Network filter: system_id=%u cue_group=%u port=%u\r\n"),
                  systemId, cueGroup, CUE_UDP_PORT);
}

void ensureDashboardOnFs() {
  if (FILESYSTEM.exists("/index.htm")) {
    return;
  }

  File file = FILESYSTEM.open("/index.htm", "w");
  if (!file) {
    Serial.print(F("Warning: could not create /index.htm on LittleFS."));
    Serial.print(LINE_END);
    return;
  }

  file.print(FPSTR(DASHBOARD_INDEX_HTM));
  file.close();
  Serial.print(F("Created /index.htm on LittleFS."));
  Serial.print(LINE_END);
}

void onConfigSaved(const char* filename) {
  Serial.printf_P(PSTR("Config saved: %s\r\n"), filename);
  loadNetworkConfig();
  ensureDashboardOnFs();
}

void handleCueStatus(AsyncWebServerRequest* request) {
  char json[96];
  snprintf(json, sizeof(json),
           "{\"system_id\":%u,\"cue_group\":%u,\"cue1\":%u,\"cue2\":%u}",
           systemId, cueGroup, cueIO.getCueState(CUE_NUMBER_1),
           cueIO.getCueState(CUE_NUMBER_2));
  request->send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);

  if (!FILESYSTEM.begin()) {
    Serial.print(F("ERROR mounting filesystem."));
    Serial.print(LINE_END);
    ESP.restart();
  }

  ensureDashboardOnFs();

  server.addOptionBox("Cue Network");
  server.addOption("System ID", systemId);
  server.addOption("Cue Group", cueGroup);
  server.setSetupPageTitle("Cue Light Setup");

  loadNetworkConfig();

  if (!server.startWiFi(10000)) {
    char apSsid[20];
    buildApSsid(apSsid, sizeof(apSsid));
    Serial.printf_P(PSTR("\r\nWiFi not connected! Starting AP mode. SSID: %s / Password: %s\r\n"),
                    apSsid, AP_PASSWORD);
    server.startCaptivePortal(apSsid, AP_PASSWORD, "/setup");
  }

  server.setConfigSavedCallback(onConfigSaved);

  cueIO.begin();

  if (!udpCue.begin()) {
    Serial.print(F("Warning: UDP cue sync unavailable."));
    Serial.print(LINE_END);
  }

  server.enableFsCodeEditor();
  server.on("/api/cues", HTTP_GET, handleCueStatus);

  server.init();
  Serial.printf_P(PSTR("\r\nCue Light Webserver %s at "), FIRMWARE_VERSION);
  Serial.print(server.getServerIP());
  Serial.print(LINE_END);
  Serial.print(F("Dashboard at /. Configure network at /setup"));
  Serial.print(LINE_END);
}

void loop() {
  udpCue.loop();
  cueIO.loop();
  delay(10);
}
