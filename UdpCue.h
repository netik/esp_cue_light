#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "config.h"

class UdpCue {
public:
  bool begin();
  void loop();
  void setNetworkFilter(uint16_t systemId, uint16_t cueGroup);

  bool broadcastCue(uint8_t cueNumber, uint8_t state);

private:
  uint16_t _systemId = DEFAULT_SYSTEM_ID;
  uint16_t _cueGroup = DEFAULT_CUE_GROUP;
  bool _ready = false;

  bool buildMessage(uint8_t cueNumber, uint8_t state, char* buffer,
                    size_t size) const;
  void pollIncoming();
  void handleReceived(const IPAddress& remoteIp, const char* data, size_t len);
};

extern UdpCue udpCue;
