# Cue Light — user manual

Firmware **v1.5.0**. This is how to operate a cue-light board on a show: buttons, lamps, WiFi setup, the Heltec screen, and LoRa.

Two hardware types share the same firmware:

| Board | What you get |
|-------|----------------|
| **Heltec WiFi LoRa 32 V3** | Onboard **PRG** button, OLED, battery, optional LoRa radio |
| **NodeMCU v2** | Two header buttons (D1 / D2), no screen, WiFi only |

Pressing a cue button toggles that cue between **red** and **green**. Every other board with the same **System ID** and **Cue Group** follows (WiFi, LoRa, or both).

---

## Heltec WiFi LoRa 32 V3

Stock firmware (`CUE_LOCAL_ONE`) uses the onboard **PRG** button as Cue 1. GPIO 2 on the header is unused unless you rebuild with a different `CUE_LOCAL` setting (see [Other firmware layouts](#other-firmware-layouts)).

**Do not hold PRG while you power or reset the board.** GPIO 0 held at reset puts the ESP32 into flash download mode. Let it boot, then use the button.

### PRG button (GPIO 0)

| When | Action | What happens |
|------|--------|----------------|
| **Power-on / reset** | Do **not** hold PRG | Normal boot |
| **Splash + blinking lamps** (first ~8 seconds) | Hold **3 seconds** | Wipe WiFi credentials and open the setup access point |
| **Splash + blinking lamps** | Release before 3 seconds | Cancel wipe; boot continues |
| **After boot (running)** | **Short press** (tap and release) | Toggle Cue 1 red ↔ green, and sync that change to peers |
| **After boot (running)** | Hold **3 seconds** | Power off: OLED shows `-OFF-`, lamps go dark, deep sleep |
| **Powered off (sleep)** | **Short press** | Wake; board boots normally (wipe window is skipped after a sleep wake) |
| **Powered off (sleep)** | USB connected | Battery still charges; board stays asleep until you tap PRG |

A short press is anything you release before 3 seconds (the firmware waits until you let go). Holding through 3 seconds is always power-off after boot, never a WiFi wipe.

### Cue 2 button (header GPIO 2)

Not used on stock Heltec firmware. Wire a momentary switch from GPIO 2 to GND only if you rebuild as `CUE_LOCAL_ALL` or `CUE_LOCAL_TWO`.

### Lamps and LEDs

| Output | Meaning |
|--------|---------|
| Cue 1 RGB (GPIO 4 red / 5 green) | Show cue. Starts **red**. Short PRG toggles it. |
| Cue 2 RGB (GPIO 6 / 7) | Wired on the header but not driven as a local cue on stock firmware. Network state for Cue 2 is still stored and synced. |
| Onboard LED (GPIO 35) | On when the local cue is **green**, off when **red** |
| During boot wipe window | Cue lamps blink red/green about 4 times per second |
| After a successful WiFi wipe | Lamps go green; OLED shows **WIFI WIPED** / **AP SETUP** for 2 seconds |

### OLED

Top row: device name **`CueLight-XXXX`** (last two bytes of the WiFi MAC — same as the setup SSID) and battery percent.

Second row: IP address, or `AP 192.168.4.1` in setup mode, or `LORA ONLY` if WiFi is off, or `NO WIFI`. On the right: a small radio icon if LoRa is on, then a number and a node icon. That number is other boards this unit currently sees (WiFi peers plus recently heard LoRa radios).

Below that: a large **CUE 1** box, filled when green, outline when red.

Splash (boot) also shows the firmware version and `HOLD PRG FOR 3S` / `TO WIPE CONFIG` while the lamps blink.

---

## NodeMCU v2

Two buttons on the header, both momentary to GND.

### Cue 1 button (D1 / GPIO 5) — primary

| When | Action | What happens |
|------|--------|----------------|
| **Boot, lamps blinking** (first ~8 seconds) | Hold **3 seconds** | Wipe WiFi credentials and open the setup access point |
| **Boot, lamps blinking** | Release before 3 seconds | Cancel wipe; boot continues |
| **After boot** | **Press** | Toggle Cue 1 red ↔ green, and sync |

There is no power-off / sleep on NodeMCU. A long hold after boot does **not** shut the board down.

### Cue 2 button (D2 / GPIO 4)

| When | Action | What happens |
|------|--------|----------------|
| Anytime after boot | **Press** | Toggle Cue 2 red ↔ green, and sync |
| Boot wipe window | — | Does **not** wipe WiFi. Only D1 does. |

### Lamps and LED

| Output | Meaning |
|--------|---------|
| Cue 1 RGB (D5 red / D6 green) | Cue 1. Starts **red**. |
| Cue 2 RGB (D7 red / D8 green) | Cue 2. Starts **red**. |
| Onboard LED (D4) | On when Cue 1 is **green** (this LED is active-low) |
| During boot wipe window | Cue lamps blink red/green |

---

## First-time setup (both boards)

1. Power the board. It tries saved WiFi for 10 seconds.
2. If that fails (or you just wiped), it starts an access point:
   - **SSID:** `CueLight-XXXX` (XXXX matches the Heltec OLED title)
   - **Password:** `123456789`
3. Join that network. Open the captive portal, or browse to `http://192.168.4.1/setup`.
4. Pick the show WiFi. Set **System ID** and **Cue Group** (defaults: both `1`). Every board that should follow each other must use the **same** pair.
5. Heltec only: optionally check **Enable LoRa** and set **LoRa Channel** `0`–`7`. Every radio Heltec in the group must use the same channel. Uncheck **Enable WiFi** to run LoRa only (OLED shows **LORA ONLY**; no web dashboard until you wipe).
6. Save. The board joins the LAN, or reboots into LoRa-only if you turned WiFi off. Note the IP from the serial monitor (115200,N,8,1) or from the Heltec OLED.

Change WiFi or IDs later at `http://<board-ip>/setup`.

### Wipe WiFi and start over

Use this when you need a new network or the board will not join.

1. Power **on without** holding the primary button (especially on Heltec).
2. Wait for the lamps to blink (Heltec: OLED splash is up).
3. Hold the **primary** button for **3 seconds** (Heltec **PRG**, NodeMCU **D1**).
4. Heltec shows **WIFI WIPED**. The setup AP starts immediately; it will not reconnect to the old network. Wipe also writes **Enable WiFi** back on, so a LoRa-only board can reach `/setup` again.

---

## Running a show

1. Power every board. Heltecs show IP and cue state on the OLED.
2. Confirm **System ID** and **Cue Group** match. Heltecs that use radio: LoRa on, same channel.
3. Press a cue button. Local lamp toggles; peers should follow within about **300 ms** on WiFi, or on the next LoRa packet if you are radio-only.
4. The web page at `http://<board-ip>/` is a **status display** (Cue 1 and Cue 2). It does not have buttons. Use the physical buttons (or `POST /api/cues`) to change cues.

Cues are independent. Toggling Cue 1 does not change Cue 2.

Boards are equals — there is no master. A late-joining board picks up current state over WiFi (mDNS + HTTP) and/or LoRa beacons (~every 5 seconds).

### LoRa (Heltec)

Enable it on `/setup`. It is off by default.

- Same cue snapshot as WiFi (both Cue 1 and Cue 2, even if the OLED only shows one).
- Channel `0` is 915.0 MHz; each step up is +0.2 MHz.
- If LoRa is on **and** the board is on show WiFi, it **relays**: WiFi changes go out over LoRa, LoRa changes are POSTed to WiFi peers. That is how a NodeMCU on the LAN can follow a Heltec that is being used as a radio.
- Uncheck **Enable WiFi** to run **LoRa only**: the WiFi radio is powered off, the web server is not started, and the OLED IP line shows **LORA ONLY**. LoRa is forced on. NodeMCUs will not hear these boards unless another Heltec stays on show WiFi as a relay. To get `/setup` back, power-cycle and wipe (hold **PRG** 3 s while the lamps blink); that turns Enable WiFi on and starts the setup AP.

The OLED node count stays at **0** for LoRa until another Heltec on the same channel and IDs is heard (beacons every few seconds). WiFi peer count only goes up when mDNS finds other boards on the LAN.

Show WiFi must allow device-to-device traffic (client isolation **off**) for HTTP sync.

---

## Button reference (all functions)

Primary button = Heltec **PRG** = NodeMCU **D1**. Secondary = Heltec header **GPIO 2** = NodeMCU **D2**.

| Function | Heltec | NodeMCU |
|----------|--------|---------|
| Toggle Cue 1 | Short **PRG** (stock firmware) | Press **D1** |
| Toggle Cue 2 | Not on stock firmware (see below) | Press **D2** |
| Wipe WiFi | After splash, hold **PRG** 3 s while lamps blink (8 s window) | At boot, hold **D1** 3 s while lamps blink |
| Cancel wipe | Release PRG before 3 s | Release D1 before 3 s |
| Power off | Hold **PRG** 3 s **after** boot | — |
| Wake from sleep | Short **PRG** | — |
| Enter USB flash mode | Hold **PRG** *during* reset (avoid this unless you mean to flash) | — (usual USB serial flash) |

---

## Other firmware layouts

`CUE_LOCAL` in `config.h` is a compile-time choice. Stock Heltec is **ONE**; stock NodeMCU is **ALL**.

| `CUE_LOCAL` | Local buttons | Heltec OLED |
|-------------|---------------|-------------|
| **ALL** | Primary → Cue 1, secondary → Cue 2 | Two cue boxes |
| **ONE** (Heltec default) | Primary → Cue 1; secondary unused | Full-width Cue 1 |
| **TWO** | Primary → Cue 2; secondary → Cue 1 | Full-width Cue 2 |

WiFi wipe and Heltec power-off / wake **always** use the physical primary button (PRG / D1), even when `CUE_LOCAL_TWO` maps that button to Cue 2 for toggling.

---

## See also

- [README](./README.md) — build, flash, pin map
- [docs/operations-guide.md](./docs/operations-guide.md) — deployment and troubleshooting
- [docs/PINOUT.md](./docs/PINOUT.md) — wiring the RGB lamps and buttons
