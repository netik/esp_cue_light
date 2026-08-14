# Cue Light — documentation

Firmware for ESP8266 cue-light boards that sync state across a LAN without a central broker or leader node.

| Document | Contents |
|----------|----------|
| [Peer sync architecture](./peer-sync-architecture.md) | Why UDP was dropped, design goals, discovery, conflict resolution, timing, failure modes |
| [Network protocol](./network-protocol.md) | HTTP API, JSON schema, mDNS service record, integration examples |
| [Operations guide](./operations-guide.md) | Deployment, configuration, monitoring, troubleshooting |

## Quick summary

Boards are **peers**. There is no leader and no MQTT broker.

1. Each board advertises itself with **mDNS** as `_cuelight._tcp`.
2. Boards discover one another by browsing that service on the LAN.
3. Each board **polls** peers over HTTP (`GET /api/cues`) every few seconds.
4. Cue changes carry **per-cue sequence numbers**; the highest sequence wins.
5. A board that joins late catches up on its first poll cycle.

All devices on the same WiFi/LAN with matching **System ID** and **Cue Group** stay in sync.

## Related project files

| File | Role |
|------|------|
| `PeerSync.cpp` / `PeerSync.h` | mDNS advertise/browse and HTTP peer polling |
| `CueIO.cpp` / `CueIO.h` | Buttons, lamp outputs, sequence numbers |
| `config.h` | Pins, defaults, sync timing constants |
| `cue_light_webserver.ino` | WiFi, web server, `/api/cues` route |
