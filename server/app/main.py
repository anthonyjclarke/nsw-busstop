from __future__ import annotations

import asyncio
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

from app.config import settings
from app.db import get_session, init_db
from app.models import AppStatePayload, StopInput
from app.services.auth import SESSION_KEY, authenticate, is_authenticated
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
    return dt.strftime("%a, %d %b")


def _mask_value(value: str, *, keep_start: int = 4, keep_end: int = 4) -> str:
    if not value:
        return "(empty)"
    if len(value) <= keep_start + keep_end:
        return "*" * len(value)
    return f"{value[:keep_start]}{'*' * (len(value) - keep_start - keep_end)}{value[-keep_end:]}"


def _runtime_config() -> dict[str, Any]:
    return {
        "authEnabled": settings.auth_enabled,
        "username": settings.app_username,
        "passwordMask": _mask_value(settings.app_password, keep_start=0, keep_end=0),
        "tfnswKeyMask": _mask_value(settings.tfnsw_api_key),
        "sessionSecretMask": _mask_value(settings.session_secret),
        "timezone": settings.timezone,
        "pollIntervalSeconds": settings.poll_interval_seconds,
        "port": settings.port,
        "configSource": "Active container environment from the NAS project folder selected in Synology Container Manager",
    }


def _with_live_minutes(payload: AppStatePayload) -> dict[str, Any]:
    data = payload.model_dump()
    local_now = _local_now()
    data["time"] = local_now.strftime("%H:%M:%S")
    data["date"] = _date_label(local_now)
    data["now"] = int(local_now.astimezone(UTC).timestamp())
    for stop in data["stops"]:
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
    payload = _with_live_minutes(app.state.state_payload)
    return templates.TemplateResponse(
        "dashboard.html",
        {
            "request": request,
            "payload": payload,
            "poll_interval_seconds": settings.poll_interval_seconds,
            "timezone": settings.timezone,
            "runtime_config": _runtime_config(),
        },
    )


@app.get("/api/state")
async def api_state(request: Request) -> JSONResponse:
    if settings.auth_enabled and not is_authenticated(request):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Authentication required")
    return JSONResponse(_with_live_minutes(app.state.state_payload))


@app.get("/api/stops")
async def api_stops(request: Request, session: Session = Depends(session_dep)) -> list[dict[str, Any]]:
    if settings.auth_enabled and not is_authenticated(request):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Authentication required")
    return [
        {"id": stop.stop_id, "name": stop.name}
        for stop in list_stops(session)
    ]


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
