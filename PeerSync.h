#pragma once

#include "PlatformCompat.h"
#include "config.h"

struct CueSnapshot {
  uint16_t systemId = 0;
  uint16_t cueGroup = 0;
  uint8_t cue1 = 0;
  uint8_t cue2 = 0;
  uint32_t seq1 = 0;
  uint32_t seq2 = 0;
};

enum class CueTransport : uint8_t { Wifi, Lora };

class PeerSync {
public:
  bool begin();
  void loop();
  void setNetworkFilter(uint16_t systemId, uint16_t cueGroup);
  void notifyLocalChange();
  bool applyIncomingJson(const char* json);
  bool applyIncomingState(const CueSnapshot& snap, CueTransport source);
  void buildStateJson(char* buffer, size_t size) const;
  void fillSnapshot(CueSnapshot& snap) const;
  uint8_t countPeers() const;
#ifndef ARDUINO_ARCH_ESP32
  void handleMdnsAnswer(MDNSResponder::MDNSServiceInfo& serviceInfo,
                        MDNSResponder::AnswerType answerType, bool added);
#endif

private:
  struct PeerEntry {
    IPAddress ip;
    unsigned long lastSeenMs;
    bool valid;
  };

  struct MdnsEvent {
    IPAddress ip;
    bool added;
  };

  uint16_t _systemId = DEFAULT_SYSTEM_ID;
  uint16_t _cueGroup = DEFAULT_CUE_GROUP;
  bool _ready = false;
  bool _pushPending = false;
  bool _pushDeferred = false;
  bool _loraForwardPending = false;
  char _pushJson[128];
  uint8_t _pushScanIndex = 0;
  uint8_t _pushRemaining = 0;
  unsigned long _lastDiscoveryMs = 0;
  unsigned long _lastPollMs = 0;
  unsigned long _suppressPollUntilMs = 0;
  uint8_t _pollIndex = 0;
  volatile uint8_t _mdnsEventCount = 0;

#ifndef ARDUINO_ARCH_ESP32
  MDNSResponder::hMDNSService _mdnsService = 0;
  MDNSResponder::hMDNSServiceQuery _mdnsQuery = 0;
#endif

  PeerEntry _peers[PEER_SYNC_MAX_PEERS];
  MdnsEvent _mdnsEvents[PEER_SYNC_MDNS_EVENT_QUEUE];

  void updateServiceTxt();
  void schedulePush();
  void markLocalChange();
  void markInboundApply();
  void queueMdnsEvent(IPAddress ip, bool added);
  void processMdnsEvents();
  void refreshPeersFromMdns();
  void expireStalePeers();
  bool processPendingPush();
  bool pollPeer(const PeerEntry& peer);
  bool pushToPeer(const PeerEntry& peer, const char* json);
  bool parseJsonToSnapshot(const char* json, CueSnapshot& snap) const;
#ifndef ARDUINO_ARCH_ESP32
  bool peerMatchesFilter(MDNSResponder::MDNSServiceInfo& serviceInfo);
#endif
  void touchPeer(IPAddress ip);
  void removePeer(IPAddress ip);
  PeerEntry* findPeer(IPAddress ip);
  PeerEntry* allocPeer(IPAddress ip);
  static void formatIp(char* buffer, size_t size, const IPAddress& ip);
  static void formatPeerUrl(char* buffer, size_t size, const IPAddress& ip);
};

extern PeerSync peerSync;
