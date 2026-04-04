from __future__ import annotations

from sqlmodel import Session, SQLModel, create_engine, select

from app.config import DEFAULT_DASHBOARD_DEPARTURES_PER_STOP, DEFAULT_STOPS, settings
from app.models import DashboardConfig, StopConfig


connect_args = {"check_same_thread": False} if settings.database_url.startswith("sqlite") else {}
engine = create_engine(settings.database_url, echo=False, connect_args=connect_args)


def init_db() -> None:
    SQLModel.metadata.create_all(engine)

    with Session(engine) as session:
        dashboard_config = session.get(DashboardConfig, 1)
        if dashboard_config is None:
            session.add(
                DashboardConfig(
                    id=1,
                    departures_per_stop=DEFAULT_DASHBOARD_DEPARTURES_PER_STOP,
                )
            )
            session.commit()

        stop_count = len(list(session.exec(select(StopConfig))))
        if stop_count == 0:
            for sort_order, (stop_id, name) in enumerate(DEFAULT_STOPS):
                session.add(StopConfig(stop_id=stop_id, name=name, sort_order=sort_order))
            session.commit()


def get_session() -> Session:
    return Session(engine)
