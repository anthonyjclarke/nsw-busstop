# NSW BusStop Server

Python/FastAPI server that polls the TfNSW Trip Planner API for bus departures
and serves a live dashboard + JSON feed. This is the **server** component of
the [nsw-busstop](../README.md) monorepo. The companion ESP32 client
([../client/](../client/)) consumes `/api/state` from this server.

For the project overview, architecture diagram, screenshots, full Synology
deployment guide, and cross-component JSON contract, see the
[top-level README](../README.md).

---

## Tech Stack

- **Language:** Python 3.12
- **Framework:** FastAPI + Uvicorn (ASGI)
- **ORM:** SQLModel (SQLAlchemy + Pydantic)
- **Database:** SQLite — single file at `/app/data/busstop.db`
- **Templates:** Jinja2 server-rendered, vanilla JS (no frontend build step)
- **HTTP client:** httpx (async) for TfNSW API calls
- **Auth:** form login with Starlette SessionMiddleware + itsdangerous; optional
  Bearer token for the ESP32 client
- **Container:** Docker Compose, single service, named volume for persistence

---

## Environment Setup

1. Copy `.env.example` to `app/.env`
2. Set your TfNSW API key and any auth settings you need

```env
TFNSW_API_KEY=your-api-key
AUTH_ENABLED=false
APP_USERNAME=admin
APP_PASSWORD=your-password
SESSION_SECRET=a-long-random-string
NAS_API_KEY=
TIMEZONE=Australia/Sydney
POLL_INTERVAL_SECONDS=60
PORT=8081
```

Full env var reference is in the
[top-level README](../README.md#environment-variable-reference).

For Synology deployments, `app/.env` in the NAS project folder is the source of
truth. The **Settings** tab of the dashboard shows live runtime status
(container uptime, last poll, last error, configured stop count) so you can
confirm what the running container is actually using.

---

## Run with Docker Compose

```sh
docker compose up -d --build
```

Then open `http://<nas-ip>:8081`.

The compose file uses a Docker-managed named volume for `/app/data` (not a bind
mount) to avoid the common Synology Container Manager error:

```
Bind mount failed: '/volume1/docker-apps/nsw-busstop-server/data'
```

**Full step-by-step NAS deployment guide:**
[../README.md](../README.md#synology-ds423-deployment-guide)

---

## Local Development

```sh
cd server
cp .env.example app/.env
# Edit app/.env with your TfNSW API key
pip install -r requirements.txt
uvicorn app.main:app --reload --port 8081
```

---

## Authentication

Controlled by `AUTH_ENABLED` in `app/.env`:

- **Default / local:** `AUTH_ENABLED=false` — no login required
- **Protected:** `AUTH_ENABLED=true` — form login for the dashboard, and
  Bearer token (`NAS_API_KEY`) for the ESP32 client on `/api/state`

When auth is enabled, the dashboard uses form login backed by an encrypted
session cookie. The ESP32 sends `Authorization: Bearer <NAS_API_KEY>` matched
server-side via `hmac.compare_digest`.

Do **not** expose this app to the internet unless:

- `AUTH_ENABLED=true`
- `APP_PASSWORD` is changed from any placeholder value
- `SESSION_SECRET` is long and random
- A reverse proxy with HTTPS sits in front

---

## Project Layout

```
server/
├── app/
│   ├── main.py               # FastAPI app, routes, background poller task
│   ├── config.py             # Settings dataclass, env var loading
│   ├── db.py                 # Engine, session factory, DB init + seed
│   ├── models.py             # SQLModel table + Pydantic schemas
│   ├── services/
│   │   ├── auth.py           # Session + Bearer token auth
│   │   ├── stops.py          # Stop CRUD operations
│   │   └── tfnsw.py          # TfNSW API polling + departure parsing
│   ├── static/
│   └── templates/            # dashboard.html, login.html
├── docker-compose.yml
├── Dockerfile
├── requirements.txt
└── .env.example
```

---

## API Endpoints

| Method | Path         | Auth   | Purpose                          |
|:-------|:-------------|:-------|:---------------------------------|
| GET    | `/health`    | No     | Health check JSON                |
| GET    | `/`          | Yes\*  | Dashboard HTML                   |
| GET    | `/login`     | No     | Login page                       |
| POST   | `/login`     | No     | Form login                       |
| POST   | `/logout`    | No     | Clear session                    |
| GET    | `/api/state` | Yes\*  | Full state JSON (for ESP32)      |
| GET    | `/api/stops` | Yes\*  | Stop list JSON                   |
| POST   | `/api/stops` | Yes\*  | Replace all stops, trigger fetch |

\* Only when `AUTH_ENABLED=true`. The ESP32 client authenticates against
`/api/state` with `Authorization: Bearer <NAS_API_KEY>`.

For the full JSON schema of `/api/state`, see the
[cross-component contract](../README.md#cross-component-json-contract).

---

## Database

Single table `stopconfig` (id, stop_id, name, sort_order). Seeded on first run
with four default Ryde/Putney stops. Stops are managed via the dashboard
**Settings** tab or `POST /api/stops`.

Persistence is via a Docker named volume (`nsw-busstop-data`) mounted at
`/app/data`. See the
[deployment guide](../README.md#data-persistence) for backup/reset.

---

## Licence

Personal project — not licensed for redistribution.
