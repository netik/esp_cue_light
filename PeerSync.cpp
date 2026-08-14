#include "PeerSync.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
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
}  // namespace

void PeerSync::setNetworkFilter(uint16_t systemId, uint16_t cueGroup) {
  _systemId = systemId;
  _cueGroup = cueGroup;
  if (_ready) {
    updateServiceTxt();
  }
}

void PeerSync::updateServiceTxt() {
  char value[8];

  snprintf(value, sizeof(value), "%u", _systemId);
  MDNS.addServiceTxt(CUE_MDNS_SERVICE, "tcp", "system_id", value);

  snprintf(value, sizeof(value), "%u", _cueGroup);
  MDNS.addServiceTxt(CUE_MDNS_SERVICE, "tcp", "cue_group", value);
}

bool PeerSync::begin() {
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    Serial.print(F("Peer sync unavailable until WiFi is connected."));
    Serial.print(LINE_END);
    return false;
  }

  char hostname[20];
  buildHostname(hostname, sizeof(hostname));

  if (!MDNS.begin(hostname)) {
    Serial.print(F("Peer sync: mDNS begin failed."));
    Serial.print(LINE_END);
    return false;
  }

  MDNS.addService(CUE_MDNS_SERVICE, "tcp", CUE_HTTP_PORT);
  updateServiceTxt();

  for (auto& peer : _peers) {
    peer.valid = false;
  }

  _ready = true;
  _syncUrgent = true;
  _lastDiscoveryMs = 0;
  _lastPollMs = 0;
  _pollIndex = 0;

  Serial.printf_P(PSTR("Peer sync ready: hostname=%s system_id=%u cue_group=%u\r\n"),
                  hostname, _systemId, _cueGroup);
  return true;
}

void PeerSync::requestSync() {
  _syncUrgent = true;
  _lastPollMs = 0;
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
      return &peer;
    }
  }

  return nullptr;
}

void PeerSync::touchPeer(IPAddress ip) {
  PeerEntry* peer = allocPeer(ip);
  if (peer != nullptr) {
    peer->lastSeenMs = millis();
  }
}

void PeerSync::discoverPeers() {
  const IPAddress selfIp = WiFi.localIP();
  const int count = MDNS.queryService(CUE_MDNS_SERVICE, "tcp");

  for (int i = 0; i < count; ++i) {
    const IPAddress ip = MDNS.IP(i);
    if (ip == IPAddress(0, 0, 0, 0) || ip == selfIp) {
      continue;
    }
    touchPeer(ip);
  }

  Serial.printf_P(PSTR("Peer sync: discovered %d mDNS result(s), tracking peers\r\n"),
                  count);
}

void PeerSync::expireStalePeers() {
  const unsigned long now = millis();
  for (auto& peer : _peers) {
    if (!peer.valid) {
      continue;
    }
    if ((now - peer.lastSeenMs) > PEER_SYNC_PEER_STALE_MS) {
      Serial.printf_P(PSTR("Peer sync: dropping stale peer %s\r\n"),
                    peer.ip.toString().c_str());
      peer.valid = false;
    }
  }
}

bool PeerSync::parseAndApply(const char* json) {
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

  bool applied = false;

  if (isSeqNewer(seq1, cueIO.getCueSeq(CUE_NUMBER_1))) {
    cueIO.applyRemoteCueState(CUE_NUMBER_1, (uint8_t)cue1, seq1);
    applied = true;
  }

  if (isSeqNewer(seq2, cueIO.getCueSeq(CUE_NUMBER_2))) {
    cueIO.applyRemoteCueState(CUE_NUMBER_2, (uint8_t)cue2, seq2);
    applied = true;
  }

  return applied;
}

bool PeerSync::pollPeer(const PeerEntry& peer) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(PEER_SYNC_HTTP_TIMEOUT_MS);

  const String url = String(F("http://")) + peer.ip.toString() + F("/api/cues");
  if (!http.begin(client, url)) {
    Serial.printf_P(PSTR("Peer sync: HTTP begin failed for %s\r\n"),
                    peer.ip.toString().c_str());
    return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf_P(PSTR("Peer sync: HTTP %d from %s\r\n"), code,
                    peer.ip.toString().c_str());
    http.end();
    return false;
  }

  char buffer[160];
  const size_t len = http.getStreamPtr()->readBytes(
      buffer, sizeof(buffer) - 1);
  http.end();

  if (len == 0) {
    return false;
  }

  buffer[len] = '\0';

  const bool applied = parseAndApply(buffer);
  Serial.printf_P(PSTR("Peer sync: polled %s %s\r\n"),
                  peer.ip.toString().c_str(), applied ? PSTR("applied") : PSTR("OK"));
  return true;
}

void PeerSync::loop() {
  if (!_ready) {
    return;
  }

  const unsigned long now = millis();

  if (_lastDiscoveryMs == 0 ||
      (now - _lastDiscoveryMs) >= PEER_SYNC_DISCOVERY_MS) {
    discoverPeers();
    expireStalePeers();
    _lastDiscoveryMs = now;
  }

  if (!_syncUrgent && (now - _lastPollMs) < PEER_SYNC_POLL_INTERVAL_MS) {
    return;
  }

  uint8_t validCount = 0;
  for (const auto& peer : _peers) {
    if (peer.valid) {
      ++validCount;
    }
  }

  if (validCount == 0) {
    _lastPollMs = now;
    _syncUrgent = false;
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

    _lastPollMs = now;
    _syncUrgent = false;
    return;
  }

  _lastPollMs = now;
  _syncUrgent = false;
}
