from __future__ import annotations

import os
from dataclasses import dataclass


DEFAULT_STOPS = [
    ("2112130", "To Gladesville"),
    ("2112131", "To Meadowbank Stn"),
    ("211267", "End of Small St"),
    ("211271", "To Macquarie Park"),
]

CLIENT_DEPARTURES_PER_STOP = 3
DEFAULT_DASHBOARD_DEPARTURES_PER_STOP = 3
MAX_TFNSW_DEPARTURES_PER_STOP = 8


@dataclass(frozen=True)
class Settings:
    tfnsw_api_key: str = os.getenv("TFNSW_API_KEY", "")
    app_username: str = os.getenv("APP_USERNAME", "admin")
    app_password: str = os.getenv("APP_PASSWORD", "change-me")
    session_secret: str = os.getenv("SESSION_SECRET", "change-this-session-secret")
    timezone: str = os.getenv("TIMEZONE", "Australia/Sydney")
    poll_interval_seconds: int = int(os.getenv("POLL_INTERVAL_SECONDS", "60"))
    port: int = int(os.getenv("PORT", "8081"))
    auth_enabled: bool = os.getenv("AUTH_ENABLED", "false").strip().lower() in {"1", "true", "yes", "on"}
    nas_api_key: str = os.getenv("NAS_API_KEY", "")
    database_url: str = os.getenv("DATABASE_URL", "sqlite:////app/data/busstop.db")


settings = Settings()
