# Cue Light — documentation

Technical documentation for the cue-light firmware (**v1.5.0**). NodeMCU (ESP8266) and Heltec WiFi LoRa 32 V3 (ESP32-S3) share this tree; the Heltec variant is selected by FQBN.

For building, flashing, hardware pins, and WiFi setup, start with the **[main README](../README.md)**.

---

## Reading guide

**Code reference (Doxygen)**

```bash
make docs          # HTML → docs/code/index.html
```

Requires [Doxygen](https://www.doxygen.nl/). Generated HTML is gitignored.

**Setting up boards for a show**

1. [Main README — Quick start](../README.md#quick-start)
2. [Operations guide](./operations-guide.md) — deployment checklist and troubleshooting

**Understanding how sync works**

1. [Peer sync architecture](./peer-sync-architecture.md) — why UDP was removed, push + poll design, timing
2. [Network protocol](./network-protocol.md) — HTTP API, mDNS, integration examples

**Integrating from a laptop or script**

1. [Network protocol — External integration](./network-protocol.md#external-integration)
2. [Network protocol — `GET /api/cues`](./network-protocol.md#get-apicues)

---

## Documents

| Document | Contents |
|----------|----------|
| [USER_MANUAL.md](../USER_MANUAL.md) | Operator guide: buttons, OLED, WiFi, LoRa |
| [Code API (generated)](./code/index.html) | Doxygen HTML (`make docs`) |
| [Peer sync architecture](./peer-sync-architecture.md) | Problem statement, peer-equality model, push + poll sync, LoRa relay, sequence numbers, timing, failure modes |
| [Network protocol](./network-protocol.md) | mDNS service record, `GET`/`POST /api/cues`, LoRa packet, sync algorithm, Python examples |
| [Operations guide](./operations-guide.md) | Deployment checklist, serial monitor output, tuning, troubleshooting |

---

## Sync model (summary)

Boards are **peers**. There is no leader and no MQTT broker.

| Step | Mechanism |
|------|-----------|
| Announce | Each board registers `_cuelight._tcp` via LEAmDNS on port 80 |
| Discover | `MDNS.installServiceQuery()` + `MDNS.update()` in loop; peer table refreshed every 15 s |
| Fast sync | `POST /api/cues` to all known peers immediately on local button press; Heltec LoRa TX if enabled |
| Fallback sync | `GET /api/cues` on one peer every 500 ms (round-robin); LoRa beacon ~5 s |
| Conflict resolution | Per-cue sequence numbers — highest seq wins |
| Late join | New board discovers peers via mDNS, polls, and adopts current state; LoRa beacons fill radio-only Heltecs |
| Relay | Heltec with Enable LoRa + STA WiFi forwards applied state to the other transport |
| LoRa only | Heltec **Enable WiFi** off: no STA/AP, no HTTP; SX1262 only. Boot wipe turns WiFi back on |

Boards sync only when **System ID** and **Cue Group** match (configured at `/setup`).

---

## Source files

| File | Role |
|------|------|
| `PeerSync.cpp` / `PeerSync.h` | mDNS discovery, HTTP POST push, background GET polling, CueSnapshot apply, LoRa/WiFi relay |
| `CueLora.cpp` / `CueLora.h` | Heltec SX1262 RadioLib transport (915 MHz); stubs on NodeMCU |
| `CueIO.cpp` / `CueIO.h` | Buttons, lamp outputs, sequence numbers; triggers push on local change |
| `CueDisplay.cpp` / `CueDisplay.h` | Heltec OLED status; no-op on NodeMCU |
| `PlatformCompat.h` | ESP8266 / ESP32 WiFi, mDNS, HTTP includes |
| `config.h` | Pins (board-selected), defaults, `PEER_SYNC_*` and `LORA_*` constants |
| `esp_cue_light.ino` | WiFi, web server, `GET`/`POST /api/cues` handlers, Enable LoRa / Enable WiFi options |

---

## Tunable constants (`config.h`)

| Constant | Default | Purpose |
|----------|---------|---------|
| `PEER_SYNC_POLL_INTERVAL_MS` | 500 | Background GET poll interval (fallback) |
| `PEER_SYNC_PUSH_TIMEOUT_MS` | 800 | HTTP POST timeout when pushing to peers |
| `PEER_SYNC_DISCOVERY_MS` | 15000 | mDNS peer-table refresh interval |
| `PEER_SYNC_HTTP_TIMEOUT_MS` | 1500 | HTTP GET timeout per peer poll |
| `PEER_SYNC_PEER_STALE_MS` | 30000 | Drop unseen peers after this |
| `PEER_SYNC_MAX_PEERS` | 8 | Max tracked peers per board |

---

## Version history

| Version | Sync transport |
|---------|----------------|
| **1.5.0** | Heltec **Enable WiFi** option: LoRa-only (WIFI_OFF); boot wipe turns WiFi back on |
| **1.4.0** | OLED title is `CueLight-XXXX`; LoRa heard-peer count on the node icon |
| **1.3.0** | Heltec LoRa (SX1262) additive transport + WiFi relay; same CueSnapshot as HTTP |
| **1.1.2** | HTTP POST push on local change; 500 ms fallback poll; 15 s mDNS refresh |
| **1.1.1** | LEAmDNS fix: `installServiceQuery`, `MDNS.update()` in loop, init after web server |
| **1.1.0** | mDNS + HTTP GET polling only (initial peer sync) |
| 1.0.0 | UDP broadcast on port 45271 (removed) |
