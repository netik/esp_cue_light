#pragma once

#include "PeerSync.h"
#include "config.h"

class CueLora {
public:
  void configure(bool enabled, uint8_t channel, uint16_t systemId,
                 uint16_t cueGroup);
  void loop();
  void sendState();
  bool takeReceived(CueSnapshot& out);
  bool isReady() const { return _ready; }
  uint8_t countHeard() const;
  void end();

private:
  struct HeardEntry {
    uint16_t id;
    unsigned long lastSeenMs;
    bool valid;
  };

  bool _ready = false;
  bool _hasPending = false;
  uint8_t _channel = 0;
  uint16_t _systemId = DEFAULT_SYSTEM_ID;
  uint16_t _cueGroup = DEFAULT_CUE_GROUP;
  uint16_t _selfId = 0;
  unsigned long _nextBeaconMs = 0;
  CueSnapshot _pending;
  HeardEntry _heard[LORA_HEARD_MAX];

  bool begin();
  void applyFrequency();
  void startRx();
  void serviceRx();
  void pack(uint8_t* buf) const;
  bool unpack(const uint8_t* buf, CueSnapshot& out, uint16_t* nodeId) const;
  void touchHeard(uint16_t id);
  void expireHeard();
  void clearHeard();
  unsigned long beaconJitter() const;
};

extern CueLora cueLora;
