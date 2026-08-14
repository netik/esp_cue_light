# Network protocol

Cue-light boards expose state over HTTP and discover each other with mDNS. There is no separate sync port or binary protocol.

## mDNS service record

Each board advertises:

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

TXT records are informational. Peer sync validates `system_id` and `cue_group` from the HTTP response body.

---

## HTTP API

### `GET /api/cues`

Returns the current cue snapshot for this board.

**Response** — `200 OK`, `Content-Type: application/json`

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

## Peer sync algorithm (reference)

Pseudocode for one poll cycle:

```
response = HTTP_GET(peer, "/api/cues")
if response.system_id != my.system_id: return
if response.cue_group != my.cue_group: return

if is_newer(response.seq1, my.seq1):
    apply cue1 = response.cue1, seq1 = response.seq1

if is_newer(response.seq2, my.seq2):
    apply cue2 = response.cue2, seq2 = response.seq2
```

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

There is **no write API** over HTTP in firmware today. Cue changes originate from physical buttons on any board and propagate via peer sync. A future `POST /api/cues` could be added for external control if needed.

---

## Removed: UDP port 45271

Firmware prior to v1.1.0 broadcast cue changes on UDP port **45271**. That transport has been **removed**. External tools should use `GET /api/cues` instead.

---

## Firewall and WiFi notes

- Allow **TCP port 80** between boards on the LAN.
- Allow **mDNS** (UDP port 5353, multicast `224.0.0.251`) for discovery.
- Disable **AP/client isolation** on the WiFi access point so stations can reach each other.

---

## See also

- [Peer sync architecture](./peer-sync-architecture.md)
- [Operations guide](./operations-guide.md)
