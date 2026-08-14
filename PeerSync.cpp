#include "PeerSync.h"

#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

#include "CueIO.h"

PeerSync peerSync;

namespace {
  const char* fieldValue(const char* json, const char* key) {
    char pattern[20];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char* start = strstr(json, pattern);
    if (start == nullptr) {
      return nullptr;
    }
  return start + strlen(pattern);
}

bool parseUintField(const char* json, const char* key, uint32_t* out,
                    uint32_t maxValue) {
  const char* value = fieldValue(json, key);
  if (value == nullptr) {
    return false;
  }

  char* end = nullptr;
  const unsigned long parsed = strtoul(value, &end, 10);
  if (end == value || parsed > maxValue) {
    return false;
  }

  *out = (uint32_t)parsed;
  return true;
}

bool isSeqNewer(uint32_t incoming, uint32_t current) {
  return incoming != current && (incoming - current) < 0x80000000UL;
}

void buildHostname(char* hostname, size_t size) {
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  snprintf(hostname, size, "CueLight-%02X%02X", mac[4], mac[5]);
}

void mdnsServiceQueryCallback(MDNSResponder::MDNSServiceInfo serviceInfo,
                              MDNSResponder::AnswerType answerType,
                              bool added) {
  peerSync.handleMdnsAnswer(serviceInfo, answerType, added);
}
}  // namespace

void PeerSync::formatIp(char* buffer, size_t size, const IPAddress& ip) {
  snprintf(buffer, size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void PeerSync::setNetworkFilter(uint16_t systemId, uint16_t cueGroup) {
  _systemId = systemId;
  _cueGroup = cueGroup;
  if (_ready) {
    updateServiceTxt();
  }
}

void PeerSync::updateServiceTxt() {
  if (_mdnsService == 0) {
    return;
  }

  char value[8];
  snprintf(value, sizeof(value), "%u", _systemId);
  MDNS.addServiceTxt(_mdnsService, "system_id", value);

  snprintf(value, sizeof(value), "%u", _cueGroup);
  MDNS.addServiceTxt(_mdnsService, "cue_group", value);
}

bool PeerSync::peerMatchesFilter(
    MDNSResponder::MDNSServiceInfo& serviceInfo) {
  if (!serviceInfo.txtAvailable()) {
    return true;
  }

  uint16_t peerSystemId = 0;
  uint16_t peerCueGroup = 0;
  bool hasSystemId = false;
  bool hasCueGroup = false;

  for (const auto& kv : serviceInfo.keyValues()) {
    if (strcmp(kv.first, "system_id") == 0) {
      peerSystemId = (uint16_t)atoi(kv.second);
      hasSystemId = true;
    } else if (strcmp(kv.first, "cue_group") == 0) {
      peerCueGroup = (uint16_t)atoi(kv.second);
      hasCueGroup = true;
    }
  }

  if (hasSystemId && peerSystemId != _systemId) {
    return false;
  }
  if (hasCueGroup && peerCueGroup != _cueGroup) {
    return false;
  }
  return true;
}

bool PeerSync::begin() {
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    Serial.print(F("Peer sync unavailable until WiFi is connected."));
    Serial.print(LINE_END);
    return false;
  }

  char hostname[20];
  buildHostname(hostname, sizeof(hostname));
  WiFi.hostname(hostname);

  if (!MDNS.begin(hostname)) {
    Serial.print(F("Peer sync: mDNS begin failed."));
    Serial.print(LINE_END);
    return false;
  }

  _mdnsService = MDNS.addService(0, CUE_MDNS_SERVICE, "tcp", CUE_HTTP_PORT);
  if (_mdnsService == 0) {
    Serial.print(F("Peer sync: mDNS addService failed."));
    Serial.print(LINE_END);
    return false;
  }

  updateServiceTxt();

  _mdnsQuery =
      MDNS.installServiceQuery(CUE_MDNS_SERVICE, "tcp", mdnsServiceQueryCallback);
  if (_mdnsQuery == 0) {
    Serial.print(F("Peer sync: mDNS installServiceQuery failed."));
    Serial.print(LINE_END);
    return false;
  }

  for (auto& peer : _peers) {
    peer.valid = false;
  }

  _ready = true;
  _lastDiscoveryMs = 0;
  _lastPollMs = 0;
  _pollIndex = 0;
  _pushPending = false;
  _pushDeferred = false;
  _pushRemaining = 0;
  _suppressPollUntilMs = 0;
  _mdnsEventCount = 0;

  char ipStr[16];
  formatIp(ipStr, sizeof(ipStr), WiFi.localIP());
  Serial.printf_P(PSTR("Peer sync ready: hostname=%s.local ip=%s system_id=%u cue_group=%u\r\n"),
                  hostname, ipStr, _systemId, _cueGroup);
  return true;
}

void PeerSync::buildStateJson(char* buffer, size_t size) const {
  snprintf(buffer, size,
           "{\"system_id\":%u,\"cue_group\":%u,\"cue1\":%u,\"cue2\":%u,"
           "\"seq1\":%u,\"seq2\":%u}",
           _systemId, _cueGroup, cueIO.getCueState(CUE_NUMBER_1),
           cueIO.getCueState(CUE_NUMBER_2), cueIO.getCueSeq(CUE_NUMBER_1),
           cueIO.getCueSeq(CUE_NUMBER_2));
}

bool PeerSync::applyIncomingJson(const char* json) {
  return parseAndApply(json, ApplySource::Push);
}

void PeerSync::markLocalChange() {
  _suppressPollUntilMs = millis() + PEER_SYNC_POLL_SUPPRESS_MS;
}

void PeerSync::markInboundApply() {
  _suppressPollUntilMs = millis() + PEER_SYNC_POLL_SUPPRESS_MS;
}

void PeerSync::notifyLocalChange() {
  if (!_ready) {
    return;
  }

  markLocalChange();
  buildStateJson(_pushJson, sizeof(_pushJson));

  const uint8_t peers = countPeers();
  if (peers == 0) {
    _pushDeferred = true;
    return;
  }

  schedulePush();
}

void PeerSync::formatPeerUrl(char* buffer, size_t size, const IPAddress& ip) {
  snprintf(buffer, size, "http://%u.%u.%u.%u/api/cues", ip[0], ip[1], ip[2],
           ip[3]);
}

void PeerSync::schedulePush() {
  const uint8_t peers = countPeers();
  if (peers == 0) {
    _pushDeferred = true;
    _pushPending = false;
    _pushRemaining = 0;
    return;
  }

  _pushPending = true;
  _pushDeferred = false;
  _pushScanIndex = 0;
  _pushRemaining = peers;
}

bool PeerSync::pushToPeer(const PeerEntry& peer, const char* json) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(PEER_SYNC_PUSH_TIMEOUT_MS);

  char url[48];
  formatPeerUrl(url, sizeof(url), peer.ip);
  if (!http.begin(client, url)) {
    return false;
  }

  http.addHeader(F("Content-Type"), F("application/json"));
  const int code = http.POST(json);
  http.end();
  return code == HTTP_CODE_OK;
}

bool PeerSync::processPendingPush() {
  if (!_pushPending) {
    return false;
  }

  if (_pushRemaining == 0) {
    _pushPending = false;
    return false;
  }

  for (uint8_t attempt = 0; attempt < PEER_SYNC_MAX_PEERS; ++attempt) {
    PeerEntry& peer = _peers[_pushScanIndex];
    _pushScanIndex = (_pushScanIndex + 1) % PEER_SYNC_MAX_PEERS;

    if (!peer.valid) {
      continue;
    }

    buildStateJson(_pushJson, sizeof(_pushJson));

    if (pushToPeer(peer, _pushJson)) {
      peer.lastSeenMs = millis();
      char ipStr[16];
      formatIp(ipStr, sizeof(ipStr), peer.ip);
      Serial.printf_P(PSTR("Peer sync: pushed to %s\r\n"), ipStr);
      --_pushRemaining;
    } else {
      char ipStr[16];
      formatIp(ipStr, sizeof(ipStr), peer.ip);
      Serial.printf_P(PSTR("Peer sync: push failed %s (will retry)\r\n"), ipStr);
    }

    cueIO.loop();

    if (_pushRemaining == 0) {
      _pushPending = false;
      markLocalChange();
    }

    yield();
    return true;
  }

  _pushPending = false;
  _pushRemaining = 0;
  return false;
}

PeerSync::PeerEntry* PeerSync::findPeer(IPAddress ip) {
  for (auto& peer : _peers) {
    if (peer.valid && peer.ip == ip) {
      return &peer;
    }
  }
  return nullptr;
}

PeerSync::PeerEntry* PeerSync::allocPeer(IPAddress ip) {
  PeerEntry* existing = findPeer(ip);
  if (existing != nullptr) {
    return existing;
  }

  for (auto& peer : _peers) {
    if (!peer.valid) {
      peer.valid = true;
      peer.ip = ip;
      peer.lastSeenMs = millis();
      char ipStr[16];
      formatIp(ipStr, sizeof(ipStr), ip);
      Serial.printf_P(PSTR("Peer sync: peer added %s\r\n"), ipStr);
      return &peer;
    }
  }

  Serial.print(F("Peer sync: peer table full."));
  Serial.print(LINE_END);
  return nullptr;
}

void PeerSync::touchPeer(IPAddress ip) {
  const bool hadPeers = countPeers() > 0;
  PeerEntry* peer = allocPeer(ip);
  if (peer != nullptr) {
    peer->lastSeenMs = millis();
  }

  if (_pushDeferred && !hadPeers && countPeers() > 0) {
    buildStateJson(_pushJson, sizeof(_pushJson));
    schedulePush();
  }
}

void PeerSync::removePeer(IPAddress ip) {
  PeerEntry* peer = findPeer(ip);
  if (peer == nullptr) {
    return;
  }

  char ipStr[16];
  formatIp(ipStr, sizeof(ipStr), ip);
  Serial.printf_P(PSTR("Peer sync: peer removed %s\r\n"), ipStr);
  peer->valid = false;
}

void PeerSync::queueMdnsEvent(IPAddress ip, bool added) {
  if (_mdnsEventCount >= PEER_SYNC_MDNS_EVENT_QUEUE) {
    return;
  }

  noInterrupts();
  _mdnsEvents[_mdnsEventCount].ip = ip;
  _mdnsEvents[_mdnsEventCount].added = added;
  ++_mdnsEventCount;
  interrupts();
}

void PeerSync::processMdnsEvents() {
  MdnsEvent events[PEER_SYNC_MDNS_EVENT_QUEUE];
  uint8_t count = 0;

  noInterrupts();
  count = _mdnsEventCount;
  for (uint8_t i = 0; i < count; ++i) {
    events[i] = _mdnsEvents[i];
  }
  _mdnsEventCount = 0;
  interrupts();

  for (uint8_t i = 0; i < count; ++i) {
    if (events[i].added) {
      touchPeer(events[i].ip);
    }
    // Ignore mDNS goodbye — transient flaps were removing active peers.
  }
}

uint8_t PeerSync::countPeers() const {
  uint8_t count = 0;
  for (const auto& peer : _peers) {
    if (peer.valid) {
      ++count;
    }
  }
  return count;
}

void PeerSync::handleMdnsAnswer(MDNSResponder::MDNSServiceInfo& serviceInfo,
                                MDNSResponder::AnswerType answerType,
                                bool added) {
  if (!_ready || answerType != MDNSResponder::AnswerType::IP4Address) {
    return;
  }

  const IPAddress selfIp = WiFi.localIP();
  for (const auto& ip : serviceInfo.IP4Adresses()) {
    if (ip == IPAddress(0, 0, 0, 0) || ip == selfIp) {
      continue;
    }
    queueMdnsEvent(ip, added);
  }
}

void PeerSync::refreshPeersFromMdns() {
  if (_mdnsQuery == 0) {
    return;
  }

  const IPAddress selfIp = WiFi.localIP();
  for (auto& info : MDNS.answerInfo(_mdnsQuery)) {
    if (!info.IP4AddressAvailable() || !peerMatchesFilter(info)) {
      continue;
    }

    for (const auto& ip : info.IP4Adresses()) {
      if (ip == IPAddress(0, 0, 0, 0) || ip == selfIp) {
        continue;
      }
      touchPeer(ip);
    }
  }

  Serial.printf_P(PSTR("Peer sync: mDNS refresh, tracking %u peer(s)\r\n"),
                  countPeers());
}

void PeerSync::expireStalePeers() {
  const unsigned long now = millis();
  for (auto& peer : _peers) {
    if (!peer.valid) {
      continue;
    }
    if ((now - peer.lastSeenMs) > PEER_SYNC_PEER_STALE_MS) {
      char ipStr[16];
      formatIp(ipStr, sizeof(ipStr), peer.ip);
      Serial.printf_P(PSTR("Peer sync: dropping stale peer %s\r\n"), ipStr);
      peer.valid = false;
    }
  }
}

bool PeerSync::parseAndApply(const char* json, ApplySource source) {
  if (source == ApplySource::Poll) {
    return false;
  }

  uint32_t systemId = 0;
  uint32_t cueGroup = 0;
  uint32_t cue1 = 0;
  uint32_t cue2 = 0;
  uint32_t seq1 = 0;
  uint32_t seq2 = 0;

  if (!parseUintField(json, "system_id", &systemId, UINT16_MAX) ||
      !parseUintField(json, "cue_group", &cueGroup, UINT16_MAX) ||
      !parseUintField(json, "cue1", &cue1, CUE_STATE_GREEN) ||
      !parseUintField(json, "cue2", &cue2, CUE_STATE_GREEN) ||
      !parseUintField(json, "seq1", &seq1, UINT32_MAX) ||
      !parseUintField(json, "seq2", &seq2, UINT32_MAX)) {
    return false;
  }

  if (systemId != _systemId || cueGroup != _cueGroup) {
    return false;
  }

  const uint32_t localSeq1 = cueIO.getCueSeq(CUE_NUMBER_1);
  const uint32_t localSeq2 = cueIO.getCueSeq(CUE_NUMBER_2);
  bool applied = false;

  if (isSeqNewer(seq1, localSeq1)) {
    cueIO.applyRemoteCueState(CUE_NUMBER_1, (uint8_t)cue1, seq1);
    applied = true;
  }

  if (isSeqNewer(seq2, localSeq2)) {
    cueIO.applyRemoteCueState(CUE_NUMBER_2, (uint8_t)cue2, seq2);
    applied = true;
  }

  if (applied) {
    markInboundApply();
  }

  return applied;
}

bool PeerSync::pollPeer(const PeerEntry& peer) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(PEER_SYNC_HTTP_TIMEOUT_MS);

  char url[48];
  formatPeerUrl(url, sizeof(url), peer.ip);
  if (!http.begin(client, url)) {
    char ipStr[16];
    formatIp(ipStr, sizeof(ipStr), peer.ip);
    Serial.printf_P(PSTR("Peer sync: HTTP begin failed for %s\r\n"), ipStr);
    return false;
  }

  const int code = http.GET();
  http.end();

  if (code != HTTP_CODE_OK) {
    char ipStr[16];
    formatIp(ipStr, sizeof(ipStr), peer.ip);
    Serial.printf_P(PSTR("Peer sync: HTTP %d from %s\r\n"), code, ipStr);
    return false;
  }

  char ipStr[16];
  formatIp(ipStr, sizeof(ipStr), peer.ip);
  Serial.printf_P(PSTR("Peer sync: polled %s OK\r\n"), ipStr);
  return true;
}

void PeerSync::loop() {
  if (!_ready) {
    return;
  }

  cueIO.loop();
  MDNS.update();
  processMdnsEvents();

  // Push always wins over discovery and poll.
  if (processPendingPush()) {
    return;
  }

  const unsigned long now = millis();

  if (_lastDiscoveryMs == 0 ||
      (now - _lastDiscoveryMs) >= PEER_SYNC_DISCOVERY_MS) {
    refreshPeersFromMdns();
    expireStalePeers();
    _lastDiscoveryMs = now;
    // Never poll in the same iteration as mDNS refresh — avoids racing a
    // background GET against a just-completed push snapshot.
    return;
  }

  if (_pushPending || _pushDeferred || now < _suppressPollUntilMs ||
      (now - _lastPollMs) < PEER_SYNC_POLL_INTERVAL_MS) {
    return;
  }

  const uint8_t validCount = countPeers();
  if (validCount == 0) {
    _lastPollMs = now;
    return;
  }

  uint8_t scanned = 0;
  while (scanned < validCount) {
    PeerEntry& peer = _peers[_pollIndex % PEER_SYNC_MAX_PEERS];
    _pollIndex = (_pollIndex + 1) % PEER_SYNC_MAX_PEERS;

    if (!peer.valid) {
      ++scanned;
      continue;
    }

    if (pollPeer(peer)) {
      peer.lastSeenMs = now;
    }

    cueIO.loop();
    yield();
    _lastPollMs = now;
    return;
  }

  _lastPollMs = now;
}
