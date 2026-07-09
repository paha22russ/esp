"""
Глобальное состояние сервера ESP32 neiro.

Хранит снимок железа, очередь команд для ESP32, лог событий,
рассуждения нейросети и настройки системного промпта — всё в памяти процесса.
"""

from __future__ import annotations

import asyncio
import time
import uuid
from collections import deque
from dataclasses import dataclass, field
from typing import Any

# Человекочитаемые подписи статусов приказов
ORDER_STATUS_LABELS: dict[str, str] = {
    "received": "Получен",
    "processing": "Обработка",
    "queued": "В очереди на ESP32",
    "sent": "Отправлен на ESP32",
    "no_device": "ESP32 не подключён",
    "no_commands": "Команды не сгенерированы",
    "error": "Ошибка",
}

# Системный промпт по умолчанию для Dynamic Agent
DEFAULT_SYSTEM_PROMPT = (
    "Ты — безумный ИИ-инженер, который исследует подключенное к ESP32 железо на лету. "
    "Экспериментируй смело. Анализируй I2C-устройства, GPIO и предлагай действия через доступные инструменты. "
    "Для мигания светодиодом используй blink_led (не digital_write). "
    "GPIO 2 — встроенный LED (active-low: LOW=включен, HIGH=выключен). "
    "НИКОГДА не используй GPIO 6–11 (flash) и 21–22 (I2C шина). Не перезагружай ESP32 без веской причины."
)


@dataclass
class DeviceState:
    """Состояние одного ESP32-устройства."""

    device_id: str = ""
    last_seen: float = 0.0
    uptime_ms: int = 0
    gpio: list[dict[str, Any]] = field(default_factory=list)
    i2c_devices: list[str] = field(default_factory=list)
    firmware_version: str = ""
    online: bool = False


class AppState:
    """
    Центральное хранилище состояния приложения.

    Все поля обновляются из эндпоинтов FastAPI и читаются дашбордом через SSE/polling.
    """

    def __init__(self) -> None:
        # Состояние ESP32 по device_id (обычно MAC-адрес)
        self.devices: dict[str, DeviceState] = {}
        # Очередь команд, ожидающих отправки на ESP32: device_id -> [cmd, ...]
        self.command_queues: dict[str, list[dict[str, Any]]] = {}
        # Живой лог простым языком для дашборда
        self.activity_log: deque[dict[str, Any]] = deque(maxlen=200)
        # Рассуждения нейросети
        self.ai_thoughts: deque[dict[str, Any]] = deque(maxlen=100)
        # Системный промпт (редактируется из браузера)
        self.system_prompt: str = DEFAULT_SYSTEM_PROMPT
        # История диалога с LLM для контекста
        self.llm_history: list[dict[str, str]] = []
        # Активный LLM-провайдер (отображается в дашборде)
        self.active_llm_provider: str = "—"
        # История прямых приказов оператора
        self.command_orders: deque[dict[str, Any]] = deque(maxlen=50)
        # Предыдущие I2C-снимки для обнаружения hot-plug
        self._prev_i2c: dict[str, set[str]] = {}
        # Подписчики SSE: asyncio.Queue для каждого клиента
        self.sse_subscribers: list[asyncio.Queue] = []
        # Блокировка для потокобезопасности (async)
        self._lock = asyncio.Lock()

    def add_log(self, message: str, level: str = "info") -> None:
        """Добавить запись в лог активности."""
        entry = {
            "ts": time.time(),
            "message": message,
            "level": level,
        }
        self.activity_log.appendleft(entry)

    def add_thought(self, text: str, source: str = "llm") -> None:
        """Сохранить рассуждение нейросети."""
        entry = {
            "ts": time.time(),
            "text": text,
            "source": source,
        }
        self.ai_thoughts.appendleft(entry)

    def enqueue_commands(
        self,
        device_id: str,
        commands: list[dict[str, Any]],
        order_id: str | None = None,
    ) -> None:
        """Поставить команды в очередь для конкретного ESP32."""
        if device_id not in self.command_queues:
            self.command_queues[device_id] = []
        for cmd in commands:
            tagged = dict(cmd)
            if order_id:
                tagged["_order_id"] = order_id
            self.command_queues[device_id].append(tagged)

    def pop_commands(self, device_id: str) -> list[dict[str, Any]]:
        """Забрать и очистить очередь команд для устройства."""
        raw = self.command_queues.pop(device_id, [])
        sent_order_ids: list[str] = []
        commands: list[dict[str, Any]] = []
        for cmd in raw:
            order_id = cmd.pop("_order_id", None)
            if order_id:
                sent_order_ids.append(order_id)
            commands.append(cmd)
        if sent_order_ids:
            self.mark_orders_sent(sent_order_ids)
        return commands

    def create_order(self, message: str, device_id: str | None = None) -> str:
        """Создать запись приказа и вернуть её id."""
        order_id = uuid.uuid4().hex[:8]
        self.command_orders.appendleft(
            {
                "id": order_id,
                "message": message,
                "status": "received",
                "status_label": ORDER_STATUS_LABELS["received"],
                "created_at": time.time(),
                "updated_at": time.time(),
                "device_id": device_id,
                "commands_summary": None,
                "error": None,
            }
        )
        return order_id

    def update_order(self, order_id: str, status: str, **extra: Any) -> None:
        """Обновить статус приказа."""
        for order in self.command_orders:
            if order["id"] != order_id:
                continue
            order["status"] = status
            order["status_label"] = ORDER_STATUS_LABELS.get(status, status)
            order["updated_at"] = time.time()
            order.update(extra)
            return

    def mark_orders_sent(self, order_ids: list[str]) -> None:
        """Пометить приказы как отправленные на ESP32."""
        for order_id in order_ids:
            self.update_order(order_id, status="sent")

    def update_device(
        self,
        device_id: str,
        uptime_ms: int,
        firmware_version: str,
        gpio: list[dict[str, Any]],
        i2c_devices: list[str],
    ) -> tuple[set[str], set[str]]:
        """
        Обновить снимок железа устройства.

        Возвращает (новые_i2c, исчезнувшие_i2c) для триггера LLM.
        """
        now = time.time()
        prev = self._prev_i2c.get(device_id, set())
        current = set(i2c_devices)
        new_devices = current - prev
        removed_devices = prev - current
        self._prev_i2c[device_id] = current

        prev_fw = self.devices[device_id].firmware_version if device_id in self.devices else ""
        self.devices[device_id] = DeviceState(
            device_id=device_id,
            last_seen=now,
            uptime_ms=uptime_ms,
            firmware_version=firmware_version or prev_fw,
            gpio=gpio,
            i2c_devices=i2c_devices,
            online=True,
        )
        return new_devices, removed_devices

    def get_primary_device_id(self) -> str | None:
        """Вернуть ID первого онлайн-устройства (для одиночного ESP32)."""
        if not self.devices:
            return None
        # Самое недавно виденное устройство
        return max(self.devices.values(), key=lambda d: d.last_seen).device_id

    def reset_ai(self) -> None:
        """Сбросить контекст нейросети («Перезапустить сервер ИИ»)."""
        self.llm_history.clear()
        self.active_llm_provider = "—"
        self.add_log("Контекст нейросети сброшен.", "warn")

    def snapshot(self) -> dict[str, Any]:
        """Полный снимок для API / дашборда."""
        primary = self.get_primary_device_id()
        device = self.devices.get(primary) if primary else None
        return {
            "device_id": primary,
            "online": device.online if device else False,
            "last_seen": device.last_seen if device else 0,
            "uptime_ms": device.uptime_ms if device else 0,
            "firmware_version": device.firmware_version if device else "",
            "gpio": device.gpio if device else [],
            "i2c_devices": device.i2c_devices if device else [],
            "activity_log": list(self.activity_log),
            "ai_thoughts": list(self.ai_thoughts),
            "system_prompt": self.system_prompt,
            "pending_commands": sum(len(q) for q in self.command_queues.values()),
            "active_llm_provider": self.active_llm_provider,
            "command_orders": list(self.command_orders),
        }

    async def notify_sse(self, event_type: str = "update") -> None:
        """Уведомить всех SSE-подписчиков об изменении состояния."""
        dead: list[asyncio.Queue] = []
        for q in self.sse_subscribers:
            try:
                q.put_nowait(event_type)
            except asyncio.QueueFull:
                dead.append(q)
        for q in dead:
            if q in self.sse_subscribers:
                self.sse_subscribers.remove(q)


# Единственный экземпляр состояния на весь процесс сервера
app_state = AppState()
