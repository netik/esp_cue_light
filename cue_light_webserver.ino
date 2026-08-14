#define ESP_FS_WS_MDNS 0

#include <AsyncFsWebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

#include "CueIO.h"
#include "DashboardHtml.h"
#include "PeerSync.h"
#include "config.h"

#define FILESYSTEM LittleFS
AsyncFsWebServer server(FILESYSTEM, 80);

uint16_t systemId = DEFAULT_SYSTEM_ID;
uint16_t cueGroup = DEFAULT_CUE_GROUP;

bool cue1ButtonPressed() {
  return digitalRead(PIN_BTN_CUE1) == LOW;
}

bool waitForWifiWipeHold() {
  pinMode(PIN_BTN_CUE1, INPUT_PULLUP);
  if (!cue1ButtonPressed()) {
    return false;
  }

  Serial.printf_P(PSTR("Cue 1 held at boot: keep pressed %u seconds to wipe WiFi...\r\n"),
                  (unsigned)(WIFI_WIPE_HOLD_MS / 1000));

  pinMode(PIN_STATUS_LED, OUTPUT);
  const unsigned long deadline = millis() + WIFI_WIPE_HOLD_MS;
  while (millis() < deadline) {
    if (!cue1ButtonPressed()) {
      Serial.print(F("WiFi wipe cancelled."));
      Serial.print(LINE_END);
      digitalWrite(PIN_STATUS_LED, HIGH);
      return false;
    }

    digitalWrite(PIN_STATUS_LED, ((millis() / 200) % 2) ? LOW : HIGH);
    delay(50);
  }

  digitalWrite(PIN_STATUS_LED, HIGH);
  return true;
}

void wipeWifiConfig() {
  if (FILESYSTEM.exists(WIFI_CREDENTIALS_FILE)) {
    FILESYSTEM.remove(WIFI_CREDENTIALS_FILE);
  }

  WiFi.persistent(false);
  WiFi.disconnect(true);
  WiFi.persistent(true);

  Serial.print(F("WiFi credentials wiped. Device will start AP /setup on next boot."));
  Serial.print(LINE_END);

  while (cue1ButtonPressed()) {
    delay(10);
  }
}

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
  peerSync.setNetworkFilter(systemId, cueGroup);
  Serial.printf_P(PSTR("\r\n\r\nPeer sync filter: system_id=%u cue_group=%u\r\n"),
                  systemId, cueGroup);
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
  char json[128];
  peerSync.buildStateJson(json, sizeof(json));
  request->send(200, "application/json", json);
}

namespace {
struct CuePushBody {
  AsyncWebServerRequest* request = nullptr;
  char json[128];
};

CuePushBody g_cuePushBody;
}  // namespace

void handleCuePush(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                   size_t index, size_t total) {
  if (total >= sizeof(g_cuePushBody.json) || total == 0) {
    request->send(413, "application/json", "{\"ok\":0}");
    return;
  }

  if (index == 0) {
    if (g_cuePushBody.request != nullptr && g_cuePushBody.request != request) {
      request->send(503, "application/json", "{\"ok\":0}");
      return;
    }
    g_cuePushBody.request = request;
    g_cuePushBody.json[0] = '\0';
  }

  if (g_cuePushBody.request != request) {
    return;
  }

  const size_t offset = min(index, sizeof(g_cuePushBody.json) - 1);
  const size_t copyLen = min(len, sizeof(g_cuePushBody.json) - 1 - offset);
  memcpy(g_cuePushBody.json + offset, data, copyLen);
  g_cuePushBody.json[offset + copyLen] = '\0';

  if (index + len < total) {
    return;
  }

  g_cuePushBody.request = nullptr;

  if (peerSync.applyIncomingJson(g_cuePushBody.json)) {
    request->send(200, "application/json", "{\"ok\":1}");
  } else {
    request->send(409, "application/json", "{\"ok\":0}");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  const bool wipeWifi = waitForWifiWipeHold();

  if (!FILESYSTEM.begin()) {
    Serial.print(F("ERROR mounting filesystem."));
    Serial.print(LINE_END);
    ESP.restart();
  }

  if (wipeWifi) {
    wipeWifiConfig();
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

  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  server.setConfigSavedCallback(onConfigSaved);

  cueIO.begin();

  server.enableFsCodeEditor();
  server.on("/api/cues", HTTP_GET, handleCueStatus);
  server.on("/api/cues", HTTP_POST, [](AsyncWebServerRequest* request) {},
            NULL, handleCuePush);

  server.init();

  if (!peerSync.begin()) {
    Serial.print(F("Warning: Peer sync unavailable."));
    Serial.print(LINE_END);
  }

  Serial.printf_P(PSTR("Cue Light Webserver %s at "), FIRMWARE_VERSION);
  Serial.print(server.getServerIP());
  Serial.print(LINE_END);
  Serial.print(F("Dashboard at /. Configure network at /setup\r\nReady.\r\n"));
  Serial.print(LINE_END);
}

void loop() {
  cueIO.loop();
  peerSync.loop();
  yield();
}
