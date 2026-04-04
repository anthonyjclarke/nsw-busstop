from __future__ import annotations

import hmac

from fastapi import Request

from app.config import settings


SESSION_KEY = "authenticated"


def _check_bearer(request: Request) -> bool:
    if not settings.nas_api_key:
        return False
    auth = request.headers.get("authorization", "")
    if not auth.lower().startswith("bearer "):
        return False
    token = auth[7:]
    return hmac.compare_digest(token, settings.nas_api_key)


def is_authenticated(request: Request) -> bool:
    if not settings.auth_enabled:
        return True
    if bool(request.session.get(SESSION_KEY)):
        return True
    return _check_bearer(request)


def authenticate(username: str, password: str) -> bool:
    return hmac.compare_digest(username, settings.app_username) and hmac.compare_digest(password, settings.app_password)
