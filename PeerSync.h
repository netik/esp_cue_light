#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include "config.h"

class PeerSync {
public:
  bool begin();
  void loop();
  void setNetworkFilter(uint16_t systemId, uint16_t cueGroup);
  void notifyLocalChange();
  bool applyIncomingJson(const char* json);
  void buildStateJson(char* buffer, size_t size) const;
  void handleMdnsAnswer(MDNSResponder::MDNSServiceInfo& serviceInfo,
                        MDNSResponder::AnswerType answerType, bool added);

private:
  struct PeerEntry {
    IPAddress ip;
    unsigned long lastSeenMs;
    bool valid;
  };

  uint16_t _systemId = DEFAULT_SYSTEM_ID;
  uint16_t _cueGroup = DEFAULT_CUE_GROUP;
  bool _ready = false;
  bool _pushPending = false;
  char _pushJson[128];
  uint8_t _pushScanIndex = 0;
  uint8_t _pushRemaining = 0;
  unsigned long _lastDiscoveryMs = 0;
  unsigned long _lastPollMs = 0;
  uint8_t _pollIndex = 0;

  MDNSResponder::hMDNSService _mdnsService = 0;
  MDNSResponder::hMDNSServiceQuery _mdnsQuery = 0;

  PeerEntry _peers[PEER_SYNC_MAX_PEERS];

  void updateServiceTxt();
  void refreshPeersFromMdns();
  void expireStalePeers();
  bool processPendingPush();
  bool pollPeer(const PeerEntry& peer);
  bool pushToPeer(const PeerEntry& peer, const char* json);
  bool parseAndApply(const char* json);
  bool peerMatchesFilter(MDNSResponder::MDNSServiceInfo& serviceInfo);
  void touchPeer(IPAddress ip);
  PeerEntry* findPeer(IPAddress ip);
  PeerEntry* allocPeer(IPAddress ip);
  uint8_t countPeers() const;
  static void formatPeerUrl(char* buffer, size_t size, const IPAddress& ip);
};

extern PeerSync peerSync;
