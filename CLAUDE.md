# nsw-busstop — Monorepo Context

## Repository Structure

This is a monorepo containing two tightly coupled components:

| Component  | Path       | Tech                          | Runs on              |
|:-----------|:-----------|:------------------------------|:---------------------|
| **Server** | `server/`  | Python 3.12, FastAPI, SQLite  | Synology DS423+ NAS  |
| **Client** | `client/`  | ESP32 Arduino, TFT_eSPI       | CYD 2.8" (ESP32-2432S028R) |

Each component has its own `CLAUDE.md` with detailed context:
- Server: [server/CLAUDE.md](server/CLAUDE.md)
- Client: [client/CLAUDE.md](client/CLAUDE.md)

## Architecture

```
[TfNSW API] --> [server/ on NAS :8081] --> [client/ ESP32 GET /api/state]
                       ^
               (stop config, polling,
                JSON normalisation)
```

The server polls TfNSW departure APIs every 60s, stores stop configuration in
SQLite, and serves pre-processed JSON via `/api/state`. The ESP32 client fetches
this JSON over plain HTTP and renders bus departures on the TFT display.

**The client is useless without the server running.**

## Cross-Component Contract

The server's `/api/state` JSON schema is the interface between components:

```json
{
  "time": "10:30:00", "date": "Fri, 4 Apr",
  "now": 1743742200, "tzOff": 36000,
  "lastRefresh": "...", "lastError": null,
  "stops": [
    { "id": "2112130", "name": "To Gladesville", "valid": true,
      "fetch_age": 45, "alert": "",
      "departures": [
        { "route": "500", "clock": "10:35", "minutes": 5,
          "epoch": 1743742500, "rt": true, "delay": 120, "dest": "Circular Quay" }
      ]
    }
  ]
}
```

Changes to this schema must be coordinated across both components.

## Secrets

| Secret             | Location                          | Purpose                     |
|:-------------------|:----------------------------------|:----------------------------|
| TfNSW API key      | `server/.env` (NAS only)          | TfNSW departure API access  |
| NAS auth creds     | `server/.env` (NAS only)          | Dashboard login             |
| NAS API key        | `server/.env` + `client/secrets.h`| Bearer token for `/api/state` |
| WiFi credentials   | `client/include/secrets.h`        | WiFiManager seed            |

Both `.env` and `secrets.h` are gitignored. Templates: `server/.env.example` and
`client/include/secrets.h.example`.

## Development

- Open `nsw-busstop.code-workspace` in VSCode for multi-root workspace
- PlatformIO auto-detects `client/platformio.ini` via workspace setting
- Server dev: `cd server && pip install -r requirements.txt && uvicorn app.main:app --reload`
- Client build: `pio run -d client/` or use PlatformIO sidebar

## Deployment

- **Server**: Copy `server/` contents to NAS, configure `.env`, build via Container Manager
- **Client**: `pio run -d client/ -t upload && pio run -d client/ -t uploadfs`
- Full NAS deployment guide in [README.md](README.md)

## Conventions

- Commits: `type(scope): description` — e.g. `feat(server): add health endpoint`
- Branches: `dev` (default development), `main` (stable/flashed releases)
- Changelogs: `client/CHANGELOG.md` for client, top-level for cross-component
