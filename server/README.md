# NSW BusStop Server

Standalone Docker project for managing TfNSW bus-stop data on the DS423 without changing the existing `CYD_BusStop_NSW` PlatformIO project.

This app is designed as the future local source of truth for:

- TfNSW API polling
- dynamic stop configuration
- web dashboard and stop editor
- JSON endpoints for future ESP32 display clients

The existing ESP32 project remains untouched in `~/PlatformIO/Projects/CYD_BusStop_NSW`.

## Current v1 scope

- Python single-container app
- FastAPI backend with server-rendered dashboard
- SQLite persistence for stop configuration
- auth toggle via `AUTH_ENABLED`
- TfNSW polling with a current-style live departures dashboard
- dynamic stop list seeded with the current Ryde/Putney defaults
- Docker-managed named volume for persistent app data on Synology
- dashboard shows masked live runtime config so active NAS settings are visible

## Ports

The container listens on port `8081` by default.

Open:

- `http://<diskstation-ip>:8081`
- Example: `http://192.168.1.100:8081`

## Authentication

Authentication is now controlled by `AUTH_ENABLED` in `.env`.

Recommended values:

- local bring-up: `AUTH_ENABLED=false`
- before any external exposure: `AUTH_ENABLED=true`

Relevant variables:

- `AUTH_ENABLED`
- `APP_USERNAME`
- `APP_PASSWORD`
- `SESSION_SECRET`

When auth is enabled, the app uses form login backed by an encrypted session cookie.

Do not expose this app to the internet unless:

- `AUTH_ENABLED=true`
- `APP_PASSWORD` is changed from any placeholder value
- `SESSION_SECRET` is long and random
- Synology reverse proxy and HTTPS are in front of it

## Environment setup

1. Copy `.env.example` to `.env`
2. Set your TfNSW API key and login credentials

Required variables:

```env
TFNSW_API_KEY=your-api-key
AUTH_ENABLED=true
APP_USERNAME=admin
APP_PASSWORD=your-password
SESSION_SECRET=a-long-random-string
TIMEZONE=Australia/Sydney
POLL_INTERVAL_SECONDS=60
PORT=8081
```

## Source Of Truth

For Synology deployments, the live configuration is the `.env` file in the NAS project folder selected in Container Manager, not necessarily the file currently open in VS Code on another machine.

This project now shows masked runtime values on the dashboard so you can verify what the running container is actually using.

If the dashboard values do not match your editor, update the NAS project folder copy and recreate or restart the project.

## Run with Docker Compose

```sh
docker compose up -d --build
```

Then open `http://192.168.1.100:8081`.

## Project layout

```text
nsw-busstop-server/
├── app/
│   ├── main.py
│   ├── config.py
│   ├── db.py
│   ├── models.py
│   ├── services/
│   │   ├── auth.py
│   │   ├── stops.py
│   │   └── tfnsw.py
│   ├── static/
│   └── templates/
├── docker-compose.yml
├── Dockerfile
├── requirements.txt
└── .env.example
```

## Synology note

The compose file uses a Docker-managed named volume for `/app/data` instead of a relative bind mount. This avoids the common Synology Container Manager error:

`Bind mount failed: '/volume1/docker-apps/nsw-busstop-server/data'`

When you create or recreate the Synology project, make sure the selected project path is the real NAS folder containing:

- `docker-compose.yml`
- `.env`
- `Dockerfile`
- `requirements.txt`
- `app/`

## API endpoints

- `GET /health`
- `GET /`
- `GET /api/state`
- `GET /api/stops`
- `POST /api/stops`

## Next planned phases

1. Tighten auth and reverse-proxy support for external access
2. Add richer health and diagnostics
3. Add ESP32 client mode using HTTP polling
4. Add optional cached history and admin features
