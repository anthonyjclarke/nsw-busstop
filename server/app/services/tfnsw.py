from __future__ import annotations

import asyncio
from dataclasses import dataclass
from datetime import UTC, datetime
from zoneinfo import ZoneInfo

import httpx

from app.config import MAX_TFNSW_DEPARTURES_PER_STOP, settings
from app.models import AppStatePayload, Departure, StopConfig, StopState


TFNSW_URL = (
    "https://api.transport.nsw.gov.au/v1/tp/departure_mon"
    "?outputFormat=rapidJSON"
    "&coordOutputFormat=EPSG:4326"
    "&mode=direct"
    "&type_dm=stop"
    "&departureMonitorMacro=true"
    "&TfNSWDM=true"
    "&version=10.2.1.42"
    "&depArr=dep"
)


@dataclass
class FetchResult:
    payload: AppStatePayload
    refreshed_at: datetime | None
    error: str | None


def _now_local() -> datetime:
    return datetime.now(ZoneInfo(settings.timezone))


def _parse_iso_to_local(value: str) -> datetime:
    return datetime.fromisoformat(value).astimezone(ZoneInfo(settings.timezone))


async def _fetch_stop(client: httpx.AsyncClient, stop: StopConfig, now_local: datetime) -> StopState:
    """Fetch departures for a single stop from TfNSW."""
    params_url = f"{TFNSW_URL}&name_dm={stop.stop_id}&limit={MAX_TFNSW_DEPARTURES_PER_STOP}"
    response = await client.get(params_url)
    response.raise_for_status()
    payload = response.json()
    stop_events = payload.get("stopEvents", [])

    alert_text = ""
    departures: list[Departure] = []

    for event in stop_events:
        infos = event.get("infos") or []
        if infos and not alert_text:
            alert_text = infos[0].get("subtitle", "") or ""

        est = event.get("departureTimeEstimated")
        planned = event.get("departureTimePlanned")
        selected = est or planned
        if not selected:
            continue

        dep_local = _parse_iso_to_local(selected)
        mins = int((dep_local - now_local).total_seconds() // 60)
        if mins < 0:
            continue

        planned_local = _parse_iso_to_local(planned) if planned else None
        delay = int((dep_local - planned_local).total_seconds()) if planned_local and est else 0

        departures.append(
            Departure(
                route=event.get("transportation", {}).get("number", ""),
                clock=dep_local.strftime("%H:%M"),
                minutes=mins,
                epoch=int(dep_local.astimezone(UTC).timestamp()),
                rt=bool(event.get("isRealtimeControlled", False)),
                delay=delay,
                dest=event.get("transportation", {}).get("destination", {}).get("name", ""),
            )
        )

    departures.sort(key=lambda dep: dep.epoch)

    return StopState(
        id=stop.stop_id,
        name=stop.name,
        valid=bool(departures),
        fetch_age=0,
        alert=alert_text,
        departures=departures,
    )


def _empty_payload(
    stops: list[StopConfig],
    now_local: datetime,
    now_utc: datetime,
    tz_off: int,
    error: str,
) -> FetchResult:
    return FetchResult(
        payload=AppStatePayload(
            time=now_local.strftime("%H:%M:%S"),
            date=now_local.strftime("%a, %-d %b"),
            now=int(now_utc.timestamp()),
            tzOff=tz_off,
            lastRefresh=None,
            lastError=error,
            stops=[
                StopState(id=stop.stop_id, name=stop.name, departures=[])
                for stop in stops
            ],
        ),
        refreshed_at=None,
        error=error,
    )


async def fetch_state(stops: list[StopConfig]) -> FetchResult:
    now_local = _now_local()
    now_utc = now_local.astimezone(UTC)
    tz_off = int(now_local.utcoffset().total_seconds()) if now_local.utcoffset() else 0

    if not settings.tfnsw_api_key:
        return _empty_payload(stops, now_local, now_utc, tz_off, "TFNSW_API_KEY is not configured")

    headers = {"Authorization": f"apikey {settings.tfnsw_api_key}"}

    try:
        async with httpx.AsyncClient(timeout=20.0, headers=headers) as client:
            stop_states = await asyncio.gather(
                *[_fetch_stop(client, stop, now_local) for stop in stops]
            )
    except httpx.HTTPStatusError as exc:
        status_code = exc.response.status_code if exc.response is not None else None
        if status_code == 401:
            return _empty_payload(stops, now_local, now_utc, tz_off, "TfNSW rejected the API key with HTTP 401")
        return _empty_payload(stops, now_local, now_utc, tz_off, f"TfNSW request failed with HTTP {status_code}")
    except httpx.HTTPError as exc:
        return _empty_payload(stops, now_local, now_utc, tz_off, f"TfNSW request error: {exc}")

    refreshed_at = _now_local()
    return FetchResult(
        payload=AppStatePayload(
            time=now_local.strftime("%H:%M:%S"),
            date=now_local.strftime("%a, %-d %b"),
            now=int(now_utc.timestamp()),
            tzOff=tz_off,
            lastRefresh=refreshed_at.isoformat(),
            lastError=None,
            stops=list(stop_states),
        ),
        refreshed_at=refreshed_at,
        error=None,
    )
