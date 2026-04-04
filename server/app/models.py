from __future__ import annotations

from typing import Optional

from sqlmodel import Field, SQLModel


class StopConfig(SQLModel, table=True):
    id: Optional[int] = Field(default=None, primary_key=True)
    stop_id: str = Field(index=True, max_length=32)
    name: str = Field(max_length=64)
    sort_order: int = Field(default=0, index=True)


class StopInput(SQLModel):
    stop_id: str
    name: str


class DashboardConfig(SQLModel, table=True):
    id: int = Field(default=1, primary_key=True)
    departures_per_stop: int = Field(default=3, ge=1, le=8)


class DashboardConfigInput(SQLModel):
    departures_per_stop: int = Field(ge=1, le=8)


class Departure(SQLModel):
    route: str = ""
    clock: str = ""
    minutes: int = 0
    epoch: int = 0
    rt: bool = False
    delay: int = 0
    dest: str = ""


class StopState(SQLModel):
    id: str
    name: str
    valid: bool = False
    fetch_age: int = -1
    alert: str = ""
    departures: list[Departure] = []


class AppStatePayload(SQLModel):
    time: str
    date: str
    now: int
    tzOff: int
    lastRefresh: str | None = None
    lastError: str | None = None
    stops: list[StopState]
