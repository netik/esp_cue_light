# Peer sync architecture

This document describes how cue-light boards synchronize state on a local network. It replaces the earlier UDP broadcast design, which could not meet reliability requirements for production cueing.

## Problem statement

### Requirements

- **Multiple boards** on the same WiFi/LAN, powered on in arbitrary order.
- **No missed cue events** — if any board changes a cue, all others must eventually reflect that state.
- **Late joiners** — a board that connects after a cue change must catch up without manual intervention.
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
│   ┌─────────────┐    mDNS browse       ┌─────────────┐           │
│   │   Board A   │◄──── _cuelight ────► │   Board B   │           │
│   │  .local:80  │                      │  .local:80  │           │
│   └──────┬──────┘                      └──────┬──────┘           │
│          │                                    │                  │
│          │   GET /api/cues  (every ~3 s)      │                  │
│          └──────────────►◄────────────────────┘                  │
│                                                                  │
│          ┌─────────────┐                                         │
│          │   Board C   │  joins anytime, browses mDNS, polls     │
│          │  .local:80  │  peers, applies highest seq per cue     │
│          └─────────────┘                                         │
└──────────────────────────────────────────────────────────────────┘
```

### Core principles

1. **Peer equality** — no leader, no follower, no broker.
2. **Pull-based sync** — boards fetch state from peers via HTTP; polling is the reliability layer.
3. **Per-cue sequence numbers** — every state change increments that cue's sequence; receivers apply only newer sequences.
4. **mDNS for discovery only** — service records locate peers; HTTP carries authoritative state.
5. **Filter by network identity** — `system_id` and `cue_group` isolate unrelated installations on the same LAN.

---

## Components

### mDNS advertisement (each board)

When WiFi is connected, each board:

1. Calls `MDNS.begin(hostname)` with a unique hostname derived from MAC (`CueLight-A1B2`).
2. Registers service `_cuelight._tcp` on port **80**.
3. Publishes TXT records `system_id` and `cue_group` for optional pre-filtering during browse.

Other boards (and laptops with Bonjour/Avahi) can resolve `CueLight-A1B2.local` or browse `_cuelight._tcp`.

### mDNS discovery (periodic)

Every **30 seconds** (configurable via `PEER_SYNC_DISCOVERY_MS`), each board runs:

```cpp
MDNS.queryService("cuelight", "tcp");
```

Results populate a small in-memory peer table (max **8** peers). The board's own IP is excluded.

Peers not seen in discovery or polling for **120 seconds** are removed as stale.

### HTTP polling (primary sync path)

Every **3 seconds** (configurable via `PEER_SYNC_POLL_INTERVAL_MS`), the board polls **one** peer per main-loop iteration (round-robin). This spreads HTTP work across loop cycles and avoids long blocking bursts.

Request:

```http
GET /api/cues HTTP/1.1
Host: 192.168.1.42
```

The response is parsed; cue states are applied only when the remote sequence number is newer than the local sequence for that cue.

### Immediate sync after local change

When a physical button changes a cue locally:

1. Local state and sequence number update immediately.
2. `PeerSync::requestSync()` resets the poll timer so peers are queried as soon as possible.

Polling still remains the safety net if the immediate round fails.

### `/api/cues` as the source of truth

Every board exposes identical state via HTTP. The dashboard, peer sync, and external scripts all read the same endpoint. There is no separate sync channel.

---

## Sequence numbers and conflict resolution

Each cue maintains an independent **32-bit unsigned sequence** (`seq1`, `seq2`). On a **local** change:

```
seq1++   (only for the cue that changed)
state1 = new value
```

On **remote** apply, a candidate update is accepted only if its sequence is **newer** than the local sequence, using unsigned wrap-safe comparison:

```cpp
bool isNewer(uint32_t incoming, uint32_t current) {
  return incoming != current && (incoming - current) < 0x80000000UL;
}
```

### Simultaneous changes

If two boards change the **same cue** at nearly the same time before either poll completes, the higher sequence wins once both boards have polled each other. In practice, human button presses are seconds apart; this is sufficient for cue lights.

If two boards change **different cues** simultaneously, each cue's sequence is independent — both changes propagate without conflict.

### Initial state

On boot, all sequences start at **0** and all cues start **RED**. The first board to change a cue sets `seq` to 1. Joining boards poll peers and adopt any non-zero sequences.

---

## Timing and latency

| Event | Typical latency |
|-------|-----------------|
| Local button → local lamp | Immediate (< 1 ms) |
| Local button → remote board | 0–3 s (next poll cycle) + HTTP RTT |
| Local button → remote board (best case) | Immediate poll triggered via `requestSync()` |
| Board joins WiFi | Up to 30 s for mDNS discovery + 3 s for first poll |
| Board reconnects after drop | Discovery on next cycle; poll on reconnect |

Tune in `config.h`:

| Constant | Default | Purpose |
|----------|---------|---------|
| `PEER_SYNC_POLL_INTERVAL_MS` | 3000 | How often each peer is polled |
| `PEER_SYNC_DISCOVERY_MS` | 30000 | How often mDNS browse refreshes peer list |
| `PEER_SYNC_HTTP_TIMEOUT_MS` | 2000 | HTTP client timeout per peer |
| `PEER_SYNC_PEER_STALE_MS` | 120000 | Remove unseen peers after this duration |
| `PEER_SYNC_MAX_PEERS` | 8 | Max tracked peers |

Lower poll interval = faster sync, more WiFi/CPU load. For live cueing, 2–3 s is a reasonable default.

---

## Network identity filtering

Two boards sync only when both match:

| Field | Config location | Default |
|-------|-----------------|---------|
| `system_id` | `/setup` → System ID | 1 |
| `cue_group` | `/setup` → Cue Group | 1 |

Filtering happens at HTTP apply time: responses with mismatched `system_id` or `cue_group` are ignored.

Use different System IDs to isolate unrelated shows on one VLAN. Use different Cue Groups within a System ID to partition sub-groups (e.g. stage left vs stage right).

---

## Failure modes

| Scenario | Behavior |
|----------|----------|
| Peer temporarily unreachable | Skipped for that poll cycle; retried on next round. Local cues still work. |
| All peers offline | Board operates standalone; re-syncs when peers return. |
| WiFi AP client isolation enabled | mDNS and HTTP between clients fails. **Disable client isolation** on the AP. |
| Board in AP/captive-portal mode | Peer sync disabled until station mode connects to LAN. |
| Duplicate hostname (rare) | mDNS negotiation resolves; IP-based polling still works. |
| More than 8 peers in one group | Only first 8 discovered IPs tracked; increase `PEER_SYNC_MAX_PEERS` if needed. |
| Sequence overflow | Safe for practical show lengths; 32-bit counter wraps with correct comparison math. |

---

## Memory and CPU budget

Peer sync adds roughly:

- **~2–3 KB RAM** — peer table, HTTP client, small parse buffer (one request at a time).
- **~4–8 KB flash** — PeerSync module + ESP8266HTTPClient (core library).

No TLS, no JSON library, no second TCP server. JSON is parsed with lightweight string scanning (same approach as the former UDP module).

---

## Comparison with previous UDP design

| Aspect | UDP broadcast (removed) | Peer sync (current) |
|--------|-------------------------|---------------------|
| Delivery guarantee | None | Eventual (poll-based) |
| Late joiner support | None | Yes (poll peers on join) |
| Discovery | Implicit (broadcast) | mDNS browse |
| External tools | Listen on UDP port | `GET /api/cues` or mDNS browse |
| RAM cost | ~1 KB | ~2–3 KB |
| Latency | ~0 ms (if not lost) | 0–3 s typical |

---

## Future extensions (not implemented)

These are documented for planning; none are required for reliable cue sync today.

- **HTTP POST notify** — push state to peers immediately in addition to polling (lower latency, slightly more code).
- **MQTT broker** — if always-on show hardware is available; boards would become thin clients.
- **Leader mode** — optional config for sites that want a single authoritative IP.
- **WebSocket push** — real-time dashboard updates without polling (dashboard already polls every 1 s).

---

## See also

- [Network protocol](./network-protocol.md) — JSON schema and mDNS record details
- [Operations guide](./operations-guide.md) — deployment and troubleshooting
