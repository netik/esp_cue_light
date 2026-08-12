#include "UdpCue.h"

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include "CueIO.h"
#include "config.h"

UdpCue udpCue;

namespace {
WiFiUDP g_udp;

void stripLineEnding(char* text) {
  if (text == nullptr) {
    return;
  }
  size_t len = strlen(text);
  while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n')) {
    text[--len] = '\0';
  }
}

const char* fieldValue(const char* json, const char* key) {
  char pattern[20];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char* start = strstr(json, pattern);
  if (start == nullptr) {
    return nullptr;
  }
  return start + strlen(pattern);
}

bool parseCueMessage(const char* json, uint16_t* systemId, uint16_t* cueGroup,
                     uint8_t* cue, uint8_t* state) {
  const char* value = fieldValue(json, "system_id");
  if (value == nullptr) {
    return false;
  }
  *systemId = (uint16_t)atoi(value);

  value = fieldValue(json, "cue_group");
  if (value == nullptr) {
    return false;
  }
  *cueGroup = (uint16_t)atoi(value);

  value = fieldValue(json, "cue");
  if (value == nullptr) {
    return false;
  }
  *cue = (uint8_t)atoi(value);

  value = fieldValue(json, "state");
  if (value == nullptr) {
    return false;
  }
  *state = (uint8_t)atoi(value);
  return true;
}

IPAddress subnetBroadcastAddress() {
  const IPAddress ip = WiFi.localIP();
  const IPAddress mask = WiFi.subnetMask();
  return IPAddress(ip[0] | ~mask[0], ip[1] | ~mask[1], ip[2] | ~mask[2],
                   ip[3] | ~mask[3]);
}
}  // namespace

bool UdpCue::buildMessage(uint8_t cueNumber, uint8_t state, char* buffer,
                          size_t size) const {
  if (size <= LINE_END_LEN) {
    return false;
  }

  const int payloadLen = snprintf(
      buffer, size - LINE_END_LEN,
      "{\"system_id\":%u,\"cue_group\":%u,\"cue\":%u,\"state\":%u}",
      _systemId, _cueGroup, cueNumber, state);
  if (payloadLen <= 0) {
    return false;
  }

  buffer[payloadLen] = '\r';
  buffer[payloadLen + 1] = '\n';
  buffer[payloadLen + LINE_END_LEN] = '\0';
  return true;
}

void UdpCue::setNetworkFilter(uint16_t systemId, uint16_t cueGroup) {
  _systemId = systemId;
  _cueGroup = cueGroup;
}

bool UdpCue::begin() {
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    Serial.print(F("UDP cue sync unavailable until WiFi is connected."));
    Serial.print(LINE_END);
    return false;
  }

  g_udp.begin(CUE_UDP_PORT);

  _ready = true;
  const IPAddress broadcast = subnetBroadcastAddress();
  Serial.printf_P(PSTR("UDP cue sync ready on port %u, broadcast %s\r\n"),
                  CUE_UDP_PORT, broadcast.toString().c_str());
  return true;
}

bool UdpCue::broadcastCue(uint8_t cueNumber, uint8_t state) {
  if (!_ready) {
    return false;
  }

  char message[96];
  if (!buildMessage(cueNumber, state, message, sizeof(message))) {
    return false;
  }

  const IPAddress broadcast = subnetBroadcastAddress();
  if (!g_udp.beginPacket(broadcast, CUE_UDP_PORT)) {
    Serial.print(F("UDP TX beginPacket failed"));
    Serial.print(LINE_END);
    return false;
  }

  g_udp.write(reinterpret_cast<const uint8_t*>(message), strlen(message));
  if (!g_udp.endPacket()) {
    Serial.print(F("UDP TX endPacket failed"));
    Serial.print(LINE_END);
    return false;
  }

  Serial.printf_P(PSTR("UDP TX %s:%u %s\r\n"), broadcast.toString().c_str(),
                  CUE_UDP_PORT, message);
  return true;
}

void UdpCue::pollIncoming() {
  const int packetSize = g_udp.parsePacket();
  if (packetSize <= 0) {
    return;
  }

  const IPAddress remoteIp = g_udp.remoteIP();
  if (remoteIp == WiFi.localIP()) {
    g_udp.flush();
    return;
  }

  char buffer[96];
  const int readLen = g_udp.read(buffer, sizeof(buffer) - 1);
  g_udp.flush();
  if (readLen <= 0) {
    return;
  }

  buffer[readLen] = '\0';
  handleReceived(remoteIp, buffer, (size_t)readLen);
}

void UdpCue::handleReceived(const IPAddress& remoteIp, const char* data,
                            size_t len) {
  char buffer[96];
  const size_t copyLen = min(len, sizeof(buffer) - 1);
  memcpy(buffer, data, copyLen);
  buffer[copyLen] = '\0';
  stripLineEnding(buffer);

  uint16_t systemId = 0;
  uint16_t cueGroup = 0;
  uint8_t cue = 0;
  uint8_t state = 0;
  if (!parseCueMessage(buffer, &systemId, &cueGroup, &cue, &state)) {
    Serial.printf_P(PSTR("UDP RX invalid JSON from %s: %s\r\n"),
                    remoteIp.toString().c_str(), buffer);
    return;
  }

  if (systemId != _systemId || cueGroup != _cueGroup) {
    return;
  }

  if (state != CUE_STATE_RED && state != CUE_STATE_GREEN) {
    return;
  }

  Serial.printf_P(PSTR("UDP RX %s: %s\r\n"), remoteIp.toString().c_str(),
                  buffer);
  cueIO.setCueState(cue, state, false);
}

void UdpCue::loop() {
  if (!_ready) {
    return;
  }
  pollIncoming();
}
