# Peer sync architecture

This document describes how cue-light boards synchronize state on a local network. It replaces the earlier UDP broadcast design, which could not meet reliability requirements for production cueing.

## Problem statement

### Requirements

- **Multiple boards** on the same WiFi/LAN, powered on in arbitrary order.
- **No missed cue events** — if any board changes a cue, all others must eventually reflect that state.
- **Late joiners** — a board that connects after a cue change must catch up without manual intervention.
- **Low latency** — cue changes should propagate to other boards in well under a second during a show.
- **ESP8266 constraints** — limited RAM (~20–30 KB free with the async web server loaded); no room for MQTT brokers/clients or heavy protocols on-device.
- **No external infrastructure** — no Raspberry Pi broker, no cloud, no dedicated leader hardware.

### Why UDP broadcast failed

The original transport sent a single JSON packet to the subnet broadcast address (`192.168.x.255:45271`) whenever a cue changed.

That approach has two fundamental flaws:

| Flaw | Consequence |
|------|-------------|
| **Fire-and-forget** | WiFi drops, AP broadcast limits, or buffer overflow silently lose packets. There is no retry or acknowledgment. |
| **No memory** | Broadcast is ephemeral. A board that is off, rebooting, or still joining WiFi never receives past events. |

Retries and periodic heartbeats over UDP improve odds but still cannot guarantee delivery or give late joiners historical state without additional mechanisms.

### Why not MQTT (on the board)

MQTT with retained messages and QoS 1 would solve late join and delivery guarantees, but:

- Requires a **broker** running elsewhere on the network.
- Adds **RAM and flash** cost on ESP8266 on top of an already heavy AsyncFsWebServer stack.
- Introduces a **single point of failure** (the broker).

MQTT remains a valid option if you later add always-on show hardware; it is intentionally not used in firmware today.

### Why not a leader node

A leader/follower model simplifies conflict resolution (one writer) but introduces a new problem: **leader discovery and failover**.

Options like lowest-MAC election, first-on-network claims, or heartbeat timeouts add complexity and split-brain edge cases on resource-constrained MCUs. Manual leader IP configuration works but is operationally fragile (DHCP changes, wrong config).

Peer sync with sequence numbers avoids the problem entirely: every board is equal, and conflicts are resolved deterministically.

---

## Design overview

```
┌──────────────────────────────────────────────────────────────────┐
│                         WiFi LAN                                 │
│                                                                  │
│   ┌─────────────┐    mDNS (_cuelight._tcp)   ┌─────────────┐     │
│   │   Board A   │◄──────────────────────────►│   Board B   │     │
│   │  .local:80  │                            │  .local:80  │     │
│   └──────┬──────┘                            └──────┬──────┘     │
│          │                                          │            │
│          │  POST /api/cues  (on button press)       │            │
│          └──────────────────►───────────────────────┘            │
│          │  GET /api/cues  (every 500 ms, fallback) │            │
│          └──────────────────►◄──────────────────────┘            │
│                                                                  │
│          ┌─────────────┐                                         │
│          │   Board C   │  joins anytime, mDNS discover, poll     │
│          │  .local:80  │  peers, apply highest seq per cue       │
│          └─────────────┘                                         │
└──────────────────────────────────────────────────────────────────┘
```

### Core principles

1. **Peer equality** — no leader, no follower, no broker.
2. **Push on change** — local button press immediately POSTs state to all known peers.
3. **Poll as fallback** — background GET polling catches missed pushes and late joiners.
4. **Per-cue sequence numbers** — every local change increments that cue's sequence; receivers apply only newer sequences.
5. **mDNS for discovery only** — service records locate peers; HTTP carries authoritative state.
6. **Filter by network identity** — `system_id` and `cue_group` isolate unrelated installations on the same LAN.

---

## Components

### mDNS advertisement (each board)

When WiFi is connected, each board (after `server.init()`):

1. Calls `MDNS.begin(hostname)` with a unique hostname derived from MAC (`CueLight-A1B2`).
2. Registers service `_cuelight._tcp` on port **80** via `MDNS.addService()`.
3. Publishes TXT records `system_id` and `cue_group` for pre-filtering during browse.

Other boards (and laptops with Bonjour/Avahi) can resolve `CueLight-A1B2.local` or browse `_cuelight._tcp`.

### mDNS discovery (continuous)

ESP8266 uses the **LEAmDNS** API (not the legacy synchronous `queryService()`). Each board:

1. Installs a dynamic service query with `MDNS.installServiceQuery("cuelight", "tcp", callback)`.
2. Calls `MDNS.update()` on every main-loop iteration — **required** for mDNS to send and receive packets.
3. Refreshes the peer table from `MDNS.answerInfo()` every **15 seconds** (configurable via `PEER_SYNC_DISCOVERY_MS`).

Peer sync starts **after** the web server initializes (`server.init()`), so the advertised HTTP service on port 80 is actually listening.

Results populate a small in-memory peer table (max **8** peers). The board's own IP is excluded. TXT records `system_id` and `cue_group` filter unrelated peers during discovery.

### HTTP push (primary sync path)

When a physical button changes a cue locally:

1. `CueIO::setCueState()` updates local state and increments the cue's sequence number.
2. `PeerSync::pushStateToPeers()` builds the full state JSON and **POSTs** it to every known peer.
3. Each peer's `POST /api/cues` handler calls `PeerSync::applyIncomingJson()` and applies state if sequences are newer.

Typical latency to remote boards: **~100–300 ms** (one HTTP POST per peer).

Serial on the sending board:

```
Cue 1 -> GREEN
Peer sync: pushed to 192.168.1.43
```

Serial on the receiving board:

```
Cue 1 -> GREEN (remote seq 5)
```

### HTTP polling (fallback sync path)

Every **500 ms** (configurable via `PEER_SYNC_POLL_INTERVAL_MS`), the board **GETs** `/api/cues` from **one** peer per main-loop iteration (round-robin). This catches:

- Pushes that failed (peer offline momentarily, HTTP timeout).
- Late joiners that haven't received a push yet.
- Any drift between boards.

```http
GET /api/cues HTTP/1.1
Host: 192.168.1.42
```

The response is parsed with the same logic as POST; cue states apply only when the remote sequence is newer.

### `/api/cues` as the source of truth

Every board exposes identical state via HTTP. The dashboard, peer sync push/poll, and external scripts all use the same JSON schema. There is no separate sync channel.

State JSON is built centrally by `PeerSync::buildStateJson()` and used by both the GET handler and push logic. Heltec LoRa packs the same fields into an 18-byte packet (`CueLora`); both paths call `PeerSync::applyIncomingState()`.

---

## LoRa (Heltec V3)

LoRa is **additive**. `/setup` has **Enable LoRa** (default off) and **LoRa Channel** 0–7. WiFi STA / AP boot is unchanged. NodeMCU has no radio.

When Enable LoRa is on:

- Local button → HTTP POST (if STA) **and** LoRa TX
- Incoming WiFi (newer seq) → apply, then LoRa TX (no HTTP re-push)
- Incoming LoRa (newer seq) → apply, then HTTP POST to known peers (no LoRa re-TX)
- Beacon every ~5 s so late joiners catch up

One Heltec on show WiFi with Enable LoRa is enough to mix NodeMCU WiFi peers with radio-only Heltecs (still on the same System ID / Cue Group / LoRa channel). Exclusive WiFi-off LoRa-only is not a mode: AP/setup remains available.

RF is compile-time **915 MHz** in `config.h` (SF7, BW 125 kHz, 14 dBm). Channel 0 is 915.0 MHz; each step adds 0.2 MHz.

---

## Sequence numbers and conflict resolution

Each cue maintains an independent **32-bit unsigned sequence** (`seq1`, `seq2`). On a **local** change:

```
seq1++   (only for the cue that changed)
state1 = new value
```

On **remote** apply (via POST or GET), a candidate update is accepted only if its sequence is **newer** than the local sequence, using unsigned wrap-safe comparison:

```cpp
bool isNewer(uint32_t incoming, uint32_t current) {
  return incoming != current && (incoming - current) < 0x80000000UL;
}
```

Remote apply does **not** re-push or re-increment sequences on the same transport — it only adopts the peer's sequence number. A Heltec with LoRa enabled forwards a *successful* apply to the **other** transport (WiFi → LoRa TX, LoRa → HTTP POST).

### Simultaneous changes

If two boards change the **same cue** at nearly the same time, the higher sequence wins once both boards have exchanged state. In practice, human button presses are seconds apart; this is sufficient for cue lights.

If two boards change **different cues** simultaneously, each cue's sequence is independent — both changes propagate without conflict.

### Initial state

On boot, all sequences start at **0** and all cues start **RED**. The first board to change a cue sets `seq` to 1. Joining boards discover peers via mDNS, poll them, and adopt any non-zero sequences.

---

## Timing and latency

| Event | Typical latency |
|-------|-----------------|
| Local button → local lamp | Immediate (< 1 ms) |
| Local button → remote board | ~100–300 ms (HTTP POST push) |
| Missed push → remote board | ≤500 ms (background GET poll) |
| Board joins WiFi | Up to 15 s for mDNS refresh + ≤500 ms for first poll |
| Board reconnects after drop | Discovery on next 15 s cycle; poll within 500 ms |

Tune in `config.h`:

| Constant | Default | Purpose |
|----------|---------|---------|
| `PEER_SYNC_PUSH_TIMEOUT_MS` | 800 | Timeout for POST push to each peer |
| `PEER_SYNC_POLL_INTERVAL_MS` | 500 | Background GET poll interval (fallback) |
| `PEER_SYNC_DISCOVERY_MS` | 15000 | How often mDNS peer table is refreshed |
| `PEER_SYNC_HTTP_TIMEOUT_MS` | 1500 | HTTP GET timeout per peer poll |
| `PEER_SYNC_PEER_STALE_MS` | 30000 | Remove unseen peers after this duration |
| `PEER_SYNC_MAX_PEERS` | 8 | Max tracked peers |

Lower poll interval = faster fallback sync, more WiFi traffic. Push latency is dominated by HTTP RTT, not the poll interval.

---

## Network identity filtering

Two boards sync only when both match:

| Field | Config location | Default |
|-------|-----------------|---------|
| `system_id` | `/setup` → System ID | 1 |
| `cue_group` | `/setup` → Cue Group | 1 |

Filtering happens at HTTP apply time (both POST and GET): payloads with mismatched `system_id` or `cue_group` are ignored. mDNS TXT records provide an additional filter during discovery.

Use different System IDs to isolate unrelated shows on one VLAN. Use different Cue Groups within a System ID to partition sub-groups (e.g. stage left vs stage right).

---

## Failure modes

| Scenario | Behavior |
|----------|----------|
| Push fails (peer unreachable) | Logged on sender; fallback GET poll recovers within 500 ms |
| Peer temporarily unreachable | Skipped for that poll cycle; retried on next round |
| All peers offline | Board operates standalone; re-syncs when peers return |
| WiFi AP client isolation enabled | mDNS and HTTP between clients fails. **Disable client isolation** on the AP. |
| Board in AP/captive-portal mode | HTTP peer sync disabled until station mode connects to LAN. LoRa still runs if Enable LoRa is on. |
| Heltec LoRa channel mismatch | Packets filtered at unpack; no apply. Match LoRa Channel on all radio boards. |
| Duplicate hostname (rare) | mDNS uses MAC-derived names; collision unlikely |
| More than 8 peers in one group | Only first 8 discovered IPs tracked; increase `PEER_SYNC_MAX_PEERS` if needed |
| Sequence overflow | Safe for practical show lengths; 32-bit counter wraps with correct comparison math |

---

## Memory and CPU budget

Peer sync adds roughly:

- **~2–3 KB RAM** — peer table, HTTP client, small parse buffer (one request at a time).
- **~4–8 KB flash** — PeerSync module + ESP8266HTTPClient (core library).

No TLS, no JSON library, no second TCP server. JSON is parsed with lightweight string scanning.

---

## Comparison with previous UDP design

| Aspect | UDP broadcast (removed) | Peer sync (current) |
|--------|-------------------------|---------------------|
| Delivery guarantee | None | Push + fallback poll |
| Late joiner support | None | Yes (mDNS + GET poll) |
| Discovery | Implicit (broadcast) | LEAmDNS browse |
| External tools | Listen on UDP port | `GET /api/cues` or mDNS browse |
| RAM cost | ~1 KB | ~2–3 KB |
| Latency | ~0 ms (if not lost) | ~100–300 ms typical |

---

## Future extensions (not implemented)

- **MQTT broker** — if always-on show hardware is available; boards would become thin clients.
- **Leader mode** — optional config for sites that want a single authoritative IP.
- **WebSocket push** — real-time dashboard updates without polling (dashboard already polls every 1 s).
- **Exclusive LoRa-only (WiFi off)** — Enable LoRa currently keeps WiFi STA/AP as today.

---

## See also

- [Network protocol](./network-protocol.md) — JSON schema and mDNS record details
- [Operations guide](./operations-guide.md) — deployment and troubleshooting
