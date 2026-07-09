"""
Проверка API-токена для ESP32 neiro.

Токен задаётся в .env как ESP32_API_TOKEN.
Если переменная пуста — проверка отключена (режим разработки).
"""

from __future__ import annotations

import os

from fastapi import Header, HTTPException


def get_configured_token() -> str:
    """Вернуть токен из окружения или пустую строку."""
    return os.getenv("ESP32_API_TOKEN", "").strip()


def verify_api_token(x_api_token: str | None = Header(default=None, alias="X-API-Token")) -> None:
    """
    Зависимость FastAPI: проверить заголовок X-API-Token.

    Используется для /api/telemetry (ESP32) и мутирующих эндпоинтов дашборда.
    """
    expected = get_configured_token()
    if not expected:
        return
    if x_api_token != expected:
        raise HTTPException(status_code=401, detail="Неверный или отсутствующий API-токен")
