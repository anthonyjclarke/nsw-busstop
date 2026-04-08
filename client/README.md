# CYD_BusStop_NSW Client

ESP32 bus departure display for the **ESP32-2432S028R (CYD 2.8")**. This is the
**client** component of the [nsw-busstop](../README.md) monorepo: it fetches
pre-processed bus data from the companion [server](../server/), caches the
fixed 3-row feed for the TFT, and serves a local browser dashboard. The device
web UI can use the NAS-backed 1 to 8 rows-per-stop setting from `/config`,
while the TFT intentionally stays fixed at 3 departures per stop.

**The server must be running on your local network for this device to show bus data.**

For the project overview, architecture diagram, screenshots, and deployment
guide, see the [top-level README](../README.md).

---

## Hardware

| Component  | Detail                               |
|:-----------|:-------------------------------------|
| Board      | ESP32-2432S028R (CYD 2.8")           |
| Display    | ILI9341, 240x320, SPI                |
| Touch      | XPT2046 (unused in this project)     |
| Flash      | 4 MB, custom partition table         |
| USB-Serial | CP2102                               |

---

## Setup

### 1. Create secrets file

```bash
cp include/secrets.h.example include/secrets.h
```

`include/secrets.h` is a local-only file and is intentionally gitignored, so it
will not appear when you clone or pull this repo on a new machine. Create it
from `include/secrets.h.example` on each machine that builds or flashes the
client.

Edit `include/secrets.h` with your WiFi credentials if you want to pre-fill the
WiFiManager portal, and set `SECRET_NAS_API_KEY` only when the server requires
API authentication.

### 2. Build and flash

```bash
# From this directory:
pio run -t upload

# Or from the monorepo root:
pio run -d client/ -t upload
```

Upload speed: 230400 baud. Port: auto-detected.

### 3. First boot

1. Connect to the `CYD-BusStop` WiFi AP from your phone
2. Enter your WiFi credentials in the captive portal
3. Device connects, syncs time, fetches bus data from the server
4. If the server isn't at the default URL, visit
   `http://cyd-busstop.local/config` to change the NAS URL

### 4. OTA updates

```bash
pio run -t upload --upload-port cyd-busstop.local
```

---

## Configuration (`include/config.h`)

| Constant             | Default                       | Purpose                |
|:---------------------|:------------------------------|:-----------------------|
| `NAS_DEFAULT_URL`    | `"http://192.168.1.100:8081"` | Default server URL     |
| `POLL_INTERVAL_MS`   | `60000`                       | NAS fetch interval     |
| `BRIGHTNESS_DEFAULT` | `200`                         | Backlight (0-255)      |
| `TIME_24HR_DEFAULT`  | `false`                       | 12hr/24hr clock        |
| `WIFI_AP_NAME`       | `"CYD-BusStop"`               | Captive portal AP name |
| `OTA_HOSTNAME`       | `"cyd-busstop"`               | mDNS + OTA hostname    |

The NAS URL can also be changed at runtime from the device `/config` page — it
is persisted to NVS and takes effect on reboot.

The TFT on this client is not configurable from the device and remains fixed
at 3 departures per stop. The local browser dashboard can use the NAS-backed
1 to 8 rows-per-stop setting from the device `/config` page.

---

## Project Structure

```
client/
├── platformio.ini
├── partitions_custom.csv
├── include/
│   ├── config.h               # Tuneable constants
│   ├── debug.h                # Leveled DBG_* macros
│   ├── secrets.h              # Gitignored — WiFi + NAS API key
│   └── secrets.h.example      # Template for secrets.h
├── src/
│   ├── main.cpp               # setup(), loop(), init orchestration
│   ├── display.cpp/.h         # TFT drawing — header, panels, status bar
│   ├── bus_api.cpp/.h         # NAS fetch, JSON parse, data structs
│   ├── config.cpp             # Default stop seed + NAS URL NVS persistence
│   ├── time_mgr.cpp/.h        # ezTime NTP, time/date/day helpers
│   ├── web_server.cpp/.h      # AsyncWebServer — dashboard + config pages
│   └── debug.cpp              # Wall-clock timestamp for debug output
```

---

## Display Layout

Landscape 320x240. Header bar with time and date, then a 2x2 grid of stop
panels. Each panel shows the stop name and up to 3 departure rows with route
number, compact `LIVE` / `SCH` status label, countdown, and a right-aligned
clock time. Rows are intentionally tightened to improve density without adding
more than 3 departures per stop.

Footer shows `Server Status` with a status dot (green = NAS reachable, red =
last poll failed) and `upd HH:MM` on the right. A small orange dot appears
beside the stop name when alerts are active. Cached countdowns continue aging
locally between successful polls, and the last good cache is preserved when
the NAS reports upstream TfNSW errors such as `HTTP 429`.

The stop list comes from the NAS `/api/state` response order; the client no
longer offers local stop editing — configure stops on the server dashboard.
The TFT layout stays fixed at 3 rows per stop for compatibility and screen
fit. The browser dashboard served by the device can proxy the NAS dashboard
feed and show 1 to 8 rows per stop.

---

## Device Web Interface

Access at the device's IP address or `http://cyd-busstop.local/`.

| Method | Path          | Description                              |
|:-------|:--------------|:-----------------------------------------|
| GET    | `/`           | Live local dashboard (1-8 rows via NAS)  |
| GET    | `/config`     | Device config + system stats page        |
| GET    | `/api/state`  | Fixed local TFT/cache JSON               |
| GET    | `/api/dashboard-state` | Browser dashboard JSON (NAS-backed when available) |
| GET    | `/api/config` | System stats + NAS URL/WebUI rows as JSON |
| POST   | `/api/config` | Update NAS URL and/or WebUI rows         |

The `/config` page shows uptime, firmware build date, WiFi SSID/IP/RSSI/MAC,
hostname, free heap, max alloc block, last-fetch age, and chip info. It also
lets you edit the NAS server URL and the NAS-backed browser rows-per-stop
setting at runtime. The TFT remains fixed at 3 rows.

---

## Debug Output

Set `DEBUG_LEVEL` in `platformio.ini` build flags (default: 3):

| Level | Macro         | Output                     |
|:------|:--------------|:---------------------------|
| 1     | `DBG_ERROR`   | Critical failures          |
| 2     | `DBG_WARN`    | Unexpected but recoverable |
| 3     | `DBG_INFO`    | State changes, init        |
| 4     | `DBG_VERBOSE` | Frequent events, values    |

All debug output is prefixed with wall-clock time after NTP sync, or uptime
(`t+12.345s`) before sync.

```text
[15:05:24] [INFO]  Fetching from NAS: http://192.168.1.100:8081/api/state
[15:05:24] [INFO]  NAS response: 2014 bytes, heap: 210356
[15:05:24] [INFO]  NAS fetch complete: 4/4 stops updated
```

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for version history.

---

## Licence

Personal project — not licensed for redistribution.
