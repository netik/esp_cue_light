# Cue Light Webserver

ESP8266 (NodeMCU v2) firmware for a two-cue light controller with a web dashboard, WiFi setup portal, and **peer sync** across boards on the same LAN.

Press a button on any board to toggle a cue red/green locally. That change **POSTs immediately** to every other board in the same **System ID** and **Cue Group** (~100–300 ms). Background polling catches anything missed.

**Firmware version:** 1.1.2

## How sync works

```
Board A  ←—— mDNS (_cuelight._tcp) ——→  Board B
         ←—— POST /api/cues on button press
         ←—— GET /api/cues every 500 ms (fallback)
```

1. Each board advertises itself on the LAN via LEAmDNS.
2. On button press, the board **POSTs** state to all known peers immediately.
3. Background **GET** polling every 500 ms catches missed pushes and late joiners.
4. Per-cue **sequence numbers** resolve conflicts — highest seq wins.

Full design details: **[docs/peer-sync-architecture.md](./docs/peer-sync-architecture.md)**

## Quick start

```bash
./scripts/setup-libraries.sh   # once, installs ESP8266 core + libraries
make upload-both               # flash firmware to both boards (see ports below)
```

1. Power boards and connect them to your show WiFi via `/setup`.
2. Set the same **System ID** and **Cue Group** on every board (defaults: `1` / `1`).
3. Press a button on one board — others should follow within **~300 ms**.

First-time flash also needs LittleFS once so `/index.htm` exists on disk:

```bash
make littlefs-both    # or make deploy-both for firmware + filesystem
```

After that, firmware-only uploads are fine when you are not changing `data/`.

---

## Hardware

Target: **NodeMCU v2** (`esp8266:esp8266:nodemcuv2`, 4 MB flash, 2 MB LittleFS).

| Function         | NodeMCU pin | GPIO |
|------------------|-------------|------|
| Cue 1 button     | D1          | 5    |
| Cue 2 button     | D2          | 4    |
| Cue 1 red lamp   | D5          | 14   |
| Cue 1 green lamp | D6          | 12   |
| Cue 2 red lamp   | D7          | 13   |
| Cue 2 green lamp | D8          | 15   |

Pin assignments are in `config.h`.

---

## Prerequisites

- [Arduino CLI](https://arduino.github.io/arduino-cli/)
- Libraries installed by `./scripts/setup-libraries.sh`:
  - `AsyncEspFsWebserver`
  - `ESPAsyncTCP`
  - `ESP32Async/ESPAsyncWebServer` (fork; setup script replaces stock library)

Core libraries used by peer sync (included with ESP8266 core, no extra install):

- `ESP8266mDNS`
- `ESP8266HTTPClient`

---

## Build and upload

### Board ports

Configured in `.vscode/settings.json` and `Makefile`:

| Board   | Port                      |
|---------|---------------------------|
| **0001**  | `/dev/cu.usbserial-0001`  |
| **83430** | `/dev/cu.usbserial-83430` |

### VS Code / Cursor tasks

| Task | What it does |
|------|----------------|
| **Arduino: Compile (ESP8266)** | Build firmware |
| **Arduino: Upload firmware → Board 0001 / 83430 / Both** | Flash firmware only |
| **Arduino: Upload LittleFS → Board 0001 / 83430** | Upload `data/` to flash |
| **Arduino: Full Deploy → Board 0001 / 83430 / Both** | Firmware + LittleFS |
| **Arduino: Serial Monitor → Board 0001 / 83430** | 115200 baud debug output |

### Makefile targets

```bash
make compile          # build only

# Firmware only (skip when data/ unchanged)
make upload-board1
make upload-board2
make upload-both

# LittleFS only
make littlefs-board1
make littlefs-board2
make littlefs-both

# Firmware + LittleFS
make deploy-board1
make deploy-board2
make deploy-both

# Ad-hoc port
make upload PORT=/dev/cu.usbserial-0001

make monitor-board1   # serial monitor, 115200 baud
```

### LittleFS and setup config

LittleFS uploads replace the whole 2 MB filesystem partition. The upload script preserves `/setup/` (WiFi credentials, System ID, Cue Group) automatically:

1. Reads current `/setup/` from the device before flashing
2. Falls back to `.littlefs-backup/setup/` if the read fails
3. Merges preserved config into the new image
4. Refreshes the local backup after a successful read

Use `--fresh` only when you intentionally want to wipe WiFi and options.

The dashboard at `/index.htm` is also embedded in firmware (`DashboardHtml.h`) and written to LittleFS on boot if missing — but an initial LittleFS upload is still recommended.

---

## First-time WiFi setup

1. Power the board. It tries saved WiFi for 10 seconds.
2. If that fails, it starts an access point:
   - **SSID:** `CueLight-XXXX` (last two bytes of MAC)
   - **Password:** `123456789`
3. Join that network. Captive portal should open; otherwise go to `http://192.168.4.1/setup`.
4. Choose your WiFi network. Set **System ID** and **Cue Group** (defaults: both `1`).
5. Save. Note the station IP from the serial monitor.

Config is stored at `/setup/config.json` on LittleFS.

**WiFi wipe:** hold the Cue 1 button for 3 seconds at boot.

### Expected serial output (station mode)

```
Peer sync ready: hostname=CueLight-8D22.local ip=192.168.x.x system_id=1 cue_group=1

Cue Light Webserver 1.1.2 at 192.168.x.x
Dashboard at /. Configure network at /setup
```

Peer sync is unavailable in AP-only mode; boards still work locally via buttons.

---

## Web interface

All URLs are on port **80**. Replace `{host}` with the board IP or `192.168.4.1` in AP mode.

| URL | Description |
|-----|-------------|
| `/` | Dashboard (redirects to `/index.htm` or `/setup`) |
| `/index.htm` | Live cue status — polls `/api/cues` every second |
| `/api/cues` | JSON state — `GET` to read, `POST` to push (peer sync) |
| `/setup` | WiFi credentials, System ID, Cue Group |

**`/api/cues` example:**

```json
{"system_id":1,"cue_group":1,"cue1":0,"cue2":1,"seq1":3,"seq2":7}
```

| Field | Values |
|-------|--------|
| `cue1`, `cue2` | `0` = RED, `1` = GREEN |
| `seq1`, `seq2` | Per-cue sequence numbers for sync |

Full API and mDNS details: **[docs/network-protocol.md](./docs/network-protocol.md)**

### Browse boards from a laptop

```bash
curl http://192.168.1.42/api/cues
dns-sd -B _cuelight._tcp          # macOS
avahi-browse -rt _cuelight._tcp   # Linux
```

### Other routes (AsyncEspFsWebserver)

| URL | Description |
|-----|-------------|
| `/setup-ws` | WebSocket backend for setup page |
| `/reset` | Reboot after returning current IP |
| `/edit` | Browser filesystem editor (enabled in firmware) |

---

## Documentation

| Document | Contents |
|----------|----------|
| [docs/README.md](./docs/README.md) | Documentation index |
| [docs/peer-sync-architecture.md](./docs/peer-sync-architecture.md) | Design rationale, timing, failure modes |
| [docs/network-protocol.md](./docs/network-protocol.md) | HTTP API, mDNS records, integration examples |
| [docs/operations-guide.md](./docs/operations-guide.md) | Deployment checklist, monitoring, troubleshooting |

---

## Project layout

```
cue_light_webserver.ino   Main sketch — WiFi, web server, API
config.h                  Pins, defaults, peer-sync timing
CueIO.*                   Buttons, lamp outputs, sequence numbers
PeerSync.*                LEAmDNS discovery, HTTP POST push, GET polling
DashboardHtml.h           Embedded dashboard (seeded to LittleFS)
data/index.htm            Dashboard source (uploaded to LittleFS)
docs/                     Architecture, protocol, operations
scripts/
  setup-libraries.sh      Install ESP8266 core and libraries
  upload-littlefs.sh      Build and flash LittleFS image
```

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Cues work locally but don't sync | Match System ID and Cue Group on all boards. Disable WiFi client isolation. See [operations guide](./docs/operations-guide.md). |
| `Peer sync unavailable` on boot | Normal in AP mode. Connect to show WiFi. |
| Slow sync (~500 ms+) | Check serial for `push failed`; see [operations guide](./docs/operations-guide.md). |
| `/` opens setup, not dashboard | Run `make littlefs-board1` (or `-board2`) once. |
| WiFi reset after LittleFS upload | Use default upload script; avoid `--fresh` unless intentional. |
| Can't join WiFi | Re-enter AP mode; AP password is always `123456789`. |

More detail: **[docs/operations-guide.md](./docs/operations-guide.md)**
