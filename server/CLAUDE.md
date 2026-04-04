# CLAUDE.md — nsw-busstop server

## Monorepo Context

This is the **server** component of the [nsw-busstop](../README.md) monorepo.
The companion ESP32 client lives at [../client/](../client/).

## What This Project Does

A Python/FastAPI web application that polls the Transport for NSW (TfNSW) API for real-time bus departures and serves a live dashboard + JSON API. Designed to run as a single Docker container on a Synology DS423+ NAS. The ESP32 client fetches `/api/state` from this server.

## Tech Stack

- **Language**: Python 3.12
- **Framework**: FastAPI + Uvicorn (ASGI)
- **ORM**: SQLModel (SQLAlchemy + Pydantic)
- **Database**: SQLite — single file at `/app/data/busstop.db`
- **Templates**: Jinja2 server-rendered, vanilla JS (no frontend build step)
- **HTTP client**: httpx (async) for TfNSW API calls
- **Auth**: form login with Starlette SessionMiddleware + itsdangerous
- **Container**: Docker Compose, single service, named volume for persistence

## Deployment

Target: Synology DS423+ via Container Manager. Uses Docker-managed named volume (`nsw-busstop-data`) — not a bind mount — to avoid Synology path errors. Port 8081 by default.

Full NAS deployment guide: [../README.md](../README.md#synology-ds423-deployment-guide)

```sh
docker compose up -d --build
```

## Key Files

| File | Purpose |
|:-----|:--------|
| `app/main.py` | FastAPI app, all routes, background poller task |
| `app/config.py` | Settings dataclass, env var loading |
| `app/models.py` | SQLModel table + Pydantic schemas |
| `app/db.py` | Engine creation, session factory, DB init |
| `app/services/tfnsw.py` | TfNSW API polling + departure parsing |
| `app/services/stops.py` | Stop CRUD operations |
| `app/services/auth.py` | Username/password check, session helpers |
| `app/templates/dashboard.html` | Main UI — departures + settings tabs |
| `docker-compose.yml` | Single service with named volume |
| `.env` | Live config (gitignored) |
| `.env.example` | Template for required env vars |

## External API

TfNSW Departure Monitor: `https://api.transport.nsw.gov.au/v1/tp/departure_mon`
- Auth: `Authorization: apikey {TFNSW_API_KEY}` header
- Polls every `POLL_INTERVAL_SECONDS` (default 60s) in a background async task
- Extracts route number, destination, estimated/planned time, delay
- 20-second timeout per request

## Environment Variables

All set in `.env` (gitignored). See `.env.example` for the full list:
`TFNSW_API_KEY`, `AUTH_ENABLED`, `APP_USERNAME`, `APP_PASSWORD`, `SESSION_SECRET`, `NAS_API_KEY`, `TIMEZONE`, `POLL_INTERVAL_SECONDS`, `PORT`, `DATABASE_URL`

`NAS_API_KEY` enables Bearer token auth for the ESP32 client. When set, the
client sends `Authorization: Bearer <key>` and the server validates it via
`hmac.compare_digest`. This is in addition to session-based dashboard auth.

## Database

Single table `stopconfig` (id, stop_id, name, sort_order). Seeded on first run with four default Ryde/Putney stops. Stops are managed via the dashboard settings tab or `POST /api/stops`.

## API Routes

| Method | Path | Auth | Purpose |
|:-------|:-----|:-----|:--------|
| GET | `/health` | No | Health check JSON |
| GET | `/` | Yes\* | Dashboard HTML |
| GET | `/login` | No | Login page |
| POST | `/login` | No | Form login |
| POST | `/logout` | No | Clear session |
| GET | `/api/state` | Yes\* | Full state JSON (stops + departures) |
| GET | `/api/stops` | Yes\* | Stop list JSON |
| POST | `/api/stops` | Yes\* | Replace all stops |

\* Only when `AUTH_ENABLED=true`

## Development Rules

- No test suite exists yet — manual testing via `/health` and dashboard
- Templates are inline JS — no bundler or minifier
- Dependencies pinned in `requirements.txt` — no `pyproject.toml`
- `.env` is the source of truth on the NAS; the local copy may drift
- Sensitive values masked on the dashboard (password `****`, API key first/last 4 chars)

## Known Quirks

- `strftime("%-d")` is Linux-only — will break on Windows (not an issue in Docker)
- Auth uses `hmac.compare_digest` for timing-safe comparison but has no rate limiting
- Background poller fetches all stops in parallel via `asyncio.gather`
- The `spiffs` partition subtype in SQLite path naming is unrelated to ESP32 SPIFFS — it's just SQLite on ext4 inside the container
