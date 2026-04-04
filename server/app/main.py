from __future__ import annotations

import asyncio
import platform
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager
from datetime import UTC, datetime
from typing import Any
from zoneinfo import ZoneInfo

from fastapi import Depends, FastAPI, Form, HTTPException, Request, status
from fastapi.responses import HTMLResponse, JSONResponse, RedirectResponse
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from sqlmodel import Session
from starlette.middleware.sessions import SessionMiddleware

from app.config import CLIENT_DEPARTURES_PER_STOP, settings
from app.db import get_session, init_db
from app.models import AppStatePayload, DashboardConfigInput, StopInput
from app.services.auth import SESSION_KEY, authenticate, is_authenticated
from app.services.server_config import get_dashboard_config, set_dashboard_departures_per_stop
from app.services.stops import list_stops, replace_stops
from app.services.tfnsw import fetch_state


@asynccontextmanager
async def lifespan(app: FastAPI) -> AsyncIterator[None]:
    init_db()
    app.state.state_payload = AppStatePayload(
        time="--:--:--",
        date="--",
        now=0,
        tzOff=0,
        lastRefresh=None,
        lastError="Waiting for initial refresh",
        stops=[],
    )
    app.state.last_error = None
    app.state.last_refresh = None
    await refresh_state()
    poller_task = asyncio.create_task(poller())
    yield
    poller_task.cancel()


app = FastAPI(title="NSW BusStop Server", lifespan=lifespan)
app.add_middleware(SessionMiddleware, secret_key=settings.session_secret)
app.mount("/static", StaticFiles(directory="app/static"), name="static")
templates = Jinja2Templates(directory="app/templates")


def session_dep() -> Session:
    with get_session() as session:
        yield session


def _local_now() -> datetime:
    return datetime.now(ZoneInfo(settings.timezone))


def _date_label(dt: datetime) -> str:
    return dt.strftime("%a, %-d %b")


_started_at: datetime = datetime.now(UTC)


def _format_uptime(delta) -> str:
    total = int(delta.total_seconds())
    days, rem = divmod(total, 86400)
    hours, rem = divmod(rem, 3600)
    mins, _ = divmod(rem, 60)
    if days:
        return f"{days}d {hours}h {mins}m"
    if hours:
        return f"{hours}h {mins}m"
    return f"{mins}m"


def _server_status() -> dict[str, Any]:
    with get_session() as session:
        stop_count = len(list_stops(session))
        dashboard_config = get_dashboard_config(session)
    return {
        "uptime": _format_uptime(datetime.now(UTC) - _started_at),
        "stopCount": stop_count,
        "dashboardDeparturesPerStop": dashboard_config.departures_per_stop,
        "lastRefresh": app.state.last_refresh.isoformat() if app.state.last_refresh else None,
        "lastError": app.state.last_error,
        "timezone": settings.timezone,
        "pollIntervalSeconds": settings.poll_interval_seconds,
        "pythonVersion": platform.python_version(),
    }


def _with_live_minutes(payload: AppStatePayload, departure_limit: int | None = None) -> dict[str, Any]:
    data = payload.model_dump()
    local_now = _local_now()
    data["time"] = local_now.strftime("%H:%M:%S")
    data["date"] = _date_label(local_now)
    data["now"] = int(local_now.astimezone(UTC).timestamp())
    for stop in data["stops"]:
        if departure_limit is not None:
            stop["departures"] = stop["departures"][:departure_limit]
        for dep in stop["departures"]:
            dep["minutes"] = max(0, int((dep["epoch"] - data["now"]) // 60))
    return data


async def refresh_state() -> None:
    with get_session() as session:
        result = await fetch_state(list_stops(session))
    app.state.state_payload = result.payload
    app.state.last_error = result.error
    app.state.last_refresh = result.refreshed_at


async def poller() -> None:
    while True:
        try:
            await refresh_state()
        except Exception as exc:
            app.state.last_error = str(exc)
        await asyncio.sleep(settings.poll_interval_seconds)



@app.get("/health")
async def health() -> dict[str, Any]:
    return {
        "ok": True,
        "lastRefresh": app.state.last_refresh.isoformat() if app.state.last_refresh else None,
        "lastError": app.state.last_error,
        "authEnabled": settings.auth_enabled,
    }


@app.get("/login", response_class=HTMLResponse)
async def login_page(request: Request) -> HTMLResponse:
    if not settings.auth_enabled:
        return RedirectResponse("/", status_code=status.HTTP_303_SEE_OTHER)
    if is_authenticated(request):
        return RedirectResponse("/", status_code=status.HTTP_303_SEE_OTHER)
    return templates.TemplateResponse("login.html", {"request": request, "error": None})


@app.post("/login", response_class=HTMLResponse)
async def login(request: Request, username: str = Form(...), password: str = Form(...)) -> HTMLResponse:
    if not settings.auth_enabled:
        return RedirectResponse("/", status_code=status.HTTP_303_SEE_OTHER)
    if not authenticate(username, password):
        return templates.TemplateResponse("login.html", {"request": request, "error": "Invalid credentials"}, status_code=401)
    request.session[SESSION_KEY] = True
    return RedirectResponse("/", status_code=status.HTTP_303_SEE_OTHER)


@app.post("/logout")
async def logout(request: Request) -> RedirectResponse:
    request.session.clear()
    return RedirectResponse("/login" if settings.auth_enabled else "/", status_code=status.HTTP_303_SEE_OTHER)


@app.get("/", response_class=HTMLResponse)
async def dashboard(request: Request) -> HTMLResponse:
    if settings.auth_enabled and not is_authenticated(request):
        return RedirectResponse("/login", status_code=status.HTTP_303_SEE_OTHER)
    with get_session() as session:
        dashboard_config = get_dashboard_config(session)
    payload = _with_live_minutes(
        app.state.state_payload,
        dashboard_config.departures_per_stop,
    )
    return templates.TemplateResponse(
        "dashboard.html",
        {
            "request": request,
            "payload": payload,
            "dashboard_departures_per_stop": dashboard_config.departures_per_stop,
            "poll_interval_seconds": settings.poll_interval_seconds,
            "timezone": settings.timezone,
            "auth_enabled": settings.auth_enabled,
            "server_status": _server_status(),
        },
    )


@app.get("/api/state")
async def api_state(request: Request) -> JSONResponse:
    if settings.auth_enabled and not is_authenticated(request):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Authentication required")
    # Keep the ESP32 contract fixed at 3 departures per stop.
    return JSONResponse(_with_live_minutes(app.state.state_payload, CLIENT_DEPARTURES_PER_STOP))


@app.get("/api/dashboard-state")
async def api_dashboard_state(request: Request, session: Session = Depends(session_dep)) -> JSONResponse:
    if settings.auth_enabled and not is_authenticated(request):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Authentication required")
    dashboard_config = get_dashboard_config(session)
    payload = _with_live_minutes(app.state.state_payload, dashboard_config.departures_per_stop)
    payload["dashboardDeparturesPerStop"] = dashboard_config.departures_per_stop
    return JSONResponse(payload)


@app.get("/api/stops")
async def api_stops(request: Request, session: Session = Depends(session_dep)) -> list[dict[str, Any]]:
    if settings.auth_enabled and not is_authenticated(request):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Authentication required")
    return [
        {"id": stop.stop_id, "name": stop.name}
        for stop in list_stops(session)
    ]


@app.get("/api/dashboard-config")
async def api_dashboard_config(request: Request, session: Session = Depends(session_dep)) -> dict[str, int]:
    if settings.auth_enabled and not is_authenticated(request):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Authentication required")
    config = get_dashboard_config(session)
    return {"departuresPerStop": config.departures_per_stop}


@app.post("/api/dashboard-config")
async def api_set_dashboard_config(
    request: Request,
    dashboard_config: DashboardConfigInput,
    session: Session = Depends(session_dep),
) -> dict[str, int | str]:
    if settings.auth_enabled and not is_authenticated(request):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Authentication required")
    config = set_dashboard_departures_per_stop(session, dashboard_config.departures_per_stop)
    return {
        "result": "ok",
        "departuresPerStop": config.departures_per_stop,
    }


@app.post("/api/stops")
async def api_replace_stops(request: Request, stops: list[StopInput], session: Session = Depends(session_dep)) -> dict[str, str]:
    if settings.auth_enabled and not is_authenticated(request):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Authentication required")
    cleaned = [stop for stop in stops if stop.stop_id.strip() and stop.name.strip()]
    if not cleaned:
        raise HTTPException(status_code=400, detail="At least one stop is required")
    replace_stops(session, cleaned)
    await refresh_state()
    return {"result": "ok"}
