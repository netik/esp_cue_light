# Operations guide

Practical steps for deploying, configuring, and troubleshooting cue-light boards on a show network.

## Deployment checklist

1. **Flash firmware** — `make deploy-board1` (or VS Code *Full Deploy* task).
2. **Join WiFi** — connect each board via captive portal at `/setup`.
3. **Set System ID and Cue Group** — must match across all boards that should sync.
4. **Verify mDNS** — from a laptop on the same LAN, browse `_cuelight._tcp`.
5. **Verify sync** — press a button on board A; board B should follow within ~3 seconds.
6. **Confirm AP settings** — client isolation must be **off**.

---

## Configuration

Open `http://<board-ip>/setup` (or `http://192.168.4.1/setup` in AP mode).

| Setting | Purpose |
|---------|---------|
| WiFi SSID / password | LAN connectivity |
| **System ID** | Isolates unrelated installations (default: 1) |
| **Cue Group** | Sub-group within a system (default: 1) |

Settings persist in LittleFS at `/setup/config.json` and survive normal firmware updates. LittleFS uploads preserve `/setup/` automatically (see main README).

After changing System ID or Cue Group, boards only sync with peers sharing the same values.

---

## Monitoring

### Serial monitor (115200 baud)

On boot with WiFi connected:

```
Peer sync ready: hostname=CueLight-A1B2 system_id=1 cue_group=1
Peer sync: discovered 2 peer(s)
Peer sync: polled 192.168.1.43 OK
Peer sync: applied cue 1 -> GREEN (seq 5) from 192.168.1.43
```

Warnings:

```
Warning: Peer sync unavailable until WiFi is connected.
```

This is normal in AP-only mode. Boards work locally; sync starts after joining the LAN.

### Web dashboard

Browse to `http://<board-ip>/` for live red/green indicators (polls `/api/cues` every second).

### API spot-check

```bash
curl -s http://192.168.1.42/api/cues | python3 -m json.tool
```

Expected:

```json
{
    "system_id": 1,
    "cue_group": 1,
    "cue1": 0,
    "cue2": 1,
    "seq1": 3,
    "seq2": 7
}
```

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| Cues work locally but never sync | System ID or Cue Group mismatch | Match values in `/setup` on all boards |
| Cues work locally but never sync | WiFi client isolation | Disable isolation on the AP |
| Cues work locally but never sync | Boards on different VLANs/subnets | Put all boards on the same L2 network |
| Slow sync (3+ seconds) | Normal poll interval | Expected; lower `PEER_SYNC_POLL_INTERVAL_MS` in `config.h` if needed |
| New board slow to join | mDNS discovery interval (30 s) | Wait one cycle or reboot after WiFi connect |
| `Peer sync unavailable` on boot | AP/captive-portal mode | Connect board to show WiFi |
| mDNS browse finds nothing | mDNS blocked on network | Allow UDP 5353 multicast; use `curl` to known IP as fallback |
| More than 8 boards in one group | Peer table limit | Increase `PEER_SYNC_MAX_PEERS` in `config.h` |
| `/api/cues` works, dashboard 404 | Missing LittleFS | Run LittleFS upload / full deploy |

### Verify peer discovery from a laptop

**macOS:**

```bash
dns-sd -B _cuelight._tcp
```

**Linux:**

```bash
avahi-browse -rt _cuelight._tcp
```

You should see one entry per powered-on board on the LAN.

### Verify HTTP reachability between boards

From a laptop, curl each board IP. If laptops can reach all boards but boards cannot sync, suspect **client isolation** or a firewall blocking board-to-board traffic.

---

## WiFi wipe (factory reset credentials)

Hold **Cue 1 button** for **3 seconds** at boot to wipe saved WiFi credentials. The board restarts AP mode (`CueLight-XXXX` / password `123456789`).

---

## Tuning sync speed

Edit `config.h`:

```cpp
#define PEER_SYNC_POLL_INTERVAL_MS 3000   // decrease for faster sync (e.g. 1500)
#define PEER_SYNC_DISCOVERY_MS 30000      // decrease for faster peer discovery on join
```

Rebuild and upload firmware after changes.

---

## See also

- [Peer sync architecture](./peer-sync-architecture.md)
- [Network protocol](./network-protocol.md)
- [Main README](../README.md) — build, upload, hardware pins
