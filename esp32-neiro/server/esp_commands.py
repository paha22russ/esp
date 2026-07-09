"""Конвертация инструментов LLM → JSON-команды ESP32."""

from __future__ import annotations

from typing import Any


def _pin_defaults(pin: int) -> dict[str, Any]:
    return {"active_low": pin == 2}


def tool_to_esp_commands(name: str, args: dict[str, Any]) -> list[dict[str, Any]]:
    """Преобразовать один tool call в команды прошивки."""
    if name == "set_pin_mode":
        return [{"cmd": "pin_mode", "pin": args["pin"], "mode": args["mode"]}]

    if name == "digital_write_pin":
        pin = args["pin"]
        return [{"cmd": "digital_write", "pin": pin, "value": args["value"]}]

    if name in ("generate_pin_signal", "blink_led"):
        pin = int(args.get("pin", 2))
        duration_sec = int(args.get("duration_sec", args.get("duration", 60)))
        cmd = {
            "cmd": "signal",
            "pin": pin,
            "wave": args.get("wave", "square"),
            "hz": float(args.get("hz", 1)),
            "duration_ms": duration_sec * 1000,
            "duty": int(args.get("duty", 100)),
            **_pin_defaults(pin),
        }
        if "active_low" in args:
            cmd["active_low"] = bool(args["active_low"])
        return [cmd]

    if name == "set_pwm_output":
        pin = int(args.get("pin", 2))
        cmd = {
            "cmd": "pwm",
            "pin": pin,
            "hz": float(args.get("hz", 1000)),
            "duty": int(args.get("duty", 50)),
            **_pin_defaults(pin),
        }
        if "active_low" in args:
            cmd["active_low"] = bool(args["active_low"])
        return [cmd]

    if name == "stop_pin":
        return [{"cmd": "stop", "pin": int(args.get("pin", 255))}]

    if name == "run_pin_sequence":
        steps = []
        for step in args.get("steps", []):
            action = step.get("action") or step
            inner_name = action.get("tool") or action.get("name")
            delay = int(step.get("delay_ms", action.get("delay_ms", 0)))
            if inner_name:
                inner_cmds = tool_to_esp_commands(inner_name, action.get("arguments") or action)
                if inner_cmds:
                    steps.append({"delay_ms": delay, **inner_cmds[0]})
            elif "cmd" in action:
                payload = {k: v for k, v in action.items() if k != "delay_ms"}
                steps.append({"delay_ms": delay, **payload})
            elif "wave" in action or "hz" in action:
                inner_cmds = tool_to_esp_commands("generate_pin_signal", action)
                if inner_cmds:
                    steps.append({"delay_ms": delay, **inner_cmds[0]})
        return [{"cmd": "sequence", "steps": steps}] if steps else []

    if name == "init_i2c_display":
        addr = str(args["address"])
        if not addr.startswith("0x"):
            addr = f"0x{int(addr):02X}" if addr.isdigit() else addr
        return [{"cmd": "init_display", "address": addr}]

    if name == "print_on_display":
        return [{"cmd": "print_text", "text": args.get("text", ""), "line": args.get("line", 0)}]

    if name == "reboot_esp32":
        return [{"cmd": "reboot"}]

    return []


def schedule_tool_to_commands(name: str, args: dict[str, Any]) -> list[dict[str, Any]]:
    """Команды для отложенного выполнения (schedule_gpio_at_time)."""
    action = args.get("action", "digital_write")
    pin = int(args.get("pin", 2))

    if action == "digital_write":
        return [{"cmd": "digital_write", "pin": pin, "value": int(args.get("value", 0))}]
    if action == "signal":
        return tool_to_esp_commands(
            "generate_pin_signal",
            {
                "pin": pin,
                "wave": args.get("wave", "square"),
                "hz": args.get("hz", 1),
                "duration_sec": args.get("duration_sec", 60),
                "duty": args.get("duty", 100),
            },
        )
    if action == "stop":
        return [{"cmd": "stop", "pin": pin}]
    return tool_to_esp_commands(action, args)
