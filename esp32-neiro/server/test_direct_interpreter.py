"""Тесты быстрого разбора прямых приказов."""

from direct_interpreter import try_interpret_direct_command


def test_blink_russian():
    result = try_interpret_direct_command("помигай светодиодом 1 гц 5 минут")
    assert result is not None
    cmd = result.commands[0]
    assert cmd["cmd"] == "signal"
    assert cmd["pin"] == 2
    assert cmd["hz"] == 1.0
    assert cmd["duration_ms"] == 300_000
    assert cmd["active_low"] is True
    assert cmd["wave"] == "square"


def test_sine_signal():
    result = try_interpret_direct_command("синус 2 гц на gpio 4")
    assert result is not None
    assert result.commands[0]["wave"] == "sine"
    assert result.commands[0]["pin"] == 4


def test_schedule_on_off():
    result = try_interpret_direct_command("в 13:30 включи светодиод а в 14:50 выключи")
    assert result is not None
    assert len(result.schedules) == 2
    assert result.schedules[0]["commands"][0]["value"] == 0
    assert result.schedules[1]["commands"][0]["value"] == 1


def test_non_blink_returns_none():
    assert try_interpret_direct_command("покажи температуру") is None
