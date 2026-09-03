/**
 * @file CueLora.cpp
 * @brief SX1262 broadcast of the same CueSnapshot used on HTTP (Heltec only).
 *
 * NodeMCU builds (@c CUE_HAS_LORA 0) compile empty stubs. Heltec packs a
 * 20-byte packet (magic, version, ids, cue states, seqs, node id, CRC-16),
 * beacons about every 5 s, and tracks recently heard radios for the OLED count.
 */

#include "CueLora.h"

#include "CueIO.h"

CueLora cueLora;

#if !CUE_HAS_LORA

/** @name NodeMCU stubs
 *  @{
 */
void CueLora::configure(bool, uint8_t, uint16_t, uint16_t) {}
void CueLora::loop() {}
void CueLora::sendState() {}
bool CueLora::takeReceived(CueSnapshot&) { return false; }
uint8_t CueLora::countHeard() const { return 0; }
void CueLora::end() {}
bool CueLora::begin() { return false; }
void CueLora::applyFrequency() {}
void CueLora::startRx() {}
void CueLora::serviceRx() {}
void CueLora::pack(uint8_t*) const {}
bool CueLora::unpack(const uint8_t*, CueSnapshot&, uint16_t*) const { return false; }
unsigned long CueLora::beaconJitter() const { return 0; }
/** @} */

#else

#include <RadioLib.h>
#include <SPI.h>

namespace {
/** RadioLib SX1262: NSS, DIO1, RST, BUSY (Heltec V3 pins 8, 14, 12, 13). */
SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST,
                          PIN_LORA_BUSY);
volatile bool g_rxFlag = false;    ///< Set in DIO1 ISR when a packet completes.
volatile bool g_ignoreRx = false;  ///< True during TX so our own packet is ignored.

/** @brief DIO1 packet-received ISR (ignored while transmitting). */
void IRAM_ATTR onLoraRx() {
  if (!g_ignoreRx) {
    g_rxFlag = true;
  }
}

/** @brief Little-endian uint16 store. */
void writeU16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)(v >> 8);
}

/** @brief Little-endian uint32 store. */
void writeU32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/** @brief Little-endian uint16 load. */
uint16_t readU16(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/** @brief Little-endian uint32 load. */
uint32_t readU32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

/**
 * @brief CRC-16/CCITT-FALSE (init 0xFFFF, poly 0x1021) over @p data.
 * @param data Bytes to hash.
 * @param len Length in bytes.
 * @return CRC of the buffer.
 */
uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x8000) {
        crc = (uint16_t)((crc << 1) ^ 0x1021);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}
}  // namespace

/**
 * @brief Serialize current CueIO state into a 20-byte LoRa payload.
 * @param[out] buf Buffer of at least @c LORA_PACKET_LEN bytes.
 *
 * Layout: magic, version, system_id, cue_group, cue1, cue2, seq1, seq2,
 * node_id, CRC-16 of bytes 0–17. Integers are little-endian.
 */
void CueLora::pack(uint8_t* buf) const {
  buf[0] = LORA_MAGIC;
  buf[1] = LORA_VERSION;
  writeU16(buf + 2, _systemId);
  writeU16(buf + 4, _cueGroup);
  buf[6] = cueIO.getCueState(CUE_NUMBER_1);
  buf[7] = cueIO.getCueState(CUE_NUMBER_2);
  writeU32(buf + 8, cueIO.getCueSeq(CUE_NUMBER_1));
  writeU32(buf + 12, cueIO.getCueSeq(CUE_NUMBER_2));
  writeU16(buf + 16, _selfId);
  writeU16(buf + 18, crc16(buf, 18));
}

/**
 * @brief Validate and decode a received payload.
 * @param buf Raw radio bytes (at least @c LORA_PACKET_LEN).
 * @param[out] out Snapshot if the packet is accepted.
 * @param[out] nodeId Optional sender id (MAC bytes 4–5).
 * @retval true Magic, version, CRC, cue values, and system/group match.
 */
bool CueLora::unpack(const uint8_t* buf, CueSnapshot& out, uint16_t* nodeId) const {
  if (buf[0] != LORA_MAGIC || buf[1] != LORA_VERSION) {
    return false;
  }
  if (readU16(buf + 18) != crc16(buf, 18)) {
    return false;
  }

  out.systemId = readU16(buf + 2);
  out.cueGroup = readU16(buf + 4);
  out.cue1 = buf[6];
  out.cue2 = buf[7];
  out.seq1 = readU32(buf + 8);
  out.seq2 = readU32(buf + 12);
  if (nodeId != nullptr) {
    *nodeId = readU16(buf + 16);
  }

  if (out.cue1 > CUE_STATE_GREEN || out.cue2 > CUE_STATE_GREEN) {
    return false;
  }
  if (out.systemId != _systemId || out.cueGroup != _cueGroup) {
    return false;
  }
  return true;
}

/**
 * @brief 0–999 ms extra delay so two boards do not beacon on the same tick.
 * @return Jitter derived from MAC[5] and @c millis().
 */
unsigned long CueLora::beaconJitter() const {
  uint8_t mac[6] = {0};
  cueWifiMacAddress(mac);
  return (unsigned long)((mac[5] * 37u + (millis() & 0xFFu)) % 1000u);
}

/**
 * @brief Retune SX1262 to 915.0 MHz + channel × 0.2 MHz.
 */
void CueLora::applyFrequency() {
  const float freq =
      LORA_FREQUENCY_MHZ + (float)_channel * LORA_CHANNEL_STEP_MHZ;
  const int16_t state = radio.setFrequency(freq);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf_P(PSTR("LoRa: setFrequency failed %d\r\n"), state);
  }
}

/**
 * @brief Arm DIO1 callback and put the radio in continuous receive.
 */
void CueLora::startRx() {
  g_ignoreRx = false;
  g_rxFlag = false;
  radio.setPacketReceivedAction(onLoraRx);
  const int16_t state = radio.startReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf_P(PSTR("LoRa: startReceive failed %d\r\n"), state);
  }
}

/**
 * @brief If DIO1 fired, read one packet, unpack, record the sender, queue apply.
 *
 * Restarts RX immediately so the next beacon is not missed. Invalid packets
 * are dropped silently.
 */
void CueLora::serviceRx() {
  if (!g_rxFlag) {
    return;
  }
  g_rxFlag = false;

  uint8_t buf[LORA_PACKET_LEN];
  const int16_t state = radio.readData(buf, LORA_PACKET_LEN);
  startRx();
  if (state != RADIOLIB_ERR_NONE) {
    return;
  }

  CueSnapshot snap;
  uint16_t nodeId = 0;
  if (!unpack(buf, snap, &nodeId)) {
    return;
  }

  touchHeard(nodeId);
  _pending = snap;
  _hasPending = true;
  Serial.printf_P(PSTR("LoRa: rx id=%04X cue1=%u seq1=%u cue2=%u seq2=%u\r\n"),
                  nodeId, snap.cue1, snap.seq1, snap.cue2, snap.seq2);
}

/**
 * @brief Bring up SPI, SX1262 (TCXO 1.8 V, DIO2 RF switch), and assign `_selfId`.
 * @retval true Radio is receiving.
 * @retval false @c radio.begin failed.
 */
bool CueLora::begin() {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);

  const float freq =
      LORA_FREQUENCY_MHZ + (float)_channel * LORA_CHANNEL_STEP_MHZ;
  const int16_t state =
      radio.begin(freq, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD,
                  LORA_POWER_DBM, LORA_PREAMBLE_LEN, LORA_TCXO_VOLTS, false);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf_P(PSTR("LoRa: begin failed %d\r\n"), state);
    _ready = false;
    return false;
  }

  radio.setDio2AsRfSwitch(true);
  radio.setCRC(2);

  uint8_t mac[6] = {0};
  cueWifiMacAddress(mac);
  _selfId = ((uint16_t)mac[4] << 8) | mac[5];
  if (_selfId == 0) {
    _selfId = (uint16_t)(ESP.getEfuseMac() & 0xFFFF);
  }
  if (_selfId == 0) {
    _selfId = 1;
  }
  clearHeard();

  _ready = true;
  _hasPending = false;
  _nextBeaconMs = millis() + LORA_BEACON_MS + beaconJitter();
  startRx();

  Serial.printf_P(PSTR("LoRa ready: %.1f MHz ch=%u SF%u id=%04X system_id=%u cue_group=%u\r\n"),
                  freq, _channel, (unsigned)LORA_SF, _selfId, _systemId, _cueGroup);
  return true;
}

/**
 * @brief Sleep the SX1262 and forget heard peers (power-off or LoRa disabled).
 */
void CueLora::end() {
  if (!_ready) {
    return;
  }

  radio.clearPacketReceivedAction();
  g_rxFlag = false;
  g_ignoreRx = true;
  radio.sleep(true);
  _ready = false;
  _hasPending = false;
  clearHeard();
  Serial.print(F("LoRa stopped."));
  Serial.print(LINE_END);
}

/**
 * @brief Apply /setup options: enable, channel, system_id, cue_group.
 * @param enabled If false, stop the radio.
 * @param channel Sub-band 0–7.
 * @param systemId Must match peers.
 * @param cueGroup Must match peers.
 *
 * Starts the radio on first enable; only retunes if already running and the
 * channel changed.
 */
void CueLora::configure(bool enabled, uint8_t channel, uint16_t systemId,
                        uint16_t cueGroup) {
  _systemId = systemId;
  _cueGroup = cueGroup;
  if (channel > LORA_CHANNEL_MAX) {
    channel = LORA_CHANNEL_MAX;
  }
  const bool channelChanged = channel != _channel;
  _channel = channel;

  if (!enabled) {
    end();
    return;
  }

  if (!_ready) {
    begin();
    return;
  }

  if (channelChanged) {
    applyFrequency();
    startRx();
    Serial.printf_P(PSTR("LoRa: channel %u (%.1f MHz)\r\n"), _channel,
                    LORA_FREQUENCY_MHZ + (float)_channel * LORA_CHANNEL_STEP_MHZ);
  }
}

/**
 * @brief CAD, then transmit the current snapshot; retry once on failure.
 *
 * Own RX is ignored during TX. Beacon timer is reset after every send.
 */
void CueLora::sendState() {
  if (!_ready) {
    return;
  }

  uint8_t buf[LORA_PACKET_LEN];
  pack(buf);

  g_ignoreRx = true;
  g_rxFlag = false;

  const int16_t cad = radio.scanChannel();
  if (cad == RADIOLIB_LORA_DETECTED) {
    delay(LORA_CAD_BACKOFF_MS);
  }

  int16_t state = radio.transmit(buf, LORA_PACKET_LEN);
  if (state != RADIOLIB_ERR_NONE) {
    delay(LORA_CAD_BACKOFF_MS);
    state = radio.transmit(buf, LORA_PACKET_LEN);
  }

  g_rxFlag = false;
  startRx();
  _nextBeaconMs = millis() + LORA_BEACON_MS + beaconJitter();

  if (state == RADIOLIB_ERR_NONE) {
    Serial.print(F("LoRa: tx"));
    Serial.print(LINE_END);
  } else {
    Serial.printf_P(PSTR("LoRa: tx failed %d\r\n"), state);
  }
}

/**
 * @brief Pop one queued RX snapshot for @ref PeerSync::loop.
 * @param[out] out Copy of the last valid packet.
 * @retval true A snapshot was pending.
 */
bool CueLora::takeReceived(CueSnapshot& out) {
  if (!_hasPending) {
    return false;
  }
  out = _pending;
  _hasPending = false;
  return true;
}

/**
 * @brief Service RX, expire stale heard ids, and beacon when due.
 */
void CueLora::loop() {
  if (!_ready) {
    return;
  }

  serviceRx();
  expireHeard();

  const unsigned long now = millis();
  if (_nextBeaconMs == 0) {
    _nextBeaconMs = now + LORA_BEACON_MS + beaconJitter();
    return;
  }
  if ((long)(now - _nextBeaconMs) >= 0) {
    sendState();
  }
}

/**
 * @brief Number of distinct radios heard within @c LORA_HEARD_STALE_MS.
 * @return Count in 0 .. @c LORA_HEARD_MAX (does not include this board).
 */
uint8_t CueLora::countHeard() const {
  uint8_t n = 0;
  for (const auto& e : _heard) {
    if (e.valid) {
      ++n;
    }
  }
  return n;
}

/** @brief Invalidate the heard-peer table. */
void CueLora::clearHeard() {
  for (auto& e : _heard) {
    e.valid = false;
    e.id = 0;
    e.lastSeenMs = 0;
  }
}

/**
 * @brief Note that @p id was heard now; insert or refresh a table slot.
 * @param id Sender node_id; ignored if it is this board (`_selfId`).
 */
void CueLora::touchHeard(uint16_t id) {
  if (id == _selfId) {
    return;
  }

  const unsigned long now = millis();
  for (auto& e : _heard) {
    if (e.valid && e.id == id) {
      e.lastSeenMs = now;
      return;
    }
  }
  for (auto& e : _heard) {
    if (!e.valid) {
      e.valid = true;
      e.id = id;
      e.lastSeenMs = now;
      Serial.printf_P(PSTR("LoRa: peer %04X\r\n"), id);
      return;
    }
  }
}

/**
 * @brief Drop heard entries not refreshed for @c LORA_HEARD_STALE_MS.
 */
void CueLora::expireHeard() {
  const unsigned long now = millis();
  for (auto& e : _heard) {
    if (e.valid && (now - e.lastSeenMs) > LORA_HEARD_STALE_MS) {
      Serial.printf_P(PSTR("LoRa: peer %04X stale\r\n"), e.id);
      e.valid = false;
    }
  }
}

#endif
