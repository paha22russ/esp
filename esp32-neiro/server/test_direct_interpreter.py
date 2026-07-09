"""Тесты быстрого разбора прямых приказов."""

from direct_interpreter import try_interpret_direct_command


def test_blink_russian():
    cmds = try_interpret_direct_command("помигай светодиодом 1 гц 5 минут")
    assert cmds is not None
    assert cmds[0]["cmd"] == "blink"
    assert cmds[0]["pin"] == 2
    assert cmds[0]["hz"] == 1.0
    assert cmds[0]["duration_ms"] == 300_000
    assert cmds[0]["active_low"] is True


def test_non_blink_returns_none():
    assert try_interpret_direct_command("покажи температуру") is None
