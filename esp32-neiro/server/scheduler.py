"""Планировщик команд ESP32 по абсолютному времени (серверные часы)."""

from __future__ import annotations

import asyncio
import re
import time
import uuid
from datetime import datetime, timedelta
from typing import Any
from zoneinfo import ZoneInfo

from state import app_state

# Часовой пояс homeserv (Москва)
TZ = ZoneInfo("Europe/Moscow")

SCHEDULE_STATUS = {
    "pending": "Ожидает",
    "done": "Выполнено",
    "cancelled": "Отменено",
    "error": "Ошибка",
}


def parse_time_expression(text: str, now: datetime | None = None) -> datetime | None:
    """
    Разобрать время из текста: 13:30, в 14:50, 2026-07-10 09:00.
    Если время сегодня уже прошло — перенос на завтра.
    """
    now = now or datetime.now(TZ)
    text = text.strip().lower()

    m = re.search(r"(\d{4})-(\d{2})-(\d{2})[ T](\d{1,2}):(\d{2})", text)
    if m:
        return datetime(
            int(m.group(1)), int(m.group(2)), int(m.group(3)),
            int(m.group(4)), int(m.group(5)), tzinfo=TZ,
        )

    m = re.search(r"(?:в\s+)?(\d{1,2})[:\.](\d{2})", text)
    if m:
        h, mi = int(m.group(1)), int(m.group(2))
        target = now.replace(hour=h, minute=mi, second=0, microsecond=0)
        if target <= now:
            target += timedelta(days=1)
        return target

    return None


def schedule_commands(
    device_id: str,
    run_at: datetime,
    commands: list[dict[str, Any]],
    label: str = "",
    order_id: str | None = None,
) -> str:
    """Добавить отложенную задачу. Возвращает job_id."""
    job_id = uuid.uuid4().hex[:8]
    entry = {
        "id": job_id,
        "device_id": device_id,
        "run_at": run_at.timestamp(),
        "run_at_label": run_at.strftime("%Y-%m-%d %H:%M"),
        "commands": commands,
        "label": label or f"Задача {run_at.strftime('%H:%M')}",
        "status": "pending",
        "status_label": SCHEDULE_STATUS["pending"],
        "order_id": order_id,
        "created_at": time.time(),
    }
    app_state.scheduled_jobs.appendleft(entry)
    app_state.add_log(
        f"Запланировано на {entry['run_at_label']}: {label or commands[0].get('cmd', '?')}",
        "info",
    )
    return job_id


async def scheduler_loop() -> None:
    """Фоновый цикл: выполнять задачи по расписанию."""
    while True:
        now = time.time()
        for job in list(app_state.scheduled_jobs):
            if job.get("status") != "pending":
                continue
            if now < job["run_at"]:
                continue
            device_id = job["device_id"]
            try:
                app_state.enqueue_commands(device_id, job["commands"], order_id=job.get("order_id"))
                job["status"] = "done"
                job["status_label"] = SCHEDULE_STATUS["done"]
                app_state.add_log(f"По расписанию ({job['run_at_label']}): {job['label']}", "success")
            except Exception as exc:
                job["status"] = "error"
                job["status_label"] = SCHEDULE_STATUS["error"]
                app_state.add_log(f"Ошибка расписания: {exc}", "error")
        await app_state.notify_sse()
        await asyncio.sleep(1)
