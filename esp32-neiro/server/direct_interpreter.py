"""
Быстрый разбор прямых приказов без LLM.

Покрывает сигналы, расписание, вкл/выкл — чтобы Ollama не отвечала «не умею».
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any

from scheduler import parse_time_expression, TZ
from esp_commands import tool_to_esp_commands

ALLOWED_PINS = {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 23, 25, 26, 27, 32, 33}


@dataclass
class InterpretResult:
    commands: list[dict[str, Any]] = field(default_factory=list)
    schedules: list[dict[str, Any]] = field(default_factory=list)
    log: str | None = None


def _parse_pin(text: str, default: int = 2) -> int:
    m = re.search(r"gpio\s*(\d{1,2})", text, re.I)
    if m:
        p = int(m.group(1))
        if p in ALLOWED_PINS:
            return p
    if "светодиод" in text or " led" in text or text.strip().startswith("led"):
        return 2
    return default


def _parse_hz(text: str) -> float:
    m = re.search(r"(\d+(?:[.,]\d+)?)\s*(?:гц|hz|герц)\b", text, re.I)
    if m:
        return float(m.group(1).replace(",", "."))
    return 1.0


def _parse_duration_ms(text: str) -> int:
    m = re.search(r"(\d+)\s*(?:мин(?:ут)?|minute|min)\b", text, re.I)
    if m:
        return int(m.group(1)) * 60_000
    m = re.search(r"(\d+)\s*(?:сек(?:унд)?|sec|s)\b", text, re.I)
    if m:
        return int(m.group(1)) * 1_000
    m = re.search(r"(\d+)\s*(?:ч(?:ас)?|hour|h)\b", text, re.I)
    if m:
        return int(m.group(1)) * 3_600_000
    return 60_000


def _parse_wave(text: str) -> str:
    if re.search(r"синус|sin(e)?\b", text, re.I):
        return "sine"
    if re.search(r"пил|saw", text, re.I):
        return "saw"
    if re.search(r"треуголь|triangle|tri\b", text, re.I):
        return "triangle"
    return "square"


def try_interpret_direct_command(message: str) -> InterpretResult | None:
    text = message.lower().strip()
    if not text:
        return None

    result = InterpretResult()

    # --- Расписание: «в 13:30 включи», «в 14:50 выключи светодиод» ---
    sched_times = list(re.finditer(r"(?:в\s+)?(\d{1,2})[:\.](\d{2})", text))
    if sched_times and re.search(r"включ|выключ|turn on|turn off", text, re.I):
        for i, m in enumerate(sched_times):
            start = m.start()
            end = sched_times[i + 1].start() if i + 1 < len(sched_times) else len(text)
            fragment = text[start:end]
            run_at = parse_time_expression(m.group(0))
            if not run_at:
                continue
            pin = _parse_pin(text)
            if re.search(r"выключ|turn off|off\b|погас", fragment, re.I):
                cmd = {"cmd": "digital_write", "pin": pin, "value": 1 if pin == 2 else 0}
                label = f"Выключить GPIO {pin} в {run_at.strftime('%H:%M')}"
            else:
                cmd = {"cmd": "digital_write", "pin": pin, "value": 0 if pin == 2 else 1}
                label = f"Включить GPIO {pin} в {run_at.strftime('%H:%M')}"
            result.schedules.append({"run_at": run_at, "commands": [cmd], "label": label})
        if result.schedules:
            result.log = f"Расписание: {len(result.schedules)} событий"
            return result

    # --- Стоп ---
    if re.search(r"\b(стоп|останов|stop)\b", text, re.I):
        pin = _parse_pin(text)
        result.commands = [{"cmd": "stop", "pin": pin}]
        result.log = f"Стоп сигнал GPIO {pin}"
        return result

    # --- Сигнал / мигание ---
    signal_markers = (
        "мига", "морг", "blink", "сигнал", "синус", "sin", "пил", "saw",
        "треуголь", "triangle", "шим", "pwm", "частот", "гц", "hz",
        "светодиод", " led", "led ",
    )
    if any(x in text for x in signal_markers):
        pin = _parse_pin(text)
        wave = _parse_wave(text)
        hz = _parse_hz(text)
        duration_ms = _parse_duration_ms(text)
        duty = 100
        dm = re.search(r"(\d+)\s*%", text)
        if dm:
            duty = int(dm.group(1))
        result.commands = tool_to_esp_commands(
            "generate_pin_signal",
            {
                "pin": pin,
                "wave": wave,
                "hz": hz,
                "duration_sec": duration_ms // 1000,
                "duty": duty,
                "active_low": pin == 2,
            },
        )
        result.log = f"Сигнал {wave} GPIO {pin} {hz} Гц"
        return result

    # --- Простое вкл/выкл ---
    if re.search(r"включ|turn on|зажги", text, re.I):
        pin = _parse_pin(text)
        val = 0 if pin == 2 else 1
        result.commands = [{"cmd": "digital_write", "pin": pin, "value": val}]
        result.log = f"Включить GPIO {pin}"
        return result

    if re.search(r"выключ|turn off|погас", text, re.I):
        pin = _parse_pin(text)
        val = 1 if pin == 2 else 0
        result.commands = [{"cmd": "digital_write", "pin": pin, "value": val}]
        result.log = f"Выключить GPIO {pin}"
        return result

    return None
