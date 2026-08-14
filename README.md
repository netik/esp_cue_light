# Cue Light Webserver

ESP8266 (NodeMCU v2) firmware for a two-cue light controller with a built-in web UI, WiFi setup portal, and **peer sync** between devices on the same LAN.

Physical buttons toggle cue states locally; changes propagate to other boards via **mDNS discovery + HTTP polling** (no leader, no broker). See **[docs/](./docs/)** for full architecture and protocol details.

Boards sync only when they share the same **System ID** and **Cue Group**.

## Hardware

Target board: **NodeMCU v2** (`esp8266:esp8266:nodemcuv2`, 4 MB flash with 2 MB LittleFS).

| Function        | NodeMCU pin | GPIO |
|-----------------|-------------|------|
| Cue 1 button    | D1          | 5    |
| Cue 2 button    | D2          | 4    |
| Cue 1 red lamp  | D5          | 14   |
| Cue 1 green lamp| D6          | 12   |
| Cue 2 red lamp  | D7          | 13   |
| Cue 2 green lamp| D8          | 15   |

Pin assignments live in `config.h` if you need to rewire.

## Prerequisites

- [Arduino CLI](https://arduino.github.io/arduino-cli/)
- ESP8266 core and libraries (project script installs these):

```bash
./scripts/setup-libraries.sh
```

Required libraries:

- `AsyncEspFsWebserver`
- `ESPAsyncTCP`
- `ESP32Async/ESPAsyncWebServer` (fork; the setup script replaces the stock library)

## Build and upload

### VS Code / Cursor tasks

Board ports are set in `.vscode/settings.json`:

| Board | Port |
|-------|------|
| **0001** | `/dev/cu.usbserial-0001` |
| **83430** | `/dev/cu.usbserial-83430` |

| Task | What it does |
|------|----------------|
| **Arduino: Compile (ESP8266)** | Build firmware (shared) |
| **Arduino: Upload firmware → Board 0001 / 83430** | Compile + upload to one board |
| **Arduino: Upload firmware → Both Boards** | Compile once, upload to both |
| **Arduino: Upload LittleFS → Board 0001 / 83430** | Upload `data/` to one board |
| **Arduino: Full Deploy → Board 0001 / 83430 / Both Boards** | Firmware + LittleFS |
| **Arduino: Serial Monitor → Board 0001 / 83430** | 115200 baud debug output |

### Command line

```bash
# Per-board deploy
make deploy-board1    # /dev/cu.usbserial-0001
make deploy-board2    # /dev/cu.usbserial-83430
make deploy-both

# Or override a single port
make upload PORT=/dev/cu.usbserial-0001
make littlefs PORT=/dev/cu.usbserial-83430

# Compile only
make compile
```

**Important:** Uploading firmware alone does **not** install `data/index.htm`. Run **Upload LittleFS** (or **Full Deploy**) at least once so the status page is available at `/`.

### Preserving setup config across LittleFS uploads

Each LittleFS upload replaces the whole 2 MB filesystem partition. Without care, that would erase `/setup/config.json` (WiFi credentials, System ID, Cue Group).

The upload script now preserves the entire `/setup/` directory automatically:

1. **Read from device** — pulls the current LittleFS image over serial and extracts `/setup/`
2. **Local backup fallback** — if the read fails, restores from `.littlefs-backup/setup/` (gitignored; contains WiFi credentials)
3. **Merge and upload** — copies `data/` into a staging folder, overlays the preserved `/setup/`, then flashes the combined image
4. **Refresh backup** — after a successful device read, updates `.littlefs-backup/`

Use `--fresh` when you intentionally want to wipe saved WiFi and options.

On boot, the serial monitor prints the device IP and setup hint:

```
Cue Light Webserver 1.0.0 at 192.168.x.x
Buttons D1/D2 toggle cues 1/2. Configure network at /setup
```

## First-time WiFi setup

1. Power the board. It tries to connect to saved WiFi for 10 seconds.
2. If that fails, it starts an access point:
   - **SSID:** `CueLight-XXXX` (last two bytes of the MAC address)
   - **Password:** `123456789`
3. Join that network. A captive portal should open; if not, browse to `http://192.168.4.1/setup`.
4. On `/setup`, choose your WiFi network and set **System ID** and **Cue Group** (defaults: both `1`).
5. Save. The device reconnects in station mode. Note the IP from the serial monitor.

Settings are stored in LittleFS at `/setup/config.json`.

## Web URLs

Replace `{host}` with the device IP (station mode) or `192.168.4.1` (AP mode). All paths are on port **80**.

### Application (this project)

| URL | Method | Description |
|-----|--------|-------------|
| `/` | GET | Redirects to `/index.htm` if that file exists on LittleFS; otherwise redirects to `/setup`. |
| `/index.htm` | GET | **Cue status dashboard** — live red/green indicators, polls `/api/cues` every second. Served from LittleFS (`data/index.htm`). |
| `/api/cues` | GET | JSON snapshot of network filter and cue states. |

**`/api/cues` response example:**

```json
{"system_id":1,"cue_group":1,"cue1":0,"cue2":1,"seq1":3,"seq2":7}
```

- `cue1` / `cue2`: `0` = RED, `1` = GREEN
- `seq1` / `seq2`: per-cue sequence numbers used for peer sync (see [docs/network-protocol.md](./docs/network-protocol.md))

### WiFi and configuration (AsyncEspFsWebserver)

| URL | Method | Description |
|-----|--------|-------------|
| `/setup` | GET | WiFi credentials, **System ID**, **Cue Group**, and other saved options. |
| `/setup-ws` | WebSocket | Backend for the setup page (scan, connect, save config). |
| `/upload` | POST | Upload configuration or other files to LittleFS. |
| `/reset` | GET | Responds with the current IP, then reboots the ESP8266. |
| `/setup/logo.svg` | GET | Setup page logo (default embedded SVG if none on filesystem). |

Config file on disk: `/setup/config.json`.

In AP / captive-portal mode, OS connectivity-check URLs (e.g. `/generate_204`, `/hotspot-detect.html`) are redirected to `/setup`.

### Filesystem editor (enabled in firmware)

`server.enableFsCodeEditor()` is on in `cue_light_webserver.ino`.

| URL | Method | Description |
|-----|--------|-------------|
| `/edit` | GET | Browser-based file editor (ACE). |
| `/edit` | POST | Upload a file. |
| `/edit` | PUT | Create or rename a file. |
| `/edit` | DELETE | Delete a file. |
| `/list?dir=/` | GET | JSON directory listing. |
| `/status` | GET | Filesystem usage info. |

### Static files

Any other file on LittleFS is served directly (e.g. `/foo.css` → `/foo.css` on the filesystem). Gzip siblings (`.gz`) are used automatically when present.

## Is `index.htm` used?

Yes. The dashboard is embedded in firmware (`DashboardHtml.h`) and **automatically written to LittleFS** as `/index.htm` on boot (and after saving `/setup`) if the file is missing.

You can still upload `data/index.htm` via **Upload LittleFS** to update the on-flash copy without reflashing firmware. The upload script always merges `data/` into the image and preserves runtime files (`/setup/`, `credentials.bin`).

## Peer sync (board-to-board)

Boards discover each other with **mDNS** (`_cuelight._tcp`) and poll `GET /api/cues` on peers every few seconds. There is no UDP broadcast and no central broker.

| Topic | Document |
|-------|----------|
| Architecture and design rationale | [docs/peer-sync-architecture.md](./docs/peer-sync-architecture.md) |
| HTTP API and mDNS records | [docs/network-protocol.md](./docs/network-protocol.md) |
| Deployment and troubleshooting | [docs/operations-guide.md](./docs/operations-guide.md) |

### Quick integration example

```bash
curl http://192.168.1.42/api/cues
dns-sd -B _cuelight._tcp    # macOS: browse for boards
```

## Project layout

```
cue_light_webserver.ino   Main sketch (web server, WiFi, API route)
config.h                  Pins, defaults, sync timing
CueIO.*                   Buttons, lamp outputs, sequence numbers
PeerSync.*                mDNS discovery and HTTP peer polling
docs/                     Architecture, protocol, operations guides
data/index.htm            Status dashboard (uploaded to LittleFS)
scripts/
  setup-libraries.sh      Install cores and libraries
  upload-littlefs.sh      Build and flash LittleFS image
```

## Troubleshooting

| Symptom | Likely cause |
|---------|----------------|
| `/` opens setup, not the dashboard | LittleFS not uploaded, or `/index.htm` missing from flash. Run **Upload LittleFS**. |
| WiFi / options reset after LittleFS upload | Use the default upload script (preserves `/setup`). Avoid `--fresh` unless intentional. |
| `/api/cues` works but `/index.htm` 404 | Filesystem empty or wrong partition layout. Re-run `./scripts/upload-littlefs.sh`. |
| Cannot join WiFi after config | Use AP mode again; check `/setup` credentials. AP password is always `123456789`. |
| Cues do not sync between devices | Match **System ID** and **Cue Group**; all devices must be on the same WiFi/LAN. Check AP client isolation is off. See [docs/operations-guide.md](./docs/operations-guide.md). |
| Peer sync warning on boot | WiFi not connected yet (common in AP-only mode). Reconnect to your LAN. Device still works locally via buttons. |
