# CYD_BusStop_NSW Client — Project Context

## IMPORTANT: This is the client component

This ESP32 firmware is the **client** half of a server/client architecture.
It fetches pre-processed bus data from the companion server (`../server/`).
The server must be running on your local network for this device to display any data.

- **Server component:** [../server/](../server/) — Python FastAPI on Synology NAS
- **Monorepo context:** [../CLAUDE.md](../CLAUDE.md)

## What It Does

Displays the next 3 bus departures for up to 4 NSW stops near Ryde/Putney on the
CYD TFT display. Departure data is fetched from a local NAS server (Python FastAPI
at `192.168.1.100:8081`) rather than calling TfNSW directly. Stop configuration is
managed on the NAS; the client mirrors the stop list returned by the server.

## Target Hardware

ESP32-2432S028R (CYD 2.8") — ILI9341, 240x320, no PSRAM.
Standard CYD pin assignments apply — see global CLAUDE.md.

## Architecture

```
[TfNSW API] --> [NAS server :8081] --> [ESP32 GET /api/state]
                       ^
               (stop config, polling,
                JSON normalisation)
```

**Critical:** The ESP32 device is now a **thin client**. All TfNSW API integration, stop configuration, and data processing happens on the server. The ESP32 only fetches pre-processed JSON and renders it.

## NAS Server

| Detail        | Value                                     |
|:--------------|:------------------------------------------|
| Default URL   | `http://192.168.1.100:8081`               |
| NVS key       | `nasUrl` in namespace `busstop2`          |
| Endpoint      | `GET /api/state` -> JSON payload          |
| Poll interval | 90 s                                      |
| Auth          | Optional Bearer token via `SECRET_NAS_API_KEY` |

NAS URL is editable at runtime via `setNasUrl()` and persisted in NVS.

## NAS JSON Schema (`/api/state`)

```json
{
  "time": "10:30:00",
  "date": "Fri, 4 Apr",
  "now": 1743742200,
  "tzOff": 36000,
  "lastRefresh": "2026-04-04T10:30:00+10:00",
  "lastError": null,
  "stops": [
    {
      "id": "2112130",
      "name": "To Gladesville",
      "valid": true,
      "fetch_age": 45,
      "alert": "",
      "departures": [
        { "route": "500", "clock": "10:35", "minutes": 5,
          "epoch": 1743742500, "rt": true, "delay": 120, "dest": "Circular Quay" }
      ]
    }
  ]
}
```

## Module Structure

| File                     | Purpose                                              |
|:-------------------------|:-----------------------------------------------------|
| `src/main.cpp`           | setup(), loop(), init orchestration, bus refresh      |
| `src/display.cpp/.h`     | All TFT drawing — header, panels, status bar         |
| `src/bus_api.cpp/.h`     | NAS fetch, JSON parse, StopData/Departure structs    |
| `src/time_mgr.cpp/.h`    | ezTime NTP init, time/date/day helpers, TZ offset    |
| `src/web_server.cpp/.h`  | AsyncWebServer, PROGMEM WebUI, local state API       |
| `src/config.cpp`         | Default stop seed + NAS URL NVS load/save            |
| `src/debug.cpp`          | `dbgTimestamp()` — wall-clock or uptime fallback      |
| `include/config.h`       | Tuneable constants + NAS config declarations         |
| `include/debug.h`        | Levelled debug macros with wall-clock timestamps     |
| `include/secrets.h`      | Gitignored — WiFi creds + NAS API key                |

## Key Data Structures (`bus_api.h`)

```cpp
struct Departure {
  char   route[8];         // e.g. "501"
  char   clockTime[6];     // local HH:MM from NAS
  char   destination[32];  // e.g. "Gladesville - Jordan St"
  time_t epochUTC;         // departure UTC epoch
  int    minutesUntil;     // refreshed by recalcMinutes() every 15s
  int    delaySecs;        // positive = late, negative = early
  bool   isRealtime;       // true = live GPS, false = scheduled
  bool   valid;
};

struct StopData {
  Departure departures[3]; // DEPARTURES_PER_STOP
  uint8_t   count;
  bool      valid;         // at least one departure
  bool      hasAlerts;
  char      alertText[64];
  uint32_t  lastFetchMs;   // millis() of last NAS fetch
};
```

## Fonts

Using TFT_eSPI built-in fonts (requires `-DLOAD_GLCD=1 -DLOAD_FONT2=1 -DLOAD_FONT4=1`):
- Font 4 (26px) — header time
- Font 2 (16px) — stop names, departure rows, status bar

**Note:** This departs from the global CLAUDE.md VLW-only rule. Built-in fonts are
used deliberately here to save flash space.

## Display Layout

Landscape 320x240. Header 28px, then 2x2 grid of stop panels (160x106 each).

Each panel: stop name (cyan) + up to 3 departure rows.
Each row: route (white), compact `LIVE`/`SCH` label, minutes or day abbr,
right-aligned clock time. Stop names are ellipsized when needed. A small
orange dot beside the stop name indicates active alerts.

Minutes display logic:
- `<=0`: "Now" (orange)
- `1-59`: "{n}m" (green if <10, yellow if >=10)
- `>=60`: "{h}h{mm}m" (yellow)
- Non-today departures: 3-letter day abbreviation (grey, e.g. "Mon")

Footer: `Server Status` label with a green/red dot beside it, `upd HH:MM` on the
right, updated after each NAS fetch attempt.

If the NAS responds with a non-empty `lastError` (for example TfNSW `HTTP 429`
rate limiting), the client preserves the last good TFT cache instead of
blanking the display.

## Refresh Strategy

| What             | Interval | Mechanism                                |
|:-----------------|:---------|:-----------------------------------------|
| TFT clock header | 1 s      | ezTime `events()` + `drawHeader()`       |
| TFT stop panels  | 15 s     | `recalcMinutes()` from stored epoch      |
| NAS fetch        | 60 s     | `fetchAllStops()` — single HTTP GET      |
| WebUI poll       | 15 s     | `fetch('/api/state')` from browser JS    |

## Device Web API Endpoints

| Method | Path          | Description                                     |
|:-------|:--------------|:------------------------------------------------|
| GET    | `/`           | Bus departures WebUI (PROGMEM HTML + JS)        |
| GET    | `/config`     | Device config + system stats page               |
| GET    | `/api/state`  | Cached stop data (re-serialised, mirrors NAS)   |
| GET    | `/api/config` | System stats + current NAS URL as JSON          |
| POST   | `/api/config` | Update NAS URL (body: `{"nasUrl":"..."}`)       |

## WebUI (`/` and `/config`)

Two PROGMEM-embedded dark-themed pages served by AsyncWebServer. No filesystem
assets — all HTML/CSS/JS is compiled into flash.

**`/` — Departures dashboard**
- Mirrors the client's current cached state (not the NAS directly)
- Live bus departures with real-time badges, delay pills, day labels
- Auto-polls `/api/state` every 15s, re-renders every 5s
- Link to `/config`

**`/config` — Device config + stats**
- NAS URL editor (persists to NVS, takes effect on reboot)
- System stats: uptime, firmware build, WiFi SSID/IP/RSSI/MAC, hostname, free
  heap, max alloc block, last-fetch age, chip info
- Auto-refreshes stats every 10s
- Links to `/` and raw `/api/state` JSON

## Secrets (`include/secrets.h`)

Gitignored. Template at `include/secrets.h.example`. Three defines required:

```cpp
#define SECRET_WIFI_SSID    "your-ssid"
#define SECRET_WIFI_PASS    "your-password"
#define SECRET_NAS_API_KEY  ""   // Bearer token for NAS auth (empty = no auth)
```

## Libraries (from `platformio.ini`)

| Library                              | Purpose                     |
|:-------------------------------------|:----------------------------|
| `bodmer/TFT_eSPI@^2.4.76`           | TFT display driver          |
| `tzapu/WiFiManager`                  | Captive portal provisioning |
| `ropg/ezTime`                        | NTP + timezone handling     |
| `bblanchon/ArduinoJson@^6.21.0`     | JSON parse/serialise        |
| `mathieucarbou/AsyncTCP#v3.3.2`      | Async TCP (GitHub ref)      |
| `mathieucarbou/ESPAsyncWebServer#v3.4.0` | Async web server (GitHub ref) |

ArduinoOTA is framework built-in (no lib_dep entry needed).

## Building & Flashing

Firmware upload only (no filesystem upload needed — WebUI is PROGMEM-embedded):

```sh
pio run -t upload          # from client/
pio run -d client/ -t upload   # from monorepo root
```

Upload speed: 230400 baud. Port: auto-detected by PlatformIO.
Hostname after provisioning: `cyd-busstop` (mDNS + OTA).

## Init Sequence (`setup()`)

1. `initDisplay()` — TFT + backlight
2. `initWiFi()` — WiFiManager with optional secrets.h seed
3. `initTime()` — ezTime NTP sync (30s timeout)
4. `initStopConfig()` — seed compiled stop defaults until NAS sync
5. `initBusApi()` — zero stop data arrays
6. `initWebServer()` — AsyncWebServer routes
7. `initOTA()` — ArduinoOTA with status bar feedback
8. First `performBusRefresh()` — fetch + draw
9. Initial header draw + log free heap

## Known Issues / Notes

- CYD has no PSRAM — no framebuffer; display is write-only
- `TIME_24HR_DEFAULT = false` — 12hr clock; set `true` in `config.h` for 24hr
- `STOP_COUNT = 4` is the display slot maximum — NAS can have fewer; unused slots show "No data"
- Stop names from NAS > 23 chars are silently truncated (`STOP_NAME_MAX = 24`)
- NAS URL is stored in NVS namespace `busstop2` — differs from v1 `busstop` intentionally
- The stop list is not persisted locally; it is replaced by the server response order on each successful NAS fetch
- `fetchAllStops()` blocks loop() for ~50-200ms (LAN HTTP); acceptable vs v1's ~10s TLS
- Built-in fonts require explicit `-DLOAD_GLCD=1 -DLOAD_FONT2=1 -DLOAD_FONT4=1` build flags
