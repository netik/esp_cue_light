/**
 * @file PeerSync.cpp
 * @brief mDNS discovery, HTTP POST push, GET poll, and LoRa/WiFi relay.
 *
 * Boards are equals. A local change POSTs @c /api/cues to known peers and
 * transmits LoRa. Incoming snapshots apply when the per-cue sequence is newer.
 * WiFi applies are forwarded to LoRa; LoRa applies are POSTed to HTTP peers.
 */

#include "PeerSync.h"

#include "CueIO.h"
#include "CueLora.h"

PeerSync peerSync;

namespace {
/**
 * @brief Pointer to the characters after `"key":` in a compact JSON object.
 * @param json Entire JSON string.
 * @param key Field name without quotes.
 * @return Pointer at the value, or nullptr if the key is missing.
 */
  const char* fieldValue(const char* json, const char* key) {
    char pattern[20];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char* start = strstr(json, pattern);
    if (start == nullptr) {
      return nullptr;
    }
  return start + strlen(pattern);
}

/**
 * @brief Parse an unsigned integer JSON field with an inclusive max.
 * @param json Entire JSON string.
 * @param key Field name.
 * @param[out] out Parsed value.
 * @param maxValue Reject if the number exceeds this.
 * @retval true Field present and in range.
 */
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

/**
 * @brief Sequence comparison that treats wrap-around as unsigned 32-bit.
 * @retval true @p incoming is different and not more than 2^31 behind @p current.
 */
bool isSeqNewer(uint32_t incoming, uint32_t current) {
  return incoming != current && (incoming - current) < 0x80000000UL;
}

/**
 * @brief mDNS / STA hostname: same string as the setup AP SSID (@c CueLight-XXXX).
 */
void buildHostname(char* hostname, size_t size) {
  cueDefaultSsid(hostname, size);
}

#ifndef ARDUINO_ARCH_ESP32
/**
 * @brief LEAmDNS query callback; forwards IPv4 answers to @ref PeerSync.
 */
void mdnsServiceQueryCallback(MDNSResponder::MDNSServiceInfo serviceInfo,
                              MDNSResponder::AnswerType answerType,
                              bool added) {
  peerSync.handleMdnsAnswer(serviceInfo, answerType, added);
}
#endif
}  // namespace

/**
 * @brief Format dotted-quad IPv4 into @p buffer.
 */
void PeerSync::formatIp(char* buffer, size_t size, const IPAddress& ip) {
  snprintf(buffer, size, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

/**
 * @brief Store system_id / cue_group and refresh mDNS TXT if already advertising.
 */
void PeerSync::setNetworkFilter(uint16_t systemId, uint16_t cueGroup) {
  _systemId = systemId;
  _cueGroup = cueGroup;
  if (_ready) {
    updateServiceTxt();
  }
}

/**
 * @brief Publish @c system_id and @c cue_group TXT on `_cuelight._tcp`.
 */
void PeerSync::updateServiceTxt() {
#ifdef ARDUINO_ARCH_ESP32
  char value[8];
  snprintf(value, sizeof(value), "%u", _systemId);
  MDNS.addServiceTxt(String(CUE_MDNS_SERVICE), String("tcp"), String("system_id"),
                     String(value));
  snprintf(value, sizeof(value), "%u", _cueGroup);
  MDNS.addServiceTxt(String(CUE_MDNS_SERVICE), String("tcp"), String("cue_group"),
                     String(value));
#else
  if (_mdnsService == 0) {
    return;
  }

  char value[8];
  snprintf(value, sizeof(value), "%u", _systemId);
  MDNS.addServiceTxt(_mdnsService, "system_id", value);

  snprintf(value, sizeof(value), "%u", _cueGroup);
  MDNS.addServiceTxt(_mdnsService, "cue_group", value);
#endif
}

#ifndef ARDUINO_ARCH_ESP32
/**
 * @brief True if the peer's TXT is missing or matches our system_id / cue_group.
 */
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
#endif

/**
 * @brief Start mDNS hostname + service once STA has an IP.
 * @retval false Not on WiFi, or mDNS begin/addService failed.
 */
bool PeerSync::begin() {
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    Serial.print(F("Peer sync unavailable until WiFi is connected."));
    Serial.print(LINE_END);
    return false;
  }

  char hostname[20];
  buildHostname(hostname, sizeof(hostname));
  cueWifiSetHostname(hostname);

  if (!MDNS.begin(hostname)) {
    Serial.print(F("Peer sync: mDNS begin failed."));
    Serial.print(LINE_END);
    return false;
  }

#ifdef ARDUINO_ARCH_ESP32
  if (!MDNS.addService(CUE_MDNS_SERVICE, "tcp", CUE_HTTP_PORT)) {
    Serial.print(F("Peer sync: mDNS addService failed."));
    Serial.print(LINE_END);
    return false;
  }
  updateServiceTxt();
#else
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
#endif

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

/**
 * @brief Copy live CueIO state plus our network filter into @p snap.
 */
void PeerSync::fillSnapshot(CueSnapshot& snap) const {
  snap.systemId = _systemId;
  snap.cueGroup = _cueGroup;
  snap.cue1 = cueIO.getCueState(CUE_NUMBER_1);
  snap.cue2 = cueIO.getCueState(CUE_NUMBER_2);
  snap.seq1 = cueIO.getCueSeq(CUE_NUMBER_1);
  snap.seq2 = cueIO.getCueSeq(CUE_NUMBER_2);
}

/**
 * @brief Compact JSON used by GET /api/cues and HTTP peer push.
 * @param[out] buffer Destination.
 * @param size Capacity of @p buffer.
 */
void PeerSync::buildStateJson(char* buffer, size_t size) const {
  CueSnapshot snap;
  fillSnapshot(snap);
  snprintf(buffer, size,
           "{\"system_id\":%u,\"cue_group\":%u,\"cue1\":%u,\"cue2\":%u,"
           "\"seq1\":%u,\"seq2\":%u}",
           snap.systemId, snap.cueGroup, snap.cue1, snap.cue2, snap.seq1,
           snap.seq2);
}

/**
 * @brief Parse HTTP body JSON and apply as a WiFi-sourced snapshot.
 * @retval true At least one cue seq was newer and applied.
 */
bool PeerSync::applyIncomingJson(const char* json) {
  CueSnapshot snap;
  if (!parseJsonToSnapshot(json, snap)) {
    return false;
  }
  return applyIncomingState(snap, CueTransport::Wifi);
}

/**
 * @brief Suppress background GET poll briefly after a local or inbound change.
 */
void PeerSync::markLocalChange() {
  _suppressPollUntilMs = millis() + PEER_SYNC_POLL_SUPPRESS_MS;
}

/** @brief Same poll suppress as @ref markLocalChange after a remote apply. */
void PeerSync::markInboundApply() {
  _suppressPollUntilMs = millis() + PEER_SYNC_POLL_SUPPRESS_MS;
}

/**
 * @brief Local button path: LoRa TX immediately, HTTP push if peers exist.
 *
 * If the peer table is empty, sets `_pushDeferred` until mDNS finds someone.
 */
void PeerSync::notifyLocalChange() {
  markLocalChange();
  cueLora.sendState();

  if (!_ready) {
    return;
  }

  buildStateJson(_pushJson, sizeof(_pushJson));

  const uint8_t peers = countPeers();
  if (peers == 0) {
    _pushDeferred = true;
    return;
  }

  schedulePush();
}

/**
 * @brief Build `http://a.b.c.d/api/cues` for a peer.
 */
void PeerSync::formatPeerUrl(char* buffer, size_t size, const IPAddress& ip) {
  snprintf(buffer, size, "http://%u.%u.%u.%u/api/cues", ip[0], ip[1], ip[2],
           ip[3]);
}

/**
 * @brief Arm a round-robin HTTP POST of current state to every valid peer.
 */
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

/**
 * @brief POST JSON snapshot to one peer.
 * @retval true HTTP 200.
 */
bool PeerSync::pushToPeer(const PeerEntry& peer, const char* json) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(PEER_SYNC_PUSH_TIMEOUT_MS);

  char url[48];
  formatPeerUrl(url, sizeof(url), peer.ip);
  if (!cueHttpBegin(http, client, url)) {
    return false;
  }

  http.addHeader(F("Content-Type"), F("application/json"));
  const int code = http.POST(json);
  http.end();
  return code == HTTP_CODE_OK;
}

/**
 * @brief Send at most one POST per loop iteration so buttons stay responsive.
 * @retval true A push attempt ran (caller should skip poll this tick).
 */
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

/**
 * @brief Find a valid table slot with this IP.
 * @return Pointer or nullptr.
 */
PeerSync::PeerEntry* PeerSync::findPeer(IPAddress ip) {
  for (auto& peer : _peers) {
    if (peer.valid && peer.ip == ip) {
      return &peer;
    }
  }
  return nullptr;
}

/**
 * @brief Return existing peer or occupy a free slot (max @c PEER_SYNC_MAX_PEERS).
 */
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

/**
 * @brief Mark @p ip seen now; flush a deferred push if this is the first peer.
 */
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

/**
 * @brief Invalidate a peer slot (unused for mDNS goodbye; flaps were dropping live boards).
 */
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

/**
 * @brief Queue an mDNS IPv4 event from ISR/callback context (ESP8266).
 */
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

/**
 * @brief Drain queued mDNS events; additions call @ref touchPeer.
 */
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

/**
 * @brief Count valid WiFi mDNS peers (not LoRa heard radios).
 */
uint8_t PeerSync::countPeers() const {
  uint8_t count = 0;
  for (const auto& peer : _peers) {
    if (peer.valid) {
      ++count;
    }
  }
  return count;
}

#ifndef ARDUINO_ARCH_ESP32
/**
 * @brief ESP8266 LEAmDNS IPv4 callback: enqueue other boards' addresses.
 */
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
#endif

/**
 * @brief Browse `_cuelight._tcp` and refresh the peer table (ESP32 query or LEAmDNS).
 *
 * Skips self IP and TXT that does not match system_id / cue_group.
 */
void PeerSync::refreshPeersFromMdns() {
#ifdef ARDUINO_ARCH_ESP32
  mdns_result_t* results = nullptr;
  const esp_err_t err =
      mdns_query_ptr("_" CUE_MDNS_SERVICE, "_tcp", PEER_SYNC_MDNS_QUERY_MS,
                     PEER_SYNC_MAX_PEERS, &results);
  if (err != ESP_OK || results == nullptr) {
    Serial.printf_P(PSTR("Peer sync: mDNS refresh, tracking %u peer(s)\r\n"),
                    countPeers());
    return;
  }

  const IPAddress selfIp = WiFi.localIP();
  for (mdns_result_t* r = results; r != nullptr; r = r->next) {
    bool match = true;
    for (size_t i = 0; i < r->txt_count; ++i) {
      if (r->txt[i].key == nullptr || r->txt[i].value == nullptr) {
        continue;
      }
      if (strcmp(r->txt[i].key, "system_id") == 0 &&
          (uint16_t)atoi(r->txt[i].value) != _systemId) {
        match = false;
        break;
      }
      if (strcmp(r->txt[i].key, "cue_group") == 0 &&
          (uint16_t)atoi(r->txt[i].value) != _cueGroup) {
        match = false;
        break;
      }
    }
    if (!match) {
      continue;
    }

    for (mdns_ip_addr_t* addr = r->addr; addr != nullptr; addr = addr->next) {
      if (addr->addr.type != MDNS_IP_PROTOCOL_V4) {
        continue;
      }
      const IPAddress ip(addr->addr.u_addr.ip4.addr);
      if (ip == IPAddress(0, 0, 0, 0) || ip == selfIp) {
        continue;
      }
      touchPeer(ip);
    }
  }

  mdns_query_results_free(results);
#else
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
#endif

  Serial.printf_P(PSTR("Peer sync: mDNS refresh, tracking %u peer(s)\r\n"),
                  countPeers());
}

/**
 * @brief Drop WiFi peers not seen for @c PEER_SYNC_PEER_STALE_MS.
 */
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

/**
 * @brief Parse the six cue-state fields from a compact JSON object.
 * @retval false Any field missing or out of range.
 */
bool PeerSync::parseJsonToSnapshot(const char* json, CueSnapshot& snap) const {
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

  snap.systemId = (uint16_t)systemId;
  snap.cueGroup = (uint16_t)cueGroup;
  snap.cue1 = (uint8_t)cue1;
  snap.cue2 = (uint8_t)cue2;
  snap.seq1 = seq1;
  snap.seq2 = seq2;
  return true;
}

/**
 * @brief Apply newer seqs from @p snap; relay onto the other transport.
 * @param source @c Wifi → schedule LoRa TX; @c Lora → HTTP push to peers.
 * @retval true At least one cue was updated.
 */
bool PeerSync::applyIncomingState(const CueSnapshot& snap, CueTransport source) {
  if (snap.systemId != _systemId || snap.cueGroup != _cueGroup) {
    return false;
  }

  const uint32_t localSeq1 = cueIO.getCueSeq(CUE_NUMBER_1);
  const uint32_t localSeq2 = cueIO.getCueSeq(CUE_NUMBER_2);
  bool applied = false;

  if (isSeqNewer(snap.seq1, localSeq1)) {
    cueIO.applyRemoteCueState(CUE_NUMBER_1, snap.cue1, snap.seq1);
    applied = true;
  }

  if (isSeqNewer(snap.seq2, localSeq2)) {
    cueIO.applyRemoteCueState(CUE_NUMBER_2, snap.cue2, snap.seq2);
    applied = true;
  }

  if (!applied) {
    return false;
  }

  markInboundApply();

  if (source == CueTransport::Wifi) {
    _loraForwardPending = true;
  } else if (source == CueTransport::Lora) {
    buildStateJson(_pushJson, sizeof(_pushJson));
    if (_ready) {
      schedulePush();
    } else {
      _pushDeferred = true;
    }
  }

  return true;
}

/**
 * @brief GET /api/cues from one peer and apply if sequences are newer.
 * @retval true HTTP 200 and a well-formed body (apply may still be a no-op).
 */
bool PeerSync::pollPeer(const PeerEntry& peer) {
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(PEER_SYNC_HTTP_TIMEOUT_MS);

  char url[48];
  formatPeerUrl(url, sizeof(url), peer.ip);
  if (!cueHttpBegin(http, client, url)) {
    char ipStr[16];
    formatIp(ipStr, sizeof(ipStr), peer.ip);
    Serial.printf_P(PSTR("Peer sync: HTTP begin failed for %s\r\n"), ipStr);
    return false;
  }

  const int code = http.GET();

  if (code != HTTP_CODE_OK) {
    char ipStr[16];
    formatIp(ipStr, sizeof(ipStr), peer.ip);
    Serial.printf_P(PSTR("Peer sync: HTTP %d from %s\r\n"), code, ipStr);
    http.end();
    return false;
  }

  char json[128];
  auto* stream = http.getStreamPtr();
  if (stream == nullptr) {
    http.end();
    return false;
  }

  const size_t maxRead = sizeof(json) - 1;
  const size_t n = stream->readBytes(json, maxRead);
  json[n] = '\0';
  const bool oversized = (n == maxRead && stream->available() > 0);
  http.end();

  if (n == 0 || oversized) {
    char ipStr[16];
    formatIp(ipStr, sizeof(ipStr), peer.ip);
    Serial.printf_P(PSTR("Peer sync: empty/oversized response from %s\r\n"), ipStr);
    return false;
  }
  CueSnapshot snap;
  const bool applied = parseJsonToSnapshot(json, snap) &&
                       applyIncomingState(snap, CueTransport::Wifi);

  char ipStr[16];
  formatIp(ipStr, sizeof(ipStr), peer.ip);
  if (applied) {
    Serial.printf_P(PSTR("Peer sync: polled %s, state applied\r\n"), ipStr);
  } else {
    Serial.printf_P(PSTR("Peer sync: polled %s OK\r\n"), ipStr);
  }
  return true;
}

/**
 * @brief Main sync tick: LoRa RX/TX relay, push, mDNS refresh, then one GET poll.
 *
 * Push always wins over discovery and poll. mDNS refresh and poll do not share
 * the same iteration.
 */
void PeerSync::loop() {
  cueLora.loop();

  CueSnapshot loraSnap;
  if (cueLora.takeReceived(loraSnap)) {
    applyIncomingState(loraSnap, CueTransport::Lora);
  }

  if (_loraForwardPending) {
    _loraForwardPending = false;
    cueLora.sendState();
  }

  if (!_ready) {
    return;
  }

  cueIO.loop();
#ifndef ARDUINO_ARCH_ESP32
  MDNS.update();
  processMdnsEvents();
#endif

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
