# Cue Light Webserver

ESP8266 (NodeMCU v2) firmware for a two-cue light controller with a built-in web UI, WiFi setup portal, and UDP sync between devices and laptops on the same LAN.

Physical buttons toggle cue states locally and broadcast changes to other nodes on the same **System ID** and **Cue Group**.

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

| Task | What it does |
|------|----------------|
| **Arduino: Compile (ESP8266)** | Build firmware |
| **Arduino: Upload (ESP8266)** | Upload firmware |
| **Arduino: Upload LittleFS** | Upload `data/` to flash; preserves `/setup` config by default |
| **Arduino: Full Deploy (sketch + filesystem)** | Upload firmware, then LittleFS |
| **Arduino: Serial Monitor** | 115200 baud debug output |

Set your serial port in `.vscode/settings.json` (`cueLight.port`).

### Command line

```bash
# Compile
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2:eesz=4M2M,ip=lm2n .

# Upload firmware
arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2:eesz=4M2M,ip=lm2n -p /dev/cu.usbserial-0001 .

# Upload LittleFS (required for index.htm dashboard)
./scripts/upload-littlefs.sh /dev/cu.usbserial-0001

# Force a clean filesystem (wipes saved WiFi / setup options)
./scripts/upload-littlefs.sh --fresh /dev/cu.usbserial-0001
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
{"system_id":1,"cue_group":1,"cue1":0,"cue2":1}
```

- `cue1` / `cue2`: `0` = RED, `1` = GREEN

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

## UDP messaging

When a cue changes (button press or remote command), the device sends a **UDP broadcast** on port **45271** (`CUE_UDP_PORT` in `config.h`) to the subnet broadcast address (e.g. `192.168.1.255`).

Payload (JSON + CRLF):

```json
{"system_id":1,"cue_group":1,"cue":1,"state":0}
```

- `state`: `0` = RED, `1` = GREEN
- Incoming messages are applied only when `system_id` and `cue_group` match this node's configured values
- Packets from this device's own IP are ignored

### Why broadcast, not multicast?

**Broadcast** is used because it is simple for laptops and scripts (bind to the port, enable `SO_BROADCAST` to send) and shows up directly in Wireshark. **Multicast** is better for large routed networks, but many WiFi access points handle it poorly without IGMP snooping configuration.

### Wireshark

```
udp.port == 45271
```

### Laptop listener example (Python)

```python
import socket

PORT = 45271
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("", PORT))

while True:
    data, addr = sock.recvfrom(1024)
    print(addr, data.decode("utf-8", errors="replace").strip())
```

### Sending from a laptop

Use your LAN broadcast address (not always `255.255.255.255`):

```python
import socket

PORT = 45271
msg = b'{"system_id":1,"cue_group":1,"cue":1,"state":1}\r\n'
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
sock.sendto(msg, ("192.168.1.255", PORT))  # replace with your subnet broadcast
```

## Project layout

```
cue_light_webserver.ino   Main sketch (web server, WiFi, API route)
config.h                  Pins, defaults, firmware version
CueIO.*                   Buttons and lamp outputs
UdpCue.*                  UDP broadcast send/receive
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
| Cues do not sync between devices | Match **System ID** and **Cue Group**; all devices must be on the same WiFi/LAN. Check AP client isolation is off. |
| UDP cue sync warning on boot | WiFi not connected yet (common in AP-only mode). Reconnect to your LAN. Device still works locally via buttons. |
