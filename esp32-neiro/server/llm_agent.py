"""
Обёртка Function Calling для Dynamic Agent ESP32 neiro.

Поддерживает OpenAI, Anthropic и Google Gemini.
Конвертирует tool_calls LLM в команды прошивки ESP32.
"""

from __future__ import annotations

import json
import os
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


def _tool_call_to_esp_command(name: str, args: dict[str, Any]) -> dict[str, Any] | None:
    """
    Преобразовать вызов инструмента LLM в команду для прошивки ESP32.

    Формат команд совпадает с обработчиком в esp32_firmware.ino.
    """
    if name == "set_pin_mode":
        return {"cmd": "pin_mode", "pin": args["pin"], "mode": args["mode"]}
    if name == "digital_write_pin":
        return {"cmd": "digital_write", "pin": args["pin"], "value": args["value"]}
    if name == "init_i2c_display":
        addr = args["address"]
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


def _parse_openai_response(response: Any) -> tuple[str, list[dict[str, Any]]]:
    """Разобрать ответ OpenAI: текст рассуждений и список ESP-команд."""
    message = response.choices[0].message
    reasoning = message.content or ""
    commands: list[dict[str, Any]] = []

    if message.tool_calls:
        for tc in message.tool_calls:
            args = json.loads(tc.function.arguments)
            cmd = _tool_call_to_esp_command(tc.function.name, args)
            if cmd:
                commands.append(cmd)

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


def _parse_google_response(response: Any) -> tuple[str, list[dict[str, Any]]]:
    """Разобрать ответ Google Gemini."""
    reasoning_parts: list[str] = []
    commands: list[dict[str, Any]] = []

    for candidate in response.candidates:
        for part in candidate.content.parts:
            if hasattr(part, "text") and part.text:
                reasoning_parts.append(part.text)
            if hasattr(part, "function_call") and part.function_call:
                fc = part.function_call
                args = dict(fc.args) if fc.args else {}
                cmd = _tool_call_to_esp_command(fc.name, args)
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


def _google_tools() -> list[dict[str, Any]]:
    """Конвертировать схему tools в формат Google Gemini."""
    import google.generativeai as genai

    declarations = []
    for t in ESP_TOOLS_OPENAI:
        fn = t["function"]
        declarations.append(
            genai.protos.FunctionDeclaration(
                name=fn["name"],
                description=fn["description"],
                parameters=genai.protos.Schema(
                    type=genai.protos.Type.OBJECT,
                    properties={
                        k: genai.protos.Schema(type=genai.protos.Type.STRING)
                        for k in fn["parameters"].get("properties", {})
                    },
                    required=fn["parameters"].get("required", []),
                ),
            )
        )
    return declarations


async def run_llm_agent(
    user_message: str,
    device_id: str | None = None,
    trigger: str = "telemetry",
) -> list[dict[str, Any]]:
    """
    Вызвать LLM с Function Calling и вернуть команды для ESP32.

    :param user_message: Сообщение пользователя или описание события
    :param device_id: ID устройства для контекста железа
    :param trigger: Причина вызова (telemetry, direct, i2c_change)
    :return: Список команд для очереди ESP32
    """
    provider = os.getenv("LLM_PROVIDER", "openai").lower()
    hardware_ctx = _build_hardware_context(device_id)

    system = app_state.system_prompt
    full_user = (
        f"[Триггер: {trigger}]\n"
        f"Снимок железа:\n{hardware_ctx}\n\n"
        f"Задача: {user_message}"
    )

    app_state.add_log(f"Нейросеть думает ({trigger})…", "info")

    reasoning = ""
    commands: list[dict[str, Any]] = []

    try:
        if provider == "openai":
            from openai import OpenAI

            client = OpenAI(api_key=os.getenv("OPENAI_API_KEY"))
            model = os.getenv("OPENAI_MODEL", "gpt-4o-mini")

            messages = [{"role": "system", "content": system}]
            messages.extend(app_state.llm_history[-10:])
            messages.append({"role": "user", "content": full_user})

            response = client.chat.completions.create(
                model=model,
                messages=messages,
                tools=ESP_TOOLS_OPENAI,
                tool_choice="auto",
            )
            reasoning, commands = _parse_openai_response(response)

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
            reasoning, commands = _parse_anthropic_response(response)

        elif provider == "google":
            import google.generativeai as genai

            genai.configure(api_key=os.getenv("GOOGLE_API_KEY"))
            model_name = os.getenv("GOOGLE_MODEL", "gemini-2.0-flash")

            model = genai.GenerativeModel(
                model_name=model_name,
                system_instruction=system,
                tools=_google_tools(),
            )
            response = model.generate_content(full_user)
            reasoning, commands = _parse_google_response(response)

        else:
            app_state.add_log(f"Неизвестный LLM_PROVIDER: {provider}", "error")
            return []

    except Exception as exc:
        app_state.add_log(f"Ошибка LLM: {exc}", "error")
        app_state.add_thought(f"Ошибка при обращении к нейросети: {exc}", "error")
        return []

    # Сохраняем контекст диалога
    app_state.llm_history.append({"role": "user", "content": full_user})
    if reasoning:
        app_state.llm_history.append({"role": "assistant", "content": reasoning})
        app_state.add_thought(reasoning, "llm")

    if commands:
        cmd_names = ", ".join(c.get("cmd", "?") for c in commands)
        app_state.add_log(f"Нейросеть сгенерировала команды: {cmd_names}", "success")
    else:
        app_state.add_log("Нейросеть не предложила команд для ESP32.", "info")

    return commands


async def analyze_i2c_change(
    device_id: str,
    new_devices: set[str],
    removed_devices: set[str],
) -> list[dict[str, Any]]:
    """
    Вызвать LLM при изменении состава I2C-шины (hot-plug).

  Пример: появился 0x3C → LLM решит инициализировать OLED.
    """
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
