"""
ESP32 neiro — бэкенд-сервер Dynamic Agent.

FastAPI-приложение для Kali Linux:
  - приём телеметрии от ESP32
  - Function Calling через LLM
  - веб-дашборд на русском языке (SSE + Tailwind CDN)

Запуск:
  uvicorn main:app --host 0.0.0.0 --port 8000
"""

from __future__ import annotations

import asyncio
import json
import os
import time
from contextlib import asynccontextmanager
from typing import Any

from dotenv import load_dotenv
from fastapi import Depends, FastAPI, Request
from fastapi.responses import HTMLResponse, JSONResponse
from pydantic import BaseModel, Field
from sse_starlette.sse import EventSourceResponse

from auth import get_configured_token, verify_api_token
from direct_interpreter import try_interpret_direct_command
from llm_agent import analyze_i2c_change, run_llm_agent
from state import app_state

# Загружаем переменные окружения из .env
load_dotenv()

# Таймаут «онлайн» для ESP32 (секунды без телеметрии = offline)
DEVICE_OFFLINE_SEC = 15


# --- Фоновые задачи LLM (не блокируют телеметрию ESP32) ---


async def _bg_analyze_i2c(device_id: str, new_devices: set[str], removed_devices: set[str]) -> None:
    """Фоновый вызов LLM при изменении I2C-шины."""
    try:
        commands = await analyze_i2c_change(device_id, new_devices, removed_devices)
        if commands:
            app_state.enqueue_commands(device_id, commands)
    except Exception as exc:
        app_state.add_log(f"Ошибка фонового LLM (I2C): {exc}", "error")
    await app_state.notify_sse()


async def _bg_direct_command(device_id: str | None, message: str) -> None:
    """Фоновый вызов LLM для прямого приказа оператора."""
    try:
        commands = try_interpret_direct_command(message)
        if commands:
            blink = commands[0]
            app_state.add_log(
                f"Быстрый разбор: мигание GPIO {blink['pin']} "
                f"{blink['hz']} Гц, {blink['duration_ms'] // 1000} с",
                "success",
            )
        else:
            commands = await run_llm_agent(message, device_id=device_id, trigger="direct")

        if not device_id:
            app_state.add_log("ESP32 не подключён — команды некуда отправить.", "error")
        elif commands:
            app_state.enqueue_commands(device_id, commands)
            names = ", ".join(c.get("cmd", "?") for c in commands)
            app_state.add_log(f"Команды в очереди ESP32: {names}", "info")
    except Exception as exc:
        app_state.add_log(f"Ошибка фонового LLM (приказ): {exc}", "error")
    await app_state.notify_sse()


# --- Pydantic-модели запросов ---


class TelemetryPayload(BaseModel):
    """Тело POST /api/telemetry от ESP32."""

    device_id: str
    uptime_ms: int = 0
    gpio: list[dict[str, Any]] = Field(default_factory=list)
    i2c_devices: list[str] = Field(default_factory=list)


class DirectCommandPayload(BaseModel):
    """Прямой приказ пользователя для нейросети."""

    message: str
    device_id: str | None = None


class PromptPayload(BaseModel):
    """Изменение системного промпта."""

    prompt: str


class RebootPayload(BaseModel):
    """Опциональный device_id для перезагрузки."""

    device_id: str | None = None


# --- Фоновая задача: помечать устройства offline ---


async def device_watchdog() -> None:
    """Периодически проверяет, не пропала ли связь с ESP32."""
    while True:
        await asyncio.sleep(5)
        now = time.time()
        for dev in app_state.devices.values():
            if now - dev.last_seen > DEVICE_OFFLINE_SEC:
                if dev.online:
                    dev.online = False
                    app_state.add_log("ESP32 не отвечает — возможно, offline.", "error")
                    await app_state.notify_sse()


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Старт/остановка фоновых задач."""
    task = asyncio.create_task(device_watchdog())
    app_state.add_log("Сервер ESP32 neiro запущен. Ожидаю телеметрию…", "success")
    yield
    task.cancel()


app = FastAPI(
    title="ESP32 neiro",
    description="Dynamic Agent — экспериментальный ИИ-агент для ESP32",
    lifespan=lifespan,
)


# ==================== API эндпоинты ====================


@app.post("/api/telemetry", dependencies=[Depends(verify_api_token)])
async def api_telemetry(payload: TelemetryPayload) -> JSONResponse:
    """
    Принять телеметрию от ESP32, обновить состояние, вернуть очередь команд.

    LLM вызывается в фоне — ответ ESP32 мгновенный (только очередь команд).
    """
    device_id = payload.device_id
    prev_dev = app_state.devices.get(device_id)
    was_offline = prev_dev is None or not prev_dev.online
    new_i2c, removed_i2c = app_state.update_device(
        device_id=device_id,
        uptime_ms=payload.uptime_ms,
        gpio=payload.gpio,
        i2c_devices=payload.i2c_devices,
    )

    if was_offline:
        app_state.add_log(
            f"ESP32 на связи ({device_id}). I2C: {', '.join(payload.i2c_devices) or 'пусто'}.",
            "success",
        )

    # Dynamic Agent: I2C hot-plug → LLM в фоне (не блокируем ESP32)
    if new_i2c or removed_i2c:
        asyncio.create_task(_bg_analyze_i2c(device_id, new_i2c, removed_i2c))

    # Отдаём накопленную очередь команд ESP32 сразу
    commands = app_state.pop_commands(device_id)
    await app_state.notify_sse()
    return JSONResponse({"commands": commands})


@app.post("/api/reboot_esp", dependencies=[Depends(verify_api_token)])
async def api_reboot_esp(payload: RebootPayload | None = None) -> JSONResponse:
    """Добавить команду перезагрузки ESP32 в очередь."""
    device_id = (payload.device_id if payload else None) or app_state.get_primary_device_id()
    if not device_id:
        return JSONResponse({"ok": False, "error": "Нет подключённого ESP32"}, status_code=404)

    app_state.enqueue_commands(device_id, [{"cmd": "reboot"}])
    app_state.add_log("Команда перезагрузки ESP32 поставлена в очередь.", "warn")
    await app_state.notify_sse()
    return JSONResponse({"ok": True, "device_id": device_id})


@app.post("/api/direct_command", dependencies=[Depends(verify_api_token)])
async def api_direct_command(payload: DirectCommandPayload) -> JSONResponse:
    """Отправить прямой текстовый приказ в нейросеть (LLM в фоне)."""
    device_id = payload.device_id or app_state.get_primary_device_id()
    app_state.add_log(f"Прямой приказ оператора: «{payload.message}»", "info")

    asyncio.create_task(_bg_direct_command(device_id, payload.message))

    await app_state.notify_sse()
    return JSONResponse({"ok": True, "message": "Приказ принят, нейросеть думает в фоне…"})


@app.post("/api/ai/reset", dependencies=[Depends(verify_api_token)])
async def api_ai_reset() -> JSONResponse:
    """Сбросить контекст нейросети («Перезапустить сервер ИИ»)."""
    app_state.reset_ai()
    await app_state.notify_sse()
    return JSONResponse({"ok": True})


@app.get("/api/settings/prompt")
async def get_prompt() -> JSONResponse:
    """Получить текущий системный промпт."""
    return JSONResponse({"prompt": app_state.system_prompt})


@app.post("/api/settings/prompt", dependencies=[Depends(verify_api_token)])
async def set_prompt(payload: PromptPayload) -> JSONResponse:
    """Изменить системный промпт из браузера."""
    app_state.system_prompt = payload.prompt.strip()
    app_state.add_log("Системный промпт обновлён оператором.", "info")
    await app_state.notify_sse()
    return JSONResponse({"ok": True, "prompt": app_state.system_prompt})


@app.get("/api/state")
async def api_state() -> JSONResponse:
    """Полный снимок состояния для polling-дашборда."""
    return JSONResponse(app_state.snapshot())


@app.get("/api/events")
async def api_events(request: Request) -> EventSourceResponse:
    """
    Server-Sent Events: поток обновлений для веб-дашборда.

    Клиент получает событие «update» при любом изменении состояния.
    """

    async def event_generator():
        queue: asyncio.Queue = asyncio.Queue(maxsize=32)
        app_state.sse_subscribers.append(queue)
        try:
            # Сразу отдаём текущий снимок
            yield {"event": "update", "data": json.dumps(app_state.snapshot(), ensure_ascii=False)}
            while True:
                if await request.is_disconnected():
                    break
                try:
                    await asyncio.wait_for(queue.get(), timeout=2.0)
                except asyncio.TimeoutError:
                    pass
                yield {"event": "update", "data": json.dumps(app_state.snapshot(), ensure_ascii=False)}
        finally:
            if queue in app_state.sse_subscribers:
                app_state.sse_subscribers.remove(queue)

    return EventSourceResponse(event_generator())


# ==================== Веб-дашборд (один HTML-файл) ====================

DASHBOARD_HTML = """<!DOCTYPE html>
<html lang="ru">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 neiro — Dynamic Agent</title>
  <script src="https://cdn.tailwindcss.com"></script>
  <style>
    body { background: #020617; }
    .glow { box-shadow: 0 0 20px rgba(56, 189, 248, 0.15); }
    .pin-high { background: #166534; border-color: #22c55e; }
    .pin-low { background: #7f1d1d; border-color: #ef4444; }
    .pin-input { background: #1e3a5f; border-color: #3b82f6; }
    .pin-unset { background: #1e293b; border-color: #475569; }
    .log-info { color: #94a3b8; }
    .log-success { color: #4ade80; }
    .log-warn { color: #fbbf24; }
    .log-error { color: #f87171; }
    ::-webkit-scrollbar { width: 6px; }
    ::-webkit-scrollbar-thumb { background: #334155; border-radius: 3px; }
  </style>
</head>
<body class="text-slate-200 min-h-screen font-sans">

  <!-- Шапка -->
  <header class="border-b border-slate-800 bg-slate-900/80 backdrop-blur sticky top-0 z-10">
    <div class="max-w-7xl mx-auto px-4 py-4 flex items-center justify-between">
      <div>
        <h1 class="text-xl font-bold text-sky-400 tracking-wide">ESP32 neiro</h1>
        <p class="text-xs text-slate-500">Dynamic Agent — экспериментальный ИИ-инженер</p>
      </div>
      <div id="statusBadge" class="flex items-center gap-2 px-3 py-1 rounded-full bg-slate-800 text-sm">
        <span id="statusDot" class="w-2.5 h-2.5 rounded-full bg-slate-600"></span>
        <span id="statusText">Ожидание ESP32…</span>
      </div>
    </div>
  </header>

  <main class="max-w-7xl mx-auto px-4 py-6 grid grid-cols-1 lg:grid-cols-3 gap-6">

    <!-- Левая колонка: лог + мысли ИИ -->
    <div class="lg:col-span-2 space-y-6">

      <!-- Блок: Что происходит сейчас -->
      <section class="bg-slate-900 rounded-xl border border-slate-800 glow p-5">
        <h2 class="text-sm font-semibold text-sky-300 uppercase tracking-wider mb-3">
          Что происходит сейчас
        </h2>
        <div id="activityLog" class="h-56 overflow-y-auto space-y-1 text-sm font-mono">
          <p class="log-info">Подключение к серверу…</p>
        </div>
      </section>

      <!-- Блок: Мысли нейросети -->
      <section class="bg-slate-900 rounded-xl border border-slate-800 glow p-5">
        <h2 class="text-sm font-semibold text-violet-300 uppercase tracking-wider mb-3">
          Мысли нейросети
        </h2>
        <div id="aiThoughts" class="h-48 overflow-y-auto space-y-3 text-sm">
          <p class="text-slate-500 italic">Нейросеть ещё не рассуждала…</p>
        </div>
      </section>

      <!-- Блок: Срез железа -->
      <section class="bg-slate-900 rounded-xl border border-slate-800 glow p-5">
        <h2 class="text-sm font-semibold text-emerald-300 uppercase tracking-wider mb-3">
          Текущий срез железа
        </h2>
        <div class="mb-4">
          <span class="text-xs text-slate-500">I2C-устройства:</span>
          <div id="i2cList" class="flex flex-wrap gap-2 mt-1">
            <span class="text-slate-600 text-sm">—</span>
          </div>
        </div>
        <div id="gpioGrid" class="grid grid-cols-4 sm:grid-cols-6 md:grid-cols-8 gap-2">
          <!-- Карточки GPIO заполняются JS -->
        </div>
        <p class="text-xs text-slate-600 mt-3">Аптайм: <span id="uptime">—</span></p>
      </section>
    </div>

    <!-- Правая колонка: панель управления -->
    <div class="space-y-6">

      <section class="bg-slate-900 rounded-xl border border-slate-800 glow p-5 space-y-4">
        <h2 class="text-sm font-semibold text-amber-300 uppercase tracking-wider">
          Панель управления
        </h2>

        <button onclick="rebootEsp()"
          class="w-full py-2.5 rounded-lg bg-red-900/50 hover:bg-red-800/60 border border-red-700 text-red-200 text-sm transition">
          Перезагрузить ESP32
        </button>

        <button onclick="resetAi()"
          class="w-full py-2.5 rounded-lg bg-violet-900/50 hover:bg-violet-800/60 border border-violet-700 text-violet-200 text-sm transition">
          Перезапустить сервер ИИ
        </button>

        <hr class="border-slate-800">

        <label class="text-xs text-slate-400 block">Прямой приказ</label>
        <textarea id="directCmd" rows="3" placeholder="ИИ, выруби всё и мигни три раза светодиодом"
          class="w-full bg-slate-800 border border-slate-700 rounded-lg p-3 text-sm text-slate-200 resize-none focus:outline-none focus:border-sky-600"></textarea>
        <button onclick="sendDirect()"
          class="w-full py-2.5 rounded-lg bg-sky-700 hover:bg-sky-600 text-white text-sm transition">
          Отправить приказ
        </button>

        <hr class="border-slate-800">

        <label class="text-xs text-slate-400 block">Системный промпт ИИ</label>
        <textarea id="systemPrompt" rows="5"
          class="w-full bg-slate-800 border border-slate-700 rounded-lg p-3 text-sm text-slate-200 resize-none focus:outline-none focus:border-sky-600"></textarea>
        <button onclick="savePrompt()"
          class="w-full py-2.5 rounded-lg bg-slate-700 hover:bg-slate-600 text-slate-200 text-sm transition">
          Сохранить промпт
        </button>
      </section>

      <section class="bg-slate-900 rounded-xl border border-slate-800 p-4 text-xs text-slate-500 space-y-1">
        <p>Очередь команд: <span id="pendingCmds" class="text-sky-400">0</span></p>
        <p>Device ID: <span id="deviceId" class="text-slate-400 font-mono">—</span></p>
        <p>LLM: <span id="llmProvider" class="text-violet-400">—</span></p>
      </section>
    </div>
  </main>

  <script>
    // API-токен (инжектируется сервером; пустой = без авторизации)
    const API_TOKEN = __API_TOKEN_JSON__;

    function authHeaders(extra) {
      const h = Object.assign({'Content-Type': 'application/json'}, extra || {});
      if (API_TOKEN) h['X-API-Token'] = API_TOKEN;
      return h;
    }

    // Защита от XSS при вставке текста из лога/мыслей ИИ
    function escapeHtml(str) {
      if (str == null) return '';
      return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
    }

    // --- Утилиты форматирования ---
    function formatUptime(ms) {
      if (!ms) return '—';
      const s = Math.floor(ms / 1000);
      const m = Math.floor(s / 60);
      const h = Math.floor(m / 60);
      if (h > 0) return h + 'ч ' + (m % 60) + 'м';
      if (m > 0) return m + 'м ' + (s % 60) + 'с';
      return s + 'с';
    }

    function formatTime(ts) {
      return new Date(ts * 1000).toLocaleTimeString('ru-RU');
    }

    // --- Обновление UI из снимка состояния ---
    function renderState(data) {
      // Статус подключения
      const dot = document.getElementById('statusDot');
      const txt = document.getElementById('statusText');
      if (data.online) {
        dot.className = 'w-2.5 h-2.5 rounded-full bg-green-400 animate-pulse';
        txt.textContent = 'ESP32 на связи';
      } else {
        dot.className = 'w-2.5 h-2.5 rounded-full bg-red-500';
        txt.textContent = 'ESP32 offline';
      }

      document.getElementById('deviceId').textContent = data.device_id || '—';
      document.getElementById('uptime').textContent = formatUptime(data.uptime_ms);
      document.getElementById('pendingCmds').textContent = data.pending_commands || 0;

      document.getElementById('llmProvider').textContent = data.active_llm_provider || '—';

      // Лог активности
      const logEl = document.getElementById('activityLog');
      if (data.activity_log && data.activity_log.length) {
        logEl.innerHTML = data.activity_log.map(e =>
          `<p class="log-${escapeHtml(e.level)}">[${formatTime(e.ts)}] ${escapeHtml(e.message)}</p>`
        ).join('');
      }

      // Мысли ИИ
      const thoughtsEl = document.getElementById('aiThoughts');
      if (data.ai_thoughts && data.ai_thoughts.length) {
        thoughtsEl.innerHTML = data.ai_thoughts.map(t =>
          `<div class="border-l-2 border-violet-600 pl-3">
            <span class="text-xs text-slate-500">${formatTime(t.ts)}</span>
            <p class="text-slate-300 mt-0.5">${escapeHtml(t.text)}</p>
          </div>`
        ).join('');
      }

      // I2C
      const i2cEl = document.getElementById('i2cList');
      if (data.i2c_devices && data.i2c_devices.length) {
        i2cEl.innerHTML = data.i2c_devices.map(a =>
          `<span class="px-2 py-1 rounded bg-emerald-900/40 border border-emerald-700 text-emerald-300 text-xs font-mono">${escapeHtml(a)}</span>`
        ).join('');
      } else {
        i2cEl.innerHTML = '<span class="text-slate-600 text-sm">нет устройств</span>';
      }

      // GPIO-карта
      const gpioEl = document.getElementById('gpioGrid');
      if (data.gpio && data.gpio.length) {
        gpioEl.innerHTML = data.gpio.map(g => {
          let cls = 'pin-unset';
          if (g.mode === 'OUTPUT') cls = g.value ? 'pin-high' : 'pin-low';
          else if (g.mode === 'INPUT') cls = 'pin-input';
          const val = g.value >= 0 ? g.value : '—';
          return `<div class="${cls} border rounded-lg p-2 text-center text-xs">
            <div class="font-bold text-slate-200">${g.pin}</div>
            <div class="text-slate-400">${g.mode || '?'}</div>
            <div class="font-mono">${val}</div>
          </div>`;
        }).join('');
      }

      // Промпт (только если поле пустое — не затираем редактирование)
      const promptEl = document.getElementById('systemPrompt');
      if (!promptEl.dataset.touched && data.system_prompt) {
        promptEl.value = data.system_prompt;
      }
    }

    // --- SSE-подключение с fallback на polling ---
    let sseOk = false;

    function connectSSE() {
      const es = new EventSource('/api/events');
      es.addEventListener('update', (ev) => {
        sseOk = true;
        try { renderState(JSON.parse(ev.data)); } catch(e) {}
      });
      es.onerror = () => {
        sseOk = false;
        es.close();
        setTimeout(connectSSE, 3000);
      };
    }

    async function pollState() {
      if (sseOk) return;
      try {
        const r = await fetch('/api/state');
        const data = await r.json();
        renderState(data);
      } catch(e) {}
    }

    // --- Действия панели управления ---
    async function rebootEsp() {
      await fetch('/api/reboot_esp', { method: 'POST', headers: authHeaders(), body: '{}' });
    }

    async function resetAi() {
      await fetch('/api/ai/reset', { method: 'POST', headers: authHeaders() });
    }

    async function sendDirect() {
      const msg = document.getElementById('directCmd').value.trim();
      if (!msg) return;
      await fetch('/api/direct_command', {
        method: 'POST',
        headers: authHeaders(),
        body: JSON.stringify({ message: msg })
      });
      document.getElementById('directCmd').value = '';
    }

    async function savePrompt() {
      const prompt = document.getElementById('systemPrompt').value;
      await fetch('/api/settings/prompt', {
        method: 'POST',
        headers: authHeaders(),
        body: JSON.stringify({ prompt })
      });
    }

    document.getElementById('systemPrompt').addEventListener('input', function() {
      this.dataset.touched = '1';
    });

    // Старт
    connectSSE();
    setInterval(pollState, 2000);
    fetch('/api/settings/prompt').then(r => r.json()).then(d => {
      document.getElementById('systemPrompt').value = d.prompt || '';
    });
  </script>
</body>
</html>"""


@app.get("/", response_class=HTMLResponse)
async def dashboard() -> HTMLResponse:
    """Главная страница — веб-дашборд с инжектированным API-токеном."""
    token_json = json.dumps(get_configured_token())
    html = DASHBOARD_HTML.replace("__API_TOKEN_JSON__", token_json)
    return HTMLResponse(html)


# Точка входа для `python main.py`
if __name__ == "__main__":
    import uvicorn

    host = os.getenv("HOST", "0.0.0.0")
    port = int(os.getenv("PORT", "8000"))
    uvicorn.run("main:app", host=host, port=port, reload=True)
