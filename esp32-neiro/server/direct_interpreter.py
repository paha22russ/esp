"""
Быстрый разбор простых прямых приказов без вызова LLM.

Нужен для задач вроде «помигай светодиодом 1 Гц 5 минут» — Ollama думает
десятки секунд, а digital_write не умеет мигать по времени.
"""

from __future__ import annotations

import re
from typing import Any


def try_interpret_direct_command(message: str) -> list[dict[str, Any]] | None:
    """
    Распознать простой приказ и вернуть ESP-команды.

    Возвращает None, если приказ не распознан — тогда вызывается LLM.
    """
    text = message.lower().strip()
    if not text:
        return None

    blink_markers = ("мига", "морг", "blink", "светодиод", " led", "led ")
    if not any(marker in text for marker in blink_markers):
        return None

    hz = 1.0
    hz_match = re.search(r"(\d+(?:[.,]\d+)?)\s*(?:гц|hz|герц)\b", text)
    if hz_match:
        hz = float(hz_match.group(1).replace(",", "."))
    elif re.search(r"\b1\s*гц\b", text):
        hz = 1.0

    duration_ms = 60_000
    min_match = re.search(r"(\d+)\s*(?:мин(?:ут)?|minute|min)\b", text)
    sec_match = re.search(r"(\d+)\s*(?:сек(?:унд)?|sec|s)\b", text)
    if min_match:
        duration_ms = int(min_match.group(1)) * 60_000
    elif sec_match:
        duration_ms = int(sec_match.group(1)) * 1_000

    pin = 2
    pin_match = re.search(r"(?:gpio\s*)?(\d{1,2})\b", text)
    if pin_match:
        candidate = int(pin_match.group(1))
        if candidate in {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 23, 25, 26, 27, 32, 33}:
            pin = candidate

    return [
        {
            "cmd": "blink",
            "pin": pin,
            "hz": hz,
            "duration_ms": duration_ms,
            "active_low": pin == 2,
        }
    ]
