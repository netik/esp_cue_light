# Operations guide

Practical steps for deploying, configuring, and troubleshooting cue-light boards on a show network.

**Firmware:** v1.3.0

---

## Deployment checklist

1. **Flash firmware** — `make upload-board1` / `make upload-both` for firmware only, or `make deploy-both` if LittleFS also needs updating.
2. **Join WiFi** — connect each board via captive portal at `/setup`.
3. **Set System ID and Cue Group** — must match across all boards that should sync.
4. **Heltec LoRa (optional)** — check **Enable LoRa** and match **LoRa Channel** on every radio Heltec. One Heltec on WiFi with LoRa on will relay to NodeMCU HTTP peers.
5. **Verify mDNS** — from a laptop on the same LAN, browse `_cuelight._tcp`.
6. **Verify sync** — press a button on board A; board B should follow within **~300 ms**.
7. **Confirm AP settings** — client isolation must be **off**.

---

## Configuration

Open `http://<board-ip>/setup` (or `http://192.168.4.1/setup` in AP mode).

| Setting | Purpose |
|---------|---------|
| WiFi SSID / password | LAN connectivity |
| **System ID** | Isolates unrelated installations (default: 1) |
| **Cue Group** | Sub-group within a system (default: 1) |
| **Enable LoRa** | Heltec only. SX1262 at 915 MHz; default off |
| **LoRa Channel** | Heltec only. Sub-band 0–7 (915.0 MHz + n × 0.2 MHz) |

Settings persist in LittleFS at `/setup/config.json` and survive normal firmware updates. LittleFS uploads preserve `/setup/` automatically (see main README).

After changing System ID or Cue Group, boards only sync with peers sharing the same values.

Heltec serial when LoRa starts:

```
LoRa ready: 915.0 MHz ch=0 SF7 system_id=1 cue_group=1
LoRa: tx
LoRa: rx cue1=1 seq1=3 cue2=0 seq2=0
```

---

## Monitoring

### Serial monitor (115200 baud)

On boot with WiFi connected:

```
Peer sync filter: system_id=1 cue_group=1
Peer sync ready: hostname=CueLight-8D22.local ip=192.168.1.42 system_id=1 cue_group=1
Cue Light Webserver 1.3.0 at 192.168.1.42
```

After mDNS discovers a peer:

```
Peer sync: peer added 192.168.1.43
Peer sync: mDNS refresh, tracking 1 peer(s)
```

On button press (sending board):

```
Cue 1 -> GREEN
Peer sync: pushed to 192.168.1.43
```

On receiving board:

```
Cue 1 -> GREEN (remote seq 3)
```

Background fallback poll (every 500 ms):

```
Peer sync: polled 192.168.1.43 OK
```

Warnings:

```
Peer sync unavailable until WiFi is connected.
Warning: Peer sync unavailable.
```

Normal in AP-only mode. Boards work locally; sync starts after joining the LAN.

### Web dashboard

Browse to `http://<board-ip>/` for live red/green indicators (polls `GET /api/cues` every second).

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
| Heltec LoRa not transmitting | Enable LoRa off | Check **Enable LoRa** on `/setup`; serial should show `LoRa ready` |
| Heltecs on LoRa don't sync | Channel mismatch | Match **LoRa Channel** (and System ID / Cue Group) |
| NodeMCU follows Heltec LoRa slowly or never | No WiFi relay | Put at least one Heltec on the show LAN with Enable LoRa |
| Cues work locally but never sync | WiFi client isolation | Disable isolation on the AP |
| Cues work locally but never sync | Boards on different VLANs/subnets | Put all boards on the same L2 network |
| Slow sync (~500 ms+) | Push failing; fallback poll only | Check serial for `push failed`; verify board-to-board HTTP on port 80 |
| Slow sync (~500 ms+) | No peers in table yet | Wait for mDNS (`peer added` in serial) or reboot both boards on WiFi |
| New board slow to join | mDNS discovery interval (15 s) | Wait one cycle or reboot after WiFi connect |
| `Peer sync unavailable` on boot | AP/captive-portal mode | Connect board to show WiFi |
| mDNS browse finds nothing on boards but laptop sees services | Old firmware (< v1.1.1) | Flash v1.1.2+; serial should show `peer added` |
| mDNS blocked on network | AP or firewall blocks multicast | Allow UDP 5353; verify with `dns-sd -B _cuelight._tcp` from laptop |
| `Peer sync: push failed` | Peer unreachable or HTTP blocked | Verify `curl http://<peer-ip>/api/cues` from laptop; check isolation |
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

Test push manually:

```bash
curl -X POST http://192.168.1.42/api/cues \
  -H "Content-Type: application/json" \
  -d '{"system_id":1,"cue_group":1,"cue1":1,"cue2":0,"seq1":10,"seq2":0}'
```

Expect `{"ok":1}` if sequences are newer than the board's current state.

---

## WiFi wipe (factory reset credentials)

At boot, cue lamps blink red/green for **8 seconds**. Hold the primary button for **3 seconds** during that window to wipe saved WiFi credentials. The board starts AP mode (`CueLight-XXXX` / password `123456789`) immediately — it does not reconnect to the previous network.

On **Heltec V3**, GPIO 0 (PRG) held *during reset* enters flash download mode instead. Power on without holding PRG. When the OLED splash appears and the lamps blink, hold **PRG for 3 seconds**. (A 3-second hold *after* boot is power-off, not wipe.)

ESP32 stores WiFi in NVS (not only `/credentials.bin`). Wipe clears that store so `/setup` comes back.

---

## Tuning sync speed

Defaults in `config.h` (v1.1.2):

```cpp
#define PEER_SYNC_POLL_INTERVAL_MS 500    // fallback GET poll (ms)
#define PEER_SYNC_PUSH_TIMEOUT_MS 800     // POST push timeout (ms)
#define PEER_SYNC_DISCOVERY_MS 15000      // mDNS peer-table refresh (ms)
#define PEER_SYNC_HTTP_TIMEOUT_MS 1500    // GET poll timeout (ms)
```

Push latency is dominated by WiFi/HTTP RTT (~100–300 ms). Lower `PEER_SYNC_POLL_INTERVAL_MS` only affects fallback poll speed, not button-press push.

Rebuild and upload firmware after changes:

```bash
make upload-both
```

---

## See also

- [Peer sync architecture](./peer-sync-architecture.md)
- [Network protocol](./network-protocol.md)
- [Main README](../README.md) — build, upload, hardware pins
