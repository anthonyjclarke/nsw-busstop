# NSW BusStop

Real-time Sydney bus departure display system. A Python server polls TfNSW APIs
and an ESP32 CYD renders the results on a TFT screen.

```
[TfNSW API] --> [Server on NAS :8081] --> [ESP32 Client (CYD TFT)]
```

---

## Components

### Server (`server/`)

Python FastAPI application. Polls TfNSW departure APIs, stores stop configuration
in SQLite, and serves a web dashboard plus JSON API. Runs as a Docker container on
a Synology NAS.

- **Tech:** Python 3.12, FastAPI, SQLModel, SQLite, Jinja2
- **Port:** 8081 (default)
- **Details:** [server/README.md](server/README.md)

### Client (`client/`)

ESP32 PlatformIO/Arduino project for the CYD 2.8" TFT display. Fetches
pre-processed JSON from the server and renders a 2x2 grid of bus stop panels
with live countdowns.

- **Tech:** ESP32 Arduino, TFT_eSPI, ArduinoJson, ezTime
- **Hardware:** ESP32-2432S028R (CYD 2.8"), ILI9341, 240x320
- **Details:** [client/README.md](client/README.md)

---

## Quick Start

### 1. Server (on NAS)

```bash
# Copy server/ to your NAS (see deployment guide below)
# Configure server/app/.env with your TfNSW API key
# Build and start via Container Manager
```

### 2. Client (on Mac)

```bash
# Open nsw-busstop.code-workspace in VSCode
# Create client/include/secrets.h from secrets.h.example
pio run -d client/ -t upload       # Flash firmware
```

### 3. First boot

1. Connect to the `CYD-BusStop` WiFi AP from your phone
2. Enter your WiFi credentials in the captive portal
3. Device connects, syncs time, and fetches bus data from the server

---

## Synology DS423+ Deployment Guide

### Prerequisites

- Synology DS423+ with **Container Manager** package installed
- TfNSW API key from [opendata.transport.nsw.gov.au](https://opendata.transport.nsw.gov.au)
- NAS accessible on your local network (e.g. `192.168.1.100`)

### Step 1: Copy server files to NAS

1. On your Mac, open **Finder** > **Go** > **Connect to Server**
2. Enter `smb://<your-nas-ip>` (e.g. `smb://192.168.1.100`)
3. Navigate to a shared folder (e.g. `docker/`)
4. Create a new folder: `nsw-busstop-server`
5. Copy the **contents** of the `server/` directory into it:
   ```
   nsw-busstop-server/
   ├── app/
   ├── Dockerfile
   ├── docker-compose.yml
   ├── requirements.txt
   └── .env.example
   ```

**Important:** Copy only the contents of `server/`, not the entire monorepo.

### Step 2: Configure environment

1. On the NAS, copy `.env.example` to `app/.env`
2. Edit `app/.env` with your settings:

```env
TFNSW_API_KEY=your-tfnsw-api-key-here
AUTH_ENABLED=false
APP_USERNAME=admin
APP_PASSWORD=your-secure-password
SESSION_SECRET=a-long-random-string
NAS_API_KEY=
TIMEZONE=Australia/Sydney
POLL_INTERVAL_SECONDS=60
PORT=8081
```

The `app/.env` file lives **only on the NAS** — it is never committed to git.

### Step 3: Build and start (Container Manager)

1. Open **Container Manager** on your Synology DSM
2. Go to **Project** > **Create**
3. Set project name: `nsw-busstop-server`
4. Set path: select the `nsw-busstop-server` folder on the NAS
5. Container Manager will auto-detect the `docker-compose.yml`
6. Click **Build & Start**
7. Wait for the build to complete (first build takes 1-2 minutes)
8. Verify: open `http://<nas-ip>:8081` in your browser

You should see the bus departure dashboard. Configure your stops directly on
the dashboard page.

### Updating after code changes

When you pull new code from git and need to update the server on the NAS:

1. On your Mac, pull the latest changes: `git pull`
2. Copy the updated `server/` files to the NAS folder (same Finder method)

   **Do NOT overwrite `app/.env`** — it contains your secrets and is the source
   of truth on the NAS.

3. In Container Manager: **Project** > `nsw-busstop-server` > **Action** > **Build**
4. Container Manager rebuilds the image and restarts the container
5. Your stop configuration and database are preserved (Docker named volume)

### Using rsync (advanced)

If you prefer the command line, use rsync to sync files while excluding `app/.env`:

```bash
rsync -av --exclude 'app/.env' --exclude '__pycache__' --exclude 'data/' \
  server/ admin@<nas-ip>:/volume1/docker/<path>/nsw-busstop-server/
```

Then SSH in and rebuild:

```bash
ssh admin@<nas-ip>
cd /volume1/docker/<path>/nsw-busstop-server
sudo docker compose up -d --build
```

### Data persistence

- The SQLite database is stored in a Docker **named volume** (`nsw-busstop-data`)
- This volume persists across container rebuilds and restarts
- Stop configuration survives updates — you won't lose your stops

**Backup the database:**
```bash
docker cp nsw-busstop-server:/app/data/busstop.db ./busstop-backup.db
```

**Reset all data** (removes stop config — will re-seed with defaults):
```bash
docker volume rm nsw-busstop-data
# Then rebuild the container
```

### Environment variable reference

| Variable               | Required | Default            | Purpose                      |
|:-----------------------|:---------|:-------------------|:-----------------------------|
| `TFNSW_API_KEY`        | Yes      | —                  | TfNSW API key                |
| `AUTH_ENABLED`         | No       | `false`            | Enable dashboard login       |
| `APP_USERNAME`         | No       | `admin`            | Dashboard username           |
| `APP_PASSWORD`         | Yes*     | `change-me`        | Dashboard password           |
| `SESSION_SECRET`       | Yes*     | —                  | Cookie signing secret        |
| `TIMEZONE`             | No       | `Australia/Sydney` | Display timezone             |
| `POLL_INTERVAL_SECONDS`| No      | `60`               | TfNSW fetch interval (secs) |
| `PORT`                 | No       | `8081`             | Server port                  |

\* Required when `AUTH_ENABLED=true`.

### Troubleshooting

| Symptom                        | Fix                                                    |
|:-------------------------------|:-------------------------------------------------------|
| Container won't start          | Check `app/.env` exists and `TFNSW_API_KEY` is set     |
| Dashboard shows no departures  | Verify API key is valid at TfNSW Open Data portal      |
| Port conflict                  | Port `8081` is published directly in `docker-compose.yml`; if you need a different host port, edit the compose file and recreate the project |
| ESP32 shows HTTP 401           | Set `AUTH_ENABLED=false` in `app/.env`, or set matching `NAS_API_KEY` / `SECRET_NAS_API_KEY` values |
| "Bind mount failed"            | Ensure using named volume (default docker-compose.yml) |
| Stops reset after rebuild      | Normal if volume was deleted; reconfigure in dashboard  |

---

## Repository Structure

```
nsw-busstop/
├── client/                  # ESP32 PlatformIO project
│   ├── platformio.ini
│   ├── partitions_custom.csv
│   ├── include/             # config.h, debug.h, secrets.h
│   ├── src/                 # main.cpp, display, bus_api, etc.
│   └── README.md
│
├── server/                  # Python FastAPI server
│   ├── Dockerfile
│   ├── docker-compose.yml
│   ├── requirements.txt
│   ├── .env.example
│   └── app/                 # FastAPI application
│
├── README.md                # This file
├── CLAUDE.md                # AI assistant context
└── nsw-busstop.code-workspace  # VSCode multi-root workspace
```

---

## Development Setup

1. Clone the repo: `git clone https://github.com/anthonyjclarke/nsw-busstop.git`
2. Open `nsw-busstop.code-workspace` in VSCode
3. PlatformIO auto-detects the client project via workspace settings

### Server (local dev)

```bash
cd server
cp .env.example app/.env
# Edit app/.env with your TfNSW API key
pip install -r requirements.txt
uvicorn app.main:app --reload --port 8081
```

### Client

```bash
# Create secrets file
cp client/include/secrets.h.example client/include/secrets.h
# Edit secrets.h with your WiFi credentials

# Build and flash
pio run -d client/ -t upload
```

---

## Licence

Personal project — not licensed for redistribution.
