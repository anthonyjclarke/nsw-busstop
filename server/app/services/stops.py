from __future__ import annotations

from sqlmodel import Session, delete, select

from app.models import StopConfig, StopInput


def list_stops(session: Session) -> list[StopConfig]:
    return list(session.exec(select(StopConfig).order_by(StopConfig.sort_order)))


def replace_stops(session: Session, stops: list[StopInput]) -> list[StopConfig]:
    session.exec(delete(StopConfig))
    session.commit()

    for sort_order, stop in enumerate(stops):
        session.add(StopConfig(stop_id=stop.stop_id.strip(), name=stop.name.strip(), sort_order=sort_order))
    session.commit()

    return list_stops(session)
