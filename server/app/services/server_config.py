from __future__ import annotations

from sqlmodel import Session

from app.config import DEFAULT_DASHBOARD_DEPARTURES_PER_STOP
from app.models import DashboardConfig


def get_dashboard_config(session: Session) -> DashboardConfig:
    config = session.get(DashboardConfig, 1)
    if config is None:
        config = DashboardConfig(
            id=1,
            departures_per_stop=DEFAULT_DASHBOARD_DEPARTURES_PER_STOP,
        )
        session.add(config)
        session.commit()
        session.refresh(config)
    return config


def set_dashboard_departures_per_stop(session: Session, departures_per_stop: int) -> DashboardConfig:
    config = get_dashboard_config(session)
    config.departures_per_stop = departures_per_stop
    session.add(config)
    session.commit()
    session.refresh(config)
    return config
