"""
Обёртка Function Calling для Dynamic Agent ESP32 neiro.

Поддерживает:
  - Google Gemini (облако, API-ключ)
  - Ollama на homeserv (локальная сеть, OpenAI-совместимый API)
  - OpenAI, Anthropic
  - Режим auto: Gemini → автоматический fallback на Ollama при исчерпании квоты

Конвертирует tool_calls LLM в команды прошивки ESP32.
"""

from __future__ import annotations

import json
import os
import re
from typing import Any

from state import app_state

# --- Описание инструментов для LLM (Function Calling) ---

ESP_TOOLS_OPENAI = [
    {
        "type": "function",
        "function": {
            "name": "set_pin_mode",
            "description": "Настроить режим GPIO-пина ESP32: INPUT или OUTPUT",
            "parameters": {
                "type": "object",
                "properties": {
                    "pin": {"type": "integer", "description": "Номер GPIO-пина (2, 4, 5, 12-19, 21-23, 25-27, 32-33)"},
                    "mode": {"type": "string", "enum": ["INPUT", "OUTPUT"], "description": "Режим пина"},
                },
                "required": ["pin", "mode"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "digital_write_pin",
            "description": "Подать HIGH (1) или LOW (0) на выходной GPIO-пин",
            "parameters": {
                "type": "object",
                "properties": {
                    "pin": {"type": "integer", "description": "Номер GPIO-пина"},
                    "value": {"type": "integer", "enum": [0, 1], "description": "0 = LOW, 1 = HIGH"},
                },
                "required": ["pin", "value"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "init_i2c_display",
            "description": "Инициализировать I2C-дисплей: 0x3C (OLED SSD1306) или 0x27 (LCD 1602)",
            "parameters": {
                "type": "object",
                "properties": {
                    "address": {
                        "type": "string",
                        "description": "I2C-адрес в hex, например 0x3C или 0x27",
                    },
                },
                "required": ["address"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "print_on_display",
            "description": "Вывести текст на инициализированный I2C-дисплей",
            "parameters": {
                "type": "object",
                "properties": {
                    "text": {"type": "string", "description": "Текст для вывода"},
                    "line": {"type": "integer", "description": "Номер строки (0 — верхняя)", "default": 0},
                },
                "required": ["text"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "reboot_esp32",
            "description": "Принудительно перезагрузить ESP32",
            "parameters": {"type": "object", "properties": {}},
        },
    },
]

# Ключевые слова ошибок, при которых переключаемся на Ollama
_GEMINI_FALLBACK_KEYWORDS = (
    "rate_limit",
    "quota",
    "resource_exhausted",
    "resource exhausted",
    "insufficient_quota",
    "billing",
    "exceeded",
    "too many requests",
    "429",
    "503",
    "unavailable",
)


def _tool_call_to_esp_command(name: str, args: dict[str, Any]) -> dict[str, Any] | None:
    """Преобразовать вызов инструмента LLM в команду для прошивки ESP32."""
    if name == "set_pin_mode":
        return {"cmd": "pin_mode", "pin": args["pin"], "mode": args["mode"]}
    if name == "digital_write_pin":
        return {"cmd": "digital_write", "pin": args["pin"], "value": args["value"]}
    if name == "init_i2c_display":
        addr = str(args["address"])
        if not addr.startswith("0x"):
            addr = f"0x{int(addr):02X}" if addr.isdigit() else addr
        return {"cmd": "init_display", "address": addr}
    if name == "print_on_display":
        return {
            "cmd": "print_text",
            "text": args.get("text", ""),
            "line": args.get("line", 0),
        }
    if name == "reboot_esp32":
        return {"cmd": "reboot"}
    return None


def _build_hardware_context(device_id: str | None) -> str:
    """Собрать текстовое описание текущего железа для промпта LLM."""
    if not device_id or device_id not in app_state.devices:
        return "ESP32 ещё не подключался. Железо неизвестно."

    dev = app_state.devices[device_id]
    gpio_lines = []
    for g in dev.gpio:
        mode = g.get("mode", "?")
        val = g.get("value", "?")
        gpio_lines.append(f"  GPIO {g.get('pin')}: mode={mode}, value={val}")

    i2c = ", ".join(dev.i2c_devices) if dev.i2c_devices else "нет устройств"
    return (
        f"Устройство: {device_id}\n"
        f"Аптайм: {dev.uptime_ms} мс\n"
        f"I2C-шина: {i2c}\n"
        f"GPIO:\n" + ("\n".join(gpio_lines) if gpio_lines else "  (пусто)")
    )


def _parse_text_tool_calls(content: str) -> list[dict[str, Any]]:
    """
    Извлечь tool calls из текста ответа Ollama.

    qwen2.5-coder часто возвращает JSON в content вместо поля tool_calls:
      {"name": "digital_write_pin", "arguments": {"pin": 2, "value": 1}}
    """
    commands: list[dict[str, Any]] = []
    if not content:
        return commands

    text = re.sub(r"```(?:json)?", "", content).replace("```", "").strip()
    decoder = json.JSONDecoder()
    idx = 0
    while idx < len(text):
        start = text.find("{", idx)
        if start == -1:
            break
        try:
            obj, end = decoder.raw_decode(text, start)
            idx = end
            if isinstance(obj, dict) and "name" in obj:
                args = obj.get("arguments") or obj.get("parameters") or {}
                cmd = _tool_call_to_esp_command(str(obj["name"]), args)
                if cmd:
                    commands.append(cmd)
        except json.JSONDecodeError:
            idx = start + 1

    return commands


def _parse_openai_response(response: Any) -> tuple[str, list[dict[str, Any]]]:
    """Разобрать ответ OpenAI-совместимого API: текст и ESP-команды."""
    message = response.choices[0].message
    reasoning = message.content or ""
    commands: list[dict[str, Any]] = []

    if message.tool_calls:
        for tc in message.tool_calls:
            args = json.loads(tc.function.arguments)
            cmd = _tool_call_to_esp_command(tc.function.name, args)
            if cmd:
                commands.append(cmd)
    elif reasoning:
        # Fallback для Ollama / qwen: команды в тексте JSON
        commands = _parse_text_tool_calls(reasoning)

    return reasoning, commands


def _parse_anthropic_response(response: Any) -> tuple[str, list[dict[str, Any]]]:
    """Разобрать ответ Anthropic Claude."""
    reasoning_parts: list[str] = []
    commands: list[dict[str, Any]] = []

    for block in response.content:
        if block.type == "text":
            reasoning_parts.append(block.text)
        elif block.type == "tool_use":
            cmd = _tool_call_to_esp_command(block.name, block.input)
            if cmd:
                commands.append(cmd)

    return "\n".join(reasoning_parts), commands


def _anthropic_tools() -> list[dict[str, Any]]:
    """Конвертировать схему tools в формат Anthropic."""
    tools = []
    for t in ESP_TOOLS_OPENAI:
        fn = t["function"]
        tools.append(
            {
                "name": fn["name"],
                "description": fn["description"],
                "input_schema": fn["parameters"],
            }
        )
    return tools


def _should_fallback_to_ollama(exc: Exception) -> bool:
    """
    Определить, нужно ли переключиться на локальную Ollama.

    Срабатывает при исчерпании квоты Gemini, rate limit и временной недоступности.
    """
    msg = str(exc).lower()
    if any(kw in msg for kw in _GEMINI_FALLBACK_KEYWORDS):
        return True
    # OpenAI SDK кладёт HTTP-код в status_code
    status = getattr(exc, "status_code", None)
    if status in (429, 503, 500):
        return True
    # У вложенных ошибок OpenAI
    body = getattr(exc, "body", None)
    if body and isinstance(body, dict):
        err = body.get("error", {})
        code = str(err.get("code", "")).lower()
        if any(kw in code for kw in _GEMINI_FALLBACK_KEYWORDS):
            return True
    return False


def _call_openai_compatible(
    *,
    base_url: str | None,
    api_key: str,
    model: str,
    system: str,
    user_message: str,
    provider_label: str,
) -> tuple[str, list[dict[str, Any]]]:
    """
    Универсальный вызов через OpenAI-совместимый API.

    Используется для: OpenAI, Gemini (OpenAI-совместимый endpoint), Ollama.
    """
    from openai import OpenAI

    kwargs: dict[str, Any] = {"api_key": api_key or "ollama"}
    if base_url:
        kwargs["base_url"] = base_url

    client = OpenAI(**kwargs)

    messages: list[dict[str, Any]] = [{"role": "system", "content": system}]
    messages.extend(app_state.llm_history[-10:])
    messages.append({"role": "user", "content": user_message})

    response = client.chat.completions.create(
        model=model,
        messages=messages,
        tools=ESP_TOOLS_OPENAI,
        tool_choice="auto",
    )
    app_state.active_llm_provider = provider_label
    return _parse_openai_response(response)


def _call_gemini(system: str, user_message: str) -> tuple[str, list[dict[str, Any]]]:
    """
    Вызов Google Gemini через OpenAI-совместимый endpoint.

    Документация: https://ai.google.dev/gemini-api/docs/openai
    """
    base_url = os.getenv(
        "GOOGLE_OPENAI_BASE_URL",
        "https://generativelanguage.googleapis.com/v1beta/openai/",
    )
    api_key = os.getenv("GOOGLE_API_KEY", "")
    model = os.getenv("GOOGLE_MODEL", "gemini-2.0-flash")

    return _call_openai_compatible(
        base_url=base_url,
        api_key=api_key,
        model=model,
        system=system,
        user_message=user_message,
        provider_label=f"Gemini ({model})",
    )


def _call_ollama(system: str, user_message: str) -> tuple[str, list[dict[str, Any]]]:
    """
    Вызов локальной Ollama на homeserv.

    OpenAI-совместимый API: http://192.168.1.112:11434/v1
  API-ключ не проверяется — можно указать «ollama».
    """
    base_url = os.getenv("OLLAMA_BASE_URL", "http://192.168.1.112:11434/v1")
    api_key = os.getenv("OLLAMA_API_KEY", "ollama")
    model = os.getenv("OLLAMA_MODEL", "qwen2.5-coder:7b")

    return _call_openai_compatible(
        base_url=base_url,
        api_key=api_key,
        model=model,
        system=system,
        user_message=user_message,
        provider_label=f"Ollama ({model})",
    )


def _call_with_auto_fallback(system: str, user_message: str) -> tuple[str, list[dict[str, Any]]]:
    """
    Режим auto: сначала Gemini, при ошибке квоты — Ollama на homeserv.

    После первого fallback флаг gemini_fallback_active остаётся True
    до нажатия «Перезапустить сервер ИИ» в дашборде.
    """
    # Если уже переключились на Ollama — не тратим квоту Gemini
    if app_state.gemini_fallback_active:
        app_state.add_log("Используется локальная Ollama (Gemini в режиме ожидания).", "info")
        return _call_ollama(system, user_message)

    try:
        reasoning, commands = _call_gemini(system, user_message)
        return reasoning, commands
    except Exception as exc:
        if not _should_fallback_to_ollama(exc):
            raise

        app_state.gemini_fallback_active = True
        app_state.add_log(
            f"Квота/лимит Gemini исчерпан ({exc}). Переключение на Ollama (homeserv)…",
            "warn",
        )
        app_state.add_thought(
            f"Gemini недоступен ({exc}). Автоматически переключаюсь на локальную модель "
            f"{os.getenv('OLLAMA_MODEL', 'qwen2.5-coder:7b')} на homeserv.",
            "system",
        )
        return _call_ollama(system, user_message)


async def run_llm_agent(
    user_message: str,
    device_id: str | None = None,
    trigger: str = "telemetry",
) -> list[dict[str, Any]]:
    """
    Вызвать LLM с Function Calling и вернуть команды для ESP32.

    Провайдер задаётся LLM_PROVIDER в .env:
      auto    — Gemini + fallback на Ollama (рекомендуется)
      google  — только Gemini
      ollama  — только Ollama (локальная сеть)
      openai  — OpenAI
      anthropic — Anthropic Claude
    """
    provider = os.getenv("LLM_PROVIDER", "auto").lower()
    hardware_ctx = _build_hardware_context(device_id)

    system = app_state.system_prompt
    full_user = (
        f"[Триггер: {trigger}]\n"
        f"Снимок железа:\n{hardware_ctx}\n\n"
        f"Задача: {user_message}"
    )

    app_state.add_log(f"Нейросеть думает ({trigger}, провайдер: {provider})…", "info")

    reasoning = ""
    commands: list[dict[str, Any]] = []

    try:
        if provider == "auto":
            reasoning, commands = _call_with_auto_fallback(system, full_user)

        elif provider == "google":
            reasoning, commands = _call_gemini(system, full_user)

        elif provider == "ollama":
            reasoning, commands = _call_ollama(system, full_user)

        elif provider == "openai":
            reasoning, commands = _call_openai_compatible(
                base_url=None,
                api_key=os.getenv("OPENAI_API_KEY", ""),
                model=os.getenv("OPENAI_MODEL", "gpt-4o-mini"),
                system=system,
                user_message=full_user,
                provider_label=f"OpenAI ({os.getenv('OPENAI_MODEL', 'gpt-4o-mini')})",
            )

        elif provider == "anthropic":
            from anthropic import Anthropic

            client = Anthropic(api_key=os.getenv("ANTHROPIC_API_KEY"))
            model = os.getenv("ANTHROPIC_MODEL", "claude-3-5-haiku-latest")

            response = client.messages.create(
                model=model,
                max_tokens=1024,
                system=system,
                messages=[{"role": "user", "content": full_user}],
                tools=_anthropic_tools(),
            )
            app_state.active_llm_provider = f"Anthropic ({model})"
            reasoning, commands = _parse_anthropic_response(response)

        else:
            app_state.add_log(f"Неизвестный LLM_PROVIDER: {provider}", "error")
            return []

    except Exception as exc:
        # Если auto уже пробовал fallback внутри — сюда попадут только фатальные ошибки
        if provider == "auto" and not app_state.gemini_fallback_active:
            try:
                app_state.gemini_fallback_active = True
                app_state.add_log(f"Gemini недоступен, пробую Ollama… ({exc})", "warn")
                reasoning, commands = _call_ollama(system, full_user)
            except Exception as ollama_exc:
                app_state.add_log(f"Ошибка Ollama: {ollama_exc}", "error")
                app_state.add_thought(f"Оба провайдера недоступны. Ollama: {ollama_exc}", "error")
                return []
        else:
            app_state.add_log(f"Ошибка LLM: {exc}", "error")
            app_state.add_thought(f"Ошибка при обращении к нейросети: {exc}", "error")
            return []

    # Сохраняем контекст диалога
    app_state.llm_history.append({"role": "user", "content": full_user})
    if reasoning:
        app_state.llm_history.append({"role": "assistant", "content": reasoning})
        source = "ollama" if "Ollama" in app_state.active_llm_provider else "llm"
        app_state.add_thought(f"[{app_state.active_llm_provider}] {reasoning}", source)

    if commands:
        cmd_names = ", ".join(c.get("cmd", "?") for c in commands)
        app_state.add_log(
            f"Нейросеть ({app_state.active_llm_provider}) сгенерировала команды: {cmd_names}",
            "success",
        )
    else:
        app_state.add_log("Нейросеть не предложила команд для ESP32.", "info")

    return commands


async def analyze_i2c_change(
    device_id: str,
    new_devices: set[str],
    removed_devices: set[str],
) -> list[dict[str, Any]]:
    """Вызвать LLM при изменении состава I2C-шины (hot-plug)."""
    parts = []
    for addr in new_devices:
        parts.append(f"НОВОЕ устройство на I2C: {addr}")
        app_state.add_log(
            f"Обнаружено новое устройство на шине I2C (адрес {addr}). Нейросеть анализирует…",
            "warn",
        )
    for addr in removed_devices:
        parts.append(f"Устройство ОТКЛЮЧИЛОСЬ с I2C: {addr}")
        app_state.add_log(f"Устройство I2C {addr} исчезло с шины.", "warn")

    if not parts:
        return []

    message = (
        "Изменился состав I2C-шины ESP32 (hot-plug). "
        + " ".join(parts)
        + " Определи тип устройства и выполни подходящие действия."
    )
    return await run_llm_agent(message, device_id=device_id, trigger="i2c_change")
