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
managed via the NAS dashboard. The device hosts a minimal config WebUI for adjusting
the NAS URL, brightness, and triggering refreshes/reboots.

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
| Poll interval | 60 s                                      |
| Auth          | Optional Bearer token via `SECRET_NAS_API_KEY` |

NAS URL is editable at runtime via the device WebUI and persisted in NVS.

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

| File                     | Purpose                                                     |
|:-------------------------|:------------------------------------------------------------|
| `src/main.cpp`           | setup(), loop(), init orchestration, bus refresh             |
| `src/display.cpp/.h`     | All TFT drawing — header, panels, status bar                |
| `src/bus_api.cpp/.h`     | NAS fetch, JSON parse, StopData/Departure structs           |
| `src/time_mgr.cpp/.h`    | ezTime NTP init, time/date/day helpers, TZ offset           |
| `src/web_server.cpp/.h`  | AsyncWebServer, LittleFS serve, device config API           |
| `src/config.cpp`         | NAS URL: NVS load/save, `getNasUrl()` / `setNasUrl()`      |
| `src/debug.cpp`          | `dbgTimestamp()` — wall-clock or uptime fallback prefix     |
| `include/config.h`       | Tuneable constants + NAS config declarations                |
| `include/debug.h`        | Levelled debug macros with wall-clock timestamps            |
| `include/secrets.h`      | Gitignored — WiFi creds + NAS API key                       |
| `data/www/index.html`    | Device config SPA — served from LittleFS                    |

## Key Data Structures (`bus_api.h`)

```cpp
struct Departure {
  char   route[8];         // e.g. "501"
  char   clockTime[6];     // local HH:MM from NAS
  char   destination[32];  // e.g. "Gladesville - Jordan St"
  time_t epochUTC;         // departure UTC epoch
  time_t epochPlanned;     // epochUTC - delaySecs (0 if no delay)
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
used deliberately here to save flash space. Upgrade to VLW is a Phase 3 item.

## Display Layout

Landscape 320x240. Header 28px, then 2x2 grid of stop panels (160x106 each).

Each panel: stop name (cyan) + up to 3 departure rows.
Each row: route (white), real-time indicator (filled circle=green/`~`=grey), minutes or day abbr, clock time.

Minutes display logic:
- `<=0`: "Now" (orange)
- `1-59`: "{n}m" (green if <10, yellow if >=10)
- `>=60`: "{h}h{mm}m" (yellow)
- Non-today departures: 3-letter day abbreviation (grey, e.g. "Mon")

Footer: `upd HH:MM` — dim grey, bottom-right corner, updated on each NAS fetch.

## Refresh Strategy

| What             | Interval | Mechanism                                |
|:-----------------|:---------|:-----------------------------------------|
| TFT clock header | 1 s      | ezTime `events()` + `drawHeader()`       |
| TFT stop panels  | 15 s     | `recalcMinutes()` from stored epoch      |
| NAS fetch        | 60 s     | `fetchAllStops()` — single HTTP GET      |
| WebUI device poll| 10 s     | `fetch('/api/device')` from browser JS   |
| NAS URL change   | on-demand| `consumeStopRefreshRequest()` in loop()  |

## Device Web API Endpoints

| Method | Path               | Description                                       |
|:-------|:-------------------|:--------------------------------------------------|
| GET    | `/`                | Device config SPA (served from LittleFS `/www/`)  |
| GET    | `/api/device`      | JSON: IP, hostname, NAS URL, errors, heap, uptime |
| GET    | `/api/state`       | Cached stop data (re-serialised, mirrors NAS schema) |
| GET    | `/api/refresh`     | Queue immediate NAS fetch                         |
| POST   | `/api/nas-url`     | Body: `{"url":"..."}` — persist + queue refresh   |
| POST   | `/api/brightness`  | Body: `{"value":0-255}` — set backlight           |
| POST   | `/api/reboot`      | Restart device                                    |

## Device Config WebUI (`data/www/index.html`)

Single-page dark-themed config interface served from LittleFS. **Not** a bus data
dashboard — the WebUI is for device management only.

Features:
- Device diagnostics: IP, hostname, heap, uptime, fetch age, errors
- NAS URL editor with save + refresh trigger
- Link to open NAS dashboard in new tab
- Brightness slider (20-255)
- Force refresh and reboot buttons

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

Two upload steps are required:
1. **Firmware:** `pio run -t upload` (or `pio run -d client/ -t upload` from monorepo root)
2. **Filesystem:** `pio run -t uploadfs` — uploads `data/www/` to LittleFS partition

Upload speed: 230400 baud. Port: auto-detected by PlatformIO.
Hostname after provisioning: `cyd-busstop` (mDNS + OTA).

## Init Sequence (`setup()`)

1. `initDisplay()` — TFT + backlight
2. `initWiFi()` — WiFiManager with optional secrets.h seed
3. `initTime()` — ezTime NTP sync (30s timeout)
4. `initNasConfig()` — load NAS URL from NVS or use default
5. `initBusApi()` — zero stop data arrays
6. `initWebServer()` — LittleFS mount + AsyncWebServer routes
7. `initOTA()` — ArduinoOTA with status bar feedback
8. First `performBusRefresh()` — fetch + draw
9. Initial header draw + log free heap

## Known Issues / Notes

- CYD has no PSRAM — no framebuffer; display is write-only
- `TIME_24HR_DEFAULT = false` — 12hr clock; set `true` in `config.h` for 24hr
- `STOP_COUNT = 4` is the display slot maximum — NAS can have fewer; unused slots show "No data"
- Stop names from NAS > 23 chars are silently truncated (`STOP_NAME_MAX = 24`)
- NAS URL is stored in NVS namespace `busstop2` — differs from v1 `busstop` intentionally
- `fetchAllStops()` blocks loop() for ~50-200ms (LAN HTTP); acceptable vs v1's ~10s TLS
- LittleFS `begin(true)` formats partition on first boot if blank — expected behaviour
- Device WebUI brightness change is not persisted across reboots (intentional)
- Built-in fonts require explicit `-DLOAD_GLCD=1 -DLOAD_FONT2=1 -DLOAD_FONT4=1` build flags
- Vertical divider in `drawDividers()` draws 28px past screen bottom (length `240` should be `240 - HEADER_H`)
- `handleWebServer()` is a no-op — AsyncWebServer is fully event-driven, but the call is kept for future use
- `DynamicJsonDocument` is used in several handlers — could be `StaticJsonDocument` for less fragmentation
