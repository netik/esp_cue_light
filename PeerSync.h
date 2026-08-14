#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "config.h"

class PeerSync {
public:
  bool begin();
  void loop();
  void setNetworkFilter(uint16_t systemId, uint16_t cueGroup);
  void requestSync();

private:
  struct PeerEntry {
    IPAddress ip;
    unsigned long lastSeenMs;
    bool valid;
  };

  uint16_t _systemId = DEFAULT_SYSTEM_ID;
  uint16_t _cueGroup = DEFAULT_CUE_GROUP;
  bool _ready = false;
  bool _syncUrgent = false;
  unsigned long _lastDiscoveryMs = 0;
  unsigned long _lastPollMs = 0;
  uint8_t _pollIndex = 0;

  PeerEntry _peers[PEER_SYNC_MAX_PEERS];

  void updateServiceTxt();
  void discoverPeers();
  void expireStalePeers();
  bool pollPeer(const PeerEntry& peer);
  bool parseAndApply(const char* json);
  void touchPeer(IPAddress ip);
  PeerEntry* findPeer(IPAddress ip);
  PeerEntry* allocPeer(IPAddress ip);
};

extern PeerSync peerSync;
