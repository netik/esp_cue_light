#define ESP_FS_WS_MDNS 0

#include <AsyncFsWebServer.h>
#include <LittleFS.h>

#include "CueDisplay.h"
#include "CueIO.h"
#include "CueLora.h"
#include "DashboardHtml.h"
#include "PeerSync.h"
#include "PlatformCompat.h"
#include "config.h"

#ifdef CUE_BOARD_HELTEC_V3
#include <esp_sleep.h>
#endif

#define FILESYSTEM LittleFS
AsyncFsWebServer server(FILESYSTEM, 80);

uint16_t systemId = DEFAULT_SYSTEM_ID;
uint16_t cueGroup = DEFAULT_CUE_GROUP;
#if CUE_HAS_LORA
bool enableLora = false;
bool enableWifi = true;
uint8_t loraChannel = 0;
#endif

bool wipeButtonPressed() {
  return digitalRead(PIN_BTN_PRIMARY) == LOW;
}

bool waitForWifiWipeHold() {
  pinMode(PIN_BTN_PRIMARY, INPUT_PULLUP);
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, STATUS_LED_OFF);

  Serial.printf_P(PSTR("Boot DFM: hold primary button %u seconds within %u seconds to wipe WiFi.\r\n"),
                  (unsigned)(WIFI_WIPE_HOLD_MS / 1000),
                  (unsigned)(WIFI_WIPE_WINDOW_MS / 1000));

  const unsigned long windowStart = millis();
  unsigned long holdStart = 0;
  bool announcedHold = false;

  while (true) {
    const unsigned long now = millis();
    const bool green = ((now / CUE_DFM_BLINK_MS) % 2) != 0;
    cueIO.setDfmLamps(green ? CUE_STATE_GREEN : CUE_STATE_RED);

    const bool pressed = wipeButtonPressed();
    if (pressed) {
      if (holdStart == 0) {
        holdStart = now;
      }
      if (!announcedHold) {
        Serial.printf_P(PSTR("Primary button held: keep pressed %u seconds to wipe WiFi...\r\n"),
                        (unsigned)(WIFI_WIPE_HOLD_MS / 1000));
        announcedHold = true;
      }
      digitalWrite(PIN_STATUS_LED,
                   ((now / 200) % 2) ? STATUS_LED_ON : STATUS_LED_OFF);
      if ((now - holdStart) >= WIFI_WIPE_HOLD_MS) {
        digitalWrite(PIN_STATUS_LED, STATUS_LED_OFF);
        cueIO.setDfmLamps(CUE_STATE_RED);
        return true;
      }
    } else {
      if (announcedHold) {
        Serial.print(F("WiFi wipe cancelled."));
        Serial.print(LINE_END);
        announcedHold = false;
      }
      holdStart = 0;
      digitalWrite(PIN_STATUS_LED, STATUS_LED_OFF);
      if ((now - windowStart) >= WIFI_WIPE_WINDOW_MS) {
        cueIO.setDfmLamps(CUE_STATE_RED);
        return false;
      }
    }

    delay(20);
  }
}

// Clears saved STA credentials and starts AP. Heltec: also writes Enable
// WiFi back on so a LoRa-only board can reach /setup. Call after addOption.
void wipeWifiConfig() {
#if CUE_HAS_LORA
  // Options must already be registered so this can persist Enable WiFi.
  enableWifi = true;
  if (!server.saveOptionValue("Enable WiFi", true)) {
    Serial.print(F("Warning: could not write Enable WiFi on wipe."));
    Serial.print(LINE_END);
  }
#endif

  if (FILESYSTEM.exists(WIFI_CREDENTIALS_FILE)) {
    FILESYSTEM.remove(WIFI_CREDENTIALS_FILE);
  }

  if (CredentialManager* creds = server.getCredentialManager()) {
    creds->clearAll();
  }

  WiFi.persistent(true);
#ifdef ARDUINO_ARCH_ESP32
  WiFi.disconnect(true, true);
#else
  WiFi.disconnect(true);
#endif

  Serial.print(F("WiFi credentials wiped. Starting AP /setup."));
  Serial.print(LINE_END);

  cueDisplayShowWifiWiped();
  digitalWrite(PIN_STATUS_LED, STATUS_LED_ON);
  cueIO.setDfmLamps(CUE_STATE_GREEN);

  while (wipeButtonPressed()) {
    delay(10);
  }
  delay(2000);
}

void buildApSsid(char* ssid, size_t size) { cueDefaultSsid(ssid, size); }

void loadNetworkConfig() {
  if (FILESYSTEM.exists(server.getConfiFileName())) {
    server.getOptionValue("System ID", systemId);
    server.getOptionValue("Cue Group", cueGroup);
#if CUE_HAS_LORA
    server.getOptionValue("Enable LoRa", enableLora);
    server.getOptionValue("Enable WiFi", enableWifi);
    uint16_t channel = loraChannel;
    if (server.getOptionValue("LoRa Channel", channel)) {
      loraChannel = (uint8_t)((channel > LORA_CHANNEL_MAX) ? LORA_CHANNEL_MAX
                                                          : channel);
    }
    if (!enableWifi) {
      enableLora = true;
    }
#endif
  }
  peerSync.setNetworkFilter(systemId, cueGroup);
  Serial.printf_P(PSTR("\r\n\r\nPeer sync filter: system_id=%u cue_group=%u\r\n"),
                  systemId, cueGroup);
#if CUE_HAS_LORA
  Serial.printf_P(PSTR("LoRa config: enable=%u channel=%u wifi=%u\r\n"),
                  enableLora ? 1 : 0, loraChannel, enableWifi ? 1 : 0);
#endif
}

void applyLoraConfig() {
#if CUE_HAS_LORA
  cueLora.configure(enableLora, loraChannel, systemId, cueGroup);
#endif
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
  applyLoraConfig();
#if CUE_HAS_LORA
  if (!enableWifi) {
    enableLora = true;
    server.saveOptionValue("Enable LoRa", true);
    server.closeSetupConfiguration();
    Serial.print(F("Enable WiFi off: reboot to LoRa-only."));
    Serial.print(LINE_END);
    delay(250);
    ESP.restart();
  }
#endif
}

void handleCueStatus(AsyncWebServerRequest* request) {
  char json[128];
  peerSync.buildStateJson(json, sizeof(json));
  request->send(200, "application/json", json);
}

namespace {
struct CuePushBody {
  AsyncWebServerRequest* request = nullptr;
  unsigned long startedMs = 0;
  char json[128];
};

CuePushBody g_cuePushBody;

void resetCuePushBodyForRequest(AsyncWebServerRequest* request) {
  if (g_cuePushBody.request == request) {
    g_cuePushBody.request = nullptr;
    g_cuePushBody.startedMs = 0;
    g_cuePushBody.json[0] = '\0';
  }
}

void checkCuePushBodyTimeout() {
  if (g_cuePushBody.request == nullptr) {
    return;
  }
  if ((millis() - g_cuePushBody.startedMs) >= CUE_PUSH_BODY_TIMEOUT_MS) {
    resetCuePushBodyForRequest(g_cuePushBody.request);
  }
}
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
    g_cuePushBody.startedMs = millis();
    g_cuePushBody.json[0] = '\0';
    request->onDisconnect([request]() { resetCuePushBodyForRequest(request); });
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

  const bool applied = peerSync.applyIncomingJson(g_cuePushBody.json);
  resetCuePushBodyForRequest(request);

  if (applied) {
    request->send(200, "application/json", "{\"ok\":1}");
  } else {
    request->send(409, "application/json", "{\"ok\":0}");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

#ifdef CUE_BOARD_HELTEC_V3
  CueIO::releaseSleepHolds();
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, LOW);
  const bool wokeFromSleep =
      esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
#else
  const bool wokeFromSleep = false;
#endif

  Serial.printf_P(PSTR("Cue Light %s (%s, CUE_LOCAL=%s)\r\n"), FIRMWARE_VERSION,
                  CUE_BOARD_LABEL, CUE_LOCAL_LABEL);
#ifdef CUE_BOARD_HELTEC_V3
  if (wokeFromSleep) {
    Serial.print(F("Woke from power-off."));
    Serial.print(LINE_END);
  }
#endif

  cueDisplayBegin();

  const bool wipeWifi = wokeFromSleep ? false : waitForWifiWipeHold();

#ifdef ARDUINO_ARCH_ESP32
  if (!FILESYSTEM.begin(true)) {
#else
  if (!FILESYSTEM.begin()) {
#endif
    Serial.print(F("ERROR mounting filesystem."));
    Serial.print(LINE_END);
    ESP.restart();
  }

  ensureDashboardOnFs();

  server.addOptionBox("Cue Network");
  server.addOption("System ID", systemId);
  server.addOption("Cue Group", cueGroup);
#if CUE_HAS_LORA
  server.addOptionBox("LoRa");
  server.addOption("Enable LoRa", enableLora);
  server.addComment("Enable LoRa",
                    "915 MHz SX1262. Relays cue state to WiFi peers when on the LAN.");
  server.addOption("Enable WiFi", enableWifi);
  server.addComment("Enable WiFi",
                    "Uncheck for LoRa only. Boot wipe turns WiFi back on.");
  server.addOption("LoRa Channel", loraChannel, 0.0, 7.0, 1.0);
  server.addComment("LoRa Channel", "0-7 sub-band. Match this on every Heltec.");
#endif
  server.setSetupPageTitle("Cue Light Setup");

  loadNetworkConfig();

  if (wipeWifi) {
    wipeWifiConfig();
  }

#if CUE_HAS_LORA
  const bool loraOnly = !wipeWifi && !enableWifi;
#else
  const bool loraOnly = false;
#endif

#if CUE_HAS_LORA
  if (loraOnly) {
    enableLora = true;
    server.saveOptionValue("Enable LoRa", true);
    cueWifiRadioOff();
    server.closeSetupConfiguration();
  } else
#endif
  {
    if (wipeWifi || !server.startWiFi(10000)) {
      char apSsid[20];
      buildApSsid(apSsid, sizeof(apSsid));
      Serial.printf_P(
          PSTR("\r\nWiFi not connected! Starting AP mode. SSID: %s / Password: %s\r\n"),
          apSsid, AP_PASSWORD);
      server.startCaptivePortal(apSsid, AP_PASSWORD, "/setup");
    }
    cueWifiDisableSleep();
  }

  cueIO.begin();

  if (!loraOnly) {
    server.setConfigSavedCallback(onConfigSaved);
    server.on("/api/cues", HTTP_GET, handleCueStatus);
    server.on("/api/cues", HTTP_POST, [](AsyncWebServerRequest* request) {},
              NULL, handleCuePush);
    server.init();

    if (!peerSync.begin()) {
      Serial.print(F("Warning: Peer sync unavailable."));
      Serial.print(LINE_END);
    }
  }

  applyLoraConfig();

  if (loraOnly) {
    Serial.printf_P(PSTR("Cue Light %s LoRa-only (WiFi off)\r\nReady.\r\n"),
                    FIRMWARE_VERSION);
    Serial.print(LINE_END);
  } else {
    Serial.printf_P(PSTR("Cue Light Webserver %s at "), FIRMWARE_VERSION);
    Serial.print(server.getServerIP());
    Serial.print(LINE_END);
    Serial.print(F("Dashboard at /. Configure network at /setup\r\nReady.\r\n"));
    Serial.print(LINE_END);
  }
}

void loop() {
  cueIO.loop();
  checkCuePushBodyTimeout();
  peerSync.loop();
  yield();
}
