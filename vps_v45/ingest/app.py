import os
import json
from datetime import datetime, timezone
from typing import Any, Optional

import psycopg
from fastapi import FastAPI, Header, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

DATABASE_URL = os.environ.get("DATABASE_URL", "postgres://boiler:boiler@db:5432/boiler")
INGEST_TOKEN = os.environ.get("INGEST_TOKEN", "dev-token-change-me")
CORS_ORIGIN = os.environ.get("CORS_ORIGIN", "*")
DEFAULT_DEVICE_ID = os.environ.get("DEFAULT_DEVICE_ID", "esp32-boiler-1")

app = FastAPI(title="Boiler ingest 4.5-beta")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"] if CORS_ORIGIN == "*" else [CORS_ORIGIN],
    allow_methods=["*"],
    allow_headers=["*"],
)


def db():
    return psycopg.connect(DATABASE_URL)


def auth(authorization: Optional[str]):
    if not authorization or not authorization.startswith("Bearer "):
        raise HTTPException(401, "missing bearer token")
    token = authorization.split(" ", 1)[1].strip()
    if token != INGEST_TOKEN:
        raise HTTPException(403, "bad token")


def latest_sample(device_id: str) -> tuple[Optional[dict], Optional[str]]:
    with db() as conn, conn.cursor() as cur:
        cur.execute(
            """SELECT ts, payload FROM telemetry
               WHERE device_id=%s ORDER BY ts DESC LIMIT 1""",
            (device_id,),
        )
        row = cur.fetchone()
        if not row:
            return None, None
        return row[1], row[0].isoformat()


def mode_to_int(mode: Any) -> int:
    if isinstance(mode, int):
        return mode
    if mode == "comfort":
        return 1
    if mode == "neuro":
        return 2
    return 0


def mode_to_ru(mode_i: int) -> str:
    return {0: "Авто", 1: "Комфорт", 2: "Нейро"}.get(mode_i, "Авто")


def sample_to_status(sample: Optional[dict], ts: Optional[str]) -> dict:
    s = sample or {}
    t = s.get("temps") or {}
    a = s.get("actuators") or {}
    sp = s.get("setpoints") or {}
    mode_i = mode_to_int(s.get("mode", 0))
    fan_on = bool(a.get("fan_relay")) or int(a.get("fan_power_pct") or 0) > 0
    pump_on = bool(a.get("pump"))
    supply = t.get("supply")
    ret = t.get("return")

    return {
        "supplyTemp": supply,
        "returnTemp": ret,
        "boilerTemp": t.get("boiler_room"),
        "outdoorTemp": t.get("outdoor"),
        "homeTemp": t.get("home"),
        "flueTemp": t.get("flue"),
        "hysteresis": 2.0,
        "setpoint": sp.get("auto_max", 60),
        "fan": fan_on,
        "fanPowerPct": a.get("fan_power_pct", 0),
        "pump": pump_on,
        "systemEnabled": s.get("system_enabled", True),
        "state": s.get("state") or "IDLE",
        "workMode": mode_i,
        "workModeName": mode_to_ru(mode_i),
        "mode": s.get("mode") or "auto",
        "modeRu": mode_to_ru(mode_i),
        "homeTempSensorValid": t.get("home") is not None,
        "homeTempSensorLWTOnline": t.get("home") is not None,
        "targetHomeTemp": sp.get("comfort_room", 22),
        "firmwareVersion": s.get("fw") or "4.5.0-beta",
        "version": s.get("fw") or "4.5.0-beta",
        "channel": s.get("channel") or "beta",
        "wifiStatus": "VPS mirror",
        "wifiRSSI": 0,
        "mqttStatus": "VPS",
        "coalFeeding": False,
        "coalFeedingRemaining": 0,
        "lowReturnTemp": bool(pump_on and ret is not None and 0 < float(ret) < 40),
        "coalBurned": False,
        "boilerExtinguished": False,
        "ignitionInProgress": False,
        "safetyTrip": s.get("safety_trip", False),
        "safetyReason": s.get("safety_reason") or "",
        "sensorPowerOn": a.get("sensor_power", True),
        "autoMin": sp.get("auto_min", 55),
        "autoMax": sp.get("auto_max", 70),
        "comfortRoom": sp.get("comfort_room", 22),
        "uptime": 0,
        "freeHeap": 0,
        "minFreeHeap": 0,
        "vpsTs": ts,
        "fanStats": {
            "totalWorkTime": 0,
            "dailyWorkTime": 0,
            "cycleCount": 0,
            "dailyCycleCount": 0,
            "currentWorkTime": 0,
        },
    }


def enqueue_cmd(device_id: str, cmd: dict):
    with db() as conn, conn.cursor() as cur:
        cur.execute(
            "INSERT INTO commands(device_id, cmd) VALUES(%s, %s::jsonb)",
            (device_id, json.dumps(cmd)),
        )
        conn.commit()


class IngestBody(BaseModel):
    type: str
    device_id: str = "esp32-boiler-1"
    sample: Optional[dict[str, Any]] = None
    ts: Optional[int] = None
    event: Optional[str] = None
    text: Optional[str] = None
    audio_url: Optional[str] = None
    snapshot: Optional[dict[str, Any]] = None


class CommandBody(BaseModel):
    device_id: str = "esp32-boiler-1"
    cmd: dict[str, Any] = Field(default_factory=dict)


@app.get("/api/v1/health")
@app.get("/api/health")
def health():
    return {"ok": True, "service": "boiler-ingest", "ts": datetime.now(timezone.utc).isoformat()}


@app.get("/api/status")
def api_status(device_id: str = DEFAULT_DEVICE_ID):
    sample, ts = latest_sample(device_id)
    return sample_to_status(sample, ts)


@app.get("/api/system/mode")
def api_mode_get(device_id: str = DEFAULT_DEVICE_ID):
    sample, _ = latest_sample(device_id)
    mode_i = mode_to_int((sample or {}).get("mode", 0))
    return {"mode": mode_i, "modeName": mode_to_ru(mode_i), "success": True}


@app.post("/api/system/mode")
async def api_mode_post(request: Request, device_id: str = DEFAULT_DEVICE_ID):
    try:
        body = await request.json()
    except Exception:
        body = {}
    mode = body.get("mode", 0)
    if isinstance(mode, str) and mode.isdigit():
        mode = int(mode)
    if isinstance(mode, int):
        name = {0: "auto", 1: "comfort", 2: "neuro"}.get(mode, "auto")
        mode_i = mode
    else:
        name = str(mode)
        mode_i = mode_to_int(name)
    enqueue_cmd(device_id, {"type": "set_mode", "mode": name})
    return {"success": True, "mode": mode_i, "modeName": mode_to_ru(mode_i), "queued": True}


@app.post("/api/system/enable")
async def api_enable(request: Request, enabled: Optional[str] = None, device_id: str = DEFAULT_DEVICE_ID):
    en = False
    if enabled is not None:
        en = enabled in ("1", "true", "True")
    else:
        try:
            body = await request.json()
            en = bool(body.get("enabled"))
        except Exception:
            en = False
    enqueue_cmd(device_id, {"type": "set_enabled", "enabled": en})
    return {"success": True, "queued": True}


@app.post("/api/v1/ingest")
def ingest(body: IngestBody, authorization: Optional[str] = Header(default=None)):
    auth(authorization)
    with db() as conn, conn.cursor() as cur:
        cur.execute(
            "INSERT INTO devices(id, name) VALUES(%s,%s) ON CONFLICT (id) DO NOTHING",
            (body.device_id, body.device_id),
        )
        if body.type == "telemetry":
            payload = body.sample or body.model_dump()
            cur.execute(
                "INSERT INTO telemetry(device_id, payload) VALUES(%s, %s::jsonb)",
                (body.device_id, json.dumps(payload)),
            )
        elif body.type == "user_event":
            cur.execute(
                """INSERT INTO user_events(device_id, event, text, audio_uri, snapshot)
                   VALUES(%s,%s,%s,%s,%s::jsonb)""",
                (
                    body.device_id,
                    body.event or "other",
                    body.text,
                    body.audio_url,
                    json.dumps(body.snapshot) if body.snapshot else None,
                ),
            )
        else:
            raise HTTPException(400, f"unknown type {body.type}")
        conn.commit()
    return {"ok": True}


@app.get("/api/v1/devices/{device_id}/latest")
@app.get("/api/latest")
def latest(device_id: str = DEFAULT_DEVICE_ID):
    sample, ts = latest_sample(device_id)
    if sample is None:
        return {"device_id": device_id, "sample": None}
    return {"device_id": device_id, "ts": ts, "sample": sample}


@app.get("/api/v1/devices/{device_id}/events")
def events(device_id: str, limit: int = 50):
    with db() as conn, conn.cursor() as cur:
        cur.execute(
            """SELECT ts, event, text FROM user_events
               WHERE device_id=%s ORDER BY ts DESC LIMIT %s""",
            (device_id, min(limit, 200)),
        )
        rows = cur.fetchall()
    return {
        "device_id": device_id,
        "events": [{"ts": r[0].isoformat(), "event": r[1], "text": r[2]} for r in rows],
    }


@app.post("/api/v1/devices/{device_id}/commands")
def enqueue_cmd_api(device_id: str, body: CommandBody, authorization: Optional[str] = Header(default=None)):
    auth(authorization)
    enqueue_cmd(device_id, body.cmd)
    return {"ok": True}


@app.get("/api/v1/devices/{device_id}/commands/next")
def next_cmd(device_id: str, authorization: Optional[str] = Header(default=None)):
    """ESP забирает команду (для управления через публичный веб без прямого доступа к ESP)."""
    auth(authorization)
    with db() as conn, conn.cursor() as cur:
        cur.execute(
            """SELECT id, cmd FROM commands
               WHERE device_id=%s AND consumed=FALSE
               ORDER BY id ASC LIMIT 1 FOR UPDATE SKIP LOCKED""",
            (device_id,),
        )
        row = cur.fetchone()
        if not row:
            return {"cmd": None}
        cur.execute("UPDATE commands SET consumed=TRUE WHERE id=%s", (row[0],))
        conn.commit()
        return {"id": row[0], "cmd": row[1]}


# --- Soft stubs so the 4.2 UI does not break on VPS ---
def _ok():
    return {"success": True, "vps": True}


@app.api_route("/api/settings/{name}", methods=["GET", "POST"])
@app.api_route("/api/wifi/{name:path}", methods=["GET", "POST"])
@app.api_route("/api/sensors/{name:path}", methods=["GET", "POST"])
@app.api_route("/api/ntp/{name:path}", methods=["GET", "POST"])
@app.api_route("/api/ml/{name:path}", methods=["GET", "POST"])
@app.api_route("/api/tunnel/{name:path}", methods=["GET", "POST"])
@app.api_route("/api/update/{name:path}", methods=["GET", "POST"])
@app.api_route("/api/coalFeeding", methods=["GET", "POST"])
@app.api_route("/api/control", methods=["GET", "POST"])
@app.api_route("/api/mqtt/test", methods=["POST"])
@app.get("/api/system/info")
@app.get("/api/system/log")
@app.post("/api/system/bootcount/reset")
@app.post("/api/system/reboot")
@app.get("/api/pins")
@app.get("/api/journal")
@app.get("/api/hypotheses")
async def ui_stub(request: Request, name: str = ""):
    path = request.url.path
    if path.endswith("/info"):
        return {
            "version": "4.5.0-beta",
            "ip": "vps",
            "mac": "--",
            "uptime": "mirror",
            "freeMem": "--",
            "time": "--:--:--",
            "date": "--.--.----",
        }
    if path.endswith("/log"):
        return {"currentBootCount": 0, "entries": []}
    if "/update/check" in path:
        return {
            "currentVersion": "4.5.0-beta",
            "latestVersion": "4.5.0-beta",
            "updateAvailable": False,
            "channel": "v45-beta",
        }
    if path.endswith("/coalFeeding"):
        return {"active": False, "coalFeeding": False, "remaining": 0}
    if "/settings/auto" in path and request.method == "GET":
        return {"setpoint": 60, "hysteresis": 2}
    if "/settings/comfort" in path and request.method == "GET":
        return {
            "targetHomeTemp": 22,
            "minBoilerTemp": 50,
            "maxBoilerTemp": 75,
            "waitTemp": 45,
            "hysteresisOn": 0.3,
            "hysteresisOff": 0.3,
            "hysteresisBoiler": 2,
        }
    if "/settings/mqtt" in path and request.method == "GET":
        return {"enabled": False, "server": "", "port": 1883, "prefix": "boiler"}
    if "/settings/relay" in path and request.method == "GET":
        return {"fanActiveHigh": True, "pumpActiveHigh": True}
    if "/wifi/info" in path:
        return {"mode": "VPS", "ssid": "mirror", "ip": "vps", "rssi": 0, "status": "mirror"}
    if "/ntp/time" in path:
        return {"time": "--:--:--", "date": "--.--.----", "synced": False}
    if "/ntp/settings" in path and request.method == "GET":
        return {"enabled": True, "server": "pool.ntp.org", "timezone": 3, "updateInterval": 3600}
    if "/ml/settings" in path and request.method == "GET":
        return {"enabled": False, "observeOnly": True, "hypotheses": 0}
    if "/tunnel/" in path and request.method == "GET":
        return {"enabled": False}
    if "/sensors/mapping" in path and request.method == "GET":
        return {
            "supply": "pt1000_cs27",
            "flue": "pt1000_cs26",
            "return": "ow1",
            "boiler": "ow2_a",
            "outdoor": "ow2_b",
            "home": "mqtt",
        }
    if "/sensors/scan" in path:
        return {"success": True, "sensors": []}
    if path.endswith("/journal"):
        return {"entries": []}
    if path.endswith("/hypotheses"):
        return {"items": []}
    if path.endswith("/pins"):
        return {"version": "4.5.0-beta", "note": "VPS mirror"}
    return _ok()
