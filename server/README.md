# NSW BusStop Server

Python FastAPI server that polls TfNSW bus departure APIs and serves a live
dashboard plus JSON endpoints. This is the **server** component of the
[nsw-busstop](../README.md) monorepo. The companion ESP32 client
([../client/](../client/)) fetches `/api/state` from this server.

## Features

- TfNSW API polling every 60s with real-time departure data
- Web dashboard with departure display, settings, and stop editor
- SQLite persistence for stop configuration (Docker named volume)
- JSON API for ESP32 client (`/api/state`, `/api/stops`)
- Optional form-based authentication with session cookies
- Single Docker container, designed for Synology DS423+ NAS

## Ports

The container listens on port `8081` by default.

Open:

- `http://<diskstation-ip>:8081`
- Example: `http://192.168.1.100:8081`

## Authentication

Authentication is controlled by `AUTH_ENABLED` in `app/.env`.

Recommended values:

- default/local bring-up: `AUTH_ENABLED=false`
- only enable it if you actually want dashboard login protection: `AUTH_ENABLED=true`

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

1. Copy `.env.example` to `app/.env`
2. Set your TfNSW API key and login credentials

Required variables:

```env
TFNSW_API_KEY=your-api-key
AUTH_ENABLED=false
APP_USERNAME=admin
APP_PASSWORD=your-password
SESSION_SECRET=a-long-random-string
TIMEZONE=Australia/Sydney
POLL_INTERVAL_SECONDS=60
PORT=8081
```

## Source Of Truth

For Synology deployments, the live configuration is `app/.env` in the NAS project folder selected in Container Manager, not necessarily the file currently open in VS Code on another machine.

This project now shows masked runtime values on the dashboard so you can verify what the running container is actually using.

If the dashboard values do not match your editor, update the NAS project folder `app/.env` copy and recreate the project.

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

## Synology Deployment

The compose file uses a Docker-managed named volume for `/app/data` instead of a
relative bind mount. This avoids the common Synology Container Manager error:

`Bind mount failed: '/volume1/docker-apps/nsw-busstop-server/data'`

**Full step-by-step NAS deployment guide:** [../README.md](../README.md#synology-ds423-deployment-guide)

When copying to the NAS, include these files in the project folder:

- `docker-compose.yml`
- `app/.env` (created from `.env.example`)
- `Dockerfile`
- `requirements.txt`
- `app/`

Deployment rule:

- Keep one runtime env file only: `app/.env`
- Do not rely on a second root `.env` for Synology Container Manager
- The compose file now loads `./app/.env` explicitly and publishes container port `8081` directly

## API endpoints

| Method | Path         | Auth    | Purpose                          |
|:-------|:-------------|:--------|:---------------------------------|
| GET    | `/health`    | No      | Health check JSON                |
| GET    | `/`          | Yes\*   | Dashboard HTML                   |
| GET    | `/api/state` | Yes\*   | Full state JSON (for ESP32)      |
| GET    | `/api/stops` | Yes\*   | Stop list JSON                   |
| POST   | `/api/stops` | Yes\*   | Replace all stops, trigger fetch |

\* Only when `AUTH_ENABLED=true`
