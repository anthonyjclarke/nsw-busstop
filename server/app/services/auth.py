from __future__ import annotations

from fastapi import Request

from app.config import settings


SESSION_KEY = "authenticated"


def is_authenticated(request: Request) -> bool:
    if not settings.auth_enabled:
        return True
    return bool(request.session.get(SESSION_KEY))


def authenticate(username: str, password: str) -> bool:
    return username == settings.app_username and password == settings.app_password
