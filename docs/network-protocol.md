# Network protocol

Cue-light boards expose state over HTTP and discover each other with mDNS. There is no separate sync port or binary protocol.

## mDNS service record

Each board advertises (via LEAmDNS, after the web server starts):

| Field | Value |
|-------|-------|
| Service type | `_cuelight._tcp.local` |
| Port | 80 |
| Hostname | `CueLight-XXXX.local` (XXXX = last two MAC bytes) |
| TXT `system_id` | Configured System ID (decimal string) |
| TXT `cue_group` | Configured Cue Group (decimal string) |

### Browse from a laptop (macOS)

```bash
dns-sd -B _cuelight._tcp
```

### Browse from a laptop (Linux with Avahi)

```bash
avahi-browse -rt _cuelight._tcp
```

### Resolve a specific board

```bash
ping CueLight-A1B2.local
curl http://CueLight-A1B2.local/api/cues
```

TXT records filter peers during mDNS discovery. All sync payloads are also validated against `system_id` and `cue_group` in the HTTP JSON body.

---

## HTTP API

Both endpoints use the same JSON schema. State is built by `PeerSync::buildStateJson()` in firmware.

### Shared JSON schema

```json
{
  "system_id": 1,
  "cue_group": 1,
  "cue1": 0,
  "cue2": 1,
  "seq1": 42,
  "seq2": 17
}
```

| Field | Type | Description |
|-------|------|-------------|
| `system_id` | uint16 | Network filter — must match for sync |
| `cue_group` | uint16 | Network filter — must match for sync |
| `cue1` | uint8 | Cue 1 state: `0` = RED, `1` = GREEN |
| `cue2` | uint8 | Cue 2 state: `0` = RED, `1` = GREEN |
| `seq1` | uint32 | Monotonic sequence for cue 1 |
| `seq2` | uint32 | Monotonic sequence for cue 2 |

#### State values

| Value | Meaning | Lamp output |
|-------|---------|-------------|
| `0` | RED | Red ON, green OFF |
| `1` | GREEN | Red OFF, green ON |

#### Sequence semantics

- Sequences increment **only on local changes** (physical button on that board).
- Remote updates adopt the peer's sequence number when applied.
- A board ignores peer data when `system_id` or `cue_group` does not match its configuration.
- Receivers apply a cue field only when the incoming sequence is **newer** than the local sequence for that cue.

---

### `GET /api/cues`

Returns the current cue snapshot for this board.

**Response** — `200 OK`, `Content-Type: application/json`

Used by:

- Web dashboard (polls every 1 s)
- Background peer sync fallback (one peer every 500 ms)
- External monitoring scripts

---

### `POST /api/cues`

Pushes a cue snapshot from a peer board. Used for **fast sync** when a local button is pressed.

**Request** — `POST`, `Content-Type: application/json`, body = shared JSON schema above.

**Responses**

| Code | Body | Meaning |
|------|------|---------|
| `200` | `{"ok":1}` | At least one cue field applied (newer sequence) |
| `409` | `{"ok":0}` | Ignored — stale sequence, parse error, or wrong system/group |
| `413` | `{"ok":0}` | Body too large (max 127 bytes) |

Remote apply via POST does not trigger a re-push (no sync loops).

---

## Peer sync algorithms (reference)

### Push (on local button press)

```
json = build_state_json()
for peer in known_peers:
    HTTP_POST(peer, "/api/cues", json)
```

### Poll fallback (every 500 ms, one peer per loop)

```
response = HTTP_GET(peer, "/api/cues")
if response.system_id != my.system_id: return
if response.cue_group != my.cue_group: return

if is_newer(response.seq1, my.seq1):
    apply cue1 = response.cue1, seq1 = response.seq1

if is_newer(response.seq2, my.seq2):
    apply cue2 = response.cue2, seq2 = response.seq2
```

Both paths call the same `applyIncomingState()` logic in firmware.

---

## External integration

### Read state (Python)

```python
import urllib.request
import json

host = "192.168.1.42"  # or CueLight-A1B2.local
with urllib.request.urlopen(f"http://{host}/api/cues", timeout=2) as resp:
    data = json.load(resp)
    print(f"Cue 1: {'GREEN' if data['cue1'] else 'RED'} (seq {data['seq1']})")
    print(f"Cue 2: {'GREEN' if data['cue2'] else 'RED'} (seq {data['seq2']})")
```

### Push state (Python — same endpoint peers use)

```python
import urllib.request
import json

host = "192.168.1.42"
payload = {
    "system_id": 1,
    "cue_group": 1,
    "cue1": 1,
    "cue2": 0,
    "seq1": 99,
    "seq2": 0,
}
body = json.dumps(payload).encode("utf-8")
req = urllib.request.Request(
    f"http://{host}/api/cues",
    data=body,
    method="POST",
    headers={"Content-Type": "application/json"},
)
with urllib.request.urlopen(req, timeout=2) as resp:
    print(resp.status, resp.read().decode())
```

The board applies the update only if sequences are newer than local state and system/group match. Increment `seq` values carefully to avoid stale rejects.

### Monitor all boards on the LAN (Python + zeroconf)

```python
from zeroconf import ServiceBrowser, Zeroconf

class Listener:
    def add_service(self, zc, type_, name):
        info = zc.get_service_info(type_, name)
        if info:
            addr = ".".join(str(b) for b in info.addresses[0])
            print(name, addr, info.port)

zeroconf = Zeroconf()
browser = ServiceBrowser(zeroconf, "_cuelight._tcp.local.", Listener())
input("Press Enter to exit...\n")
zeroconf.close()
```

---

## LoRa transport (Heltec V3)

Heltec boards with **Enable LoRa** on `/setup` send the same snapshot as `/api/cues` in a 20-byte broadcast packet. There is no mDNS or HTTP on the air. Each packet includes a 16-bit node id (last two MAC bytes) so boards can count recently heard radios on the OLED.

```
offset  size  field
0       1     magic (0xC1)
1       1     version (2)
2–3     2     system_id (little-endian)
4–5     2     cue_group (little-endian)
6       1     cue1
7       1     cue2
8–11    4     seq1 (little-endian)
12–15   4     seq2 (little-endian)
16–17   2     node_id (MAC bytes 4–5, big-endian)
18–19   2     CRC-16/CCITT of bytes 0–17
```

Receivers apply the packet with the same sequence rules as HTTP. A board that is on WiFi **and** LoRa relays:

- WiFi apply (newer seq) → LoRa TX (no HTTP re-push)
- LoRa apply (newer seq) → HTTP POST to known peers (no LoRa re-TX)

RF: 915.0 MHz + channel × 0.2 MHz, SF7, BW 125 kHz, CR 4/5, sync word `0xC1`, 14 dBm. Beacon every ~5 s with MAC jitter so late joiners catch up.

---

## Removed: UDP port 45271

Firmware prior to v1.1.0 broadcast cue changes on UDP port **45271**. That transport has been **removed**. External tools should use `GET /api/cues` (read) or `POST /api/cues` (write, same rules as peer sync).

---

## Firewall and WiFi notes

- Allow **TCP port 80** between boards on the LAN (GET and POST).
- Allow **mDNS** (UDP port 5353, multicast `224.0.0.251`) for discovery.
- Disable **AP/client isolation** on the WiFi access point so stations can reach each other.

---

## See also

- [Peer sync architecture](./peer-sync-architecture.md)
- [Operations guide](./operations-guide.md)
