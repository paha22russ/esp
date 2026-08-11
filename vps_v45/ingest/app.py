import os
import json
from datetime import datetime, timezone
from typing import Any, Optional

import psycopg
from fastapi import FastAPI, Header, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

DATABASE_URL = os.environ.get("DATABASE_URL", "postgres://boiler:boiler@db:5432/boiler")
INGEST_TOKEN = os.environ.get("INGEST_TOKEN", "dev-token-change-me")
CORS_ORIGIN = os.environ.get("CORS_ORIGIN", "*")

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
def health():
    return {"ok": True, "service": "boiler-ingest", "ts": datetime.now(timezone.utc).isoformat()}


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
def latest(device_id: str):
    with db() as conn, conn.cursor() as cur:
        cur.execute(
            """SELECT ts, payload FROM telemetry
               WHERE device_id=%s ORDER BY ts DESC LIMIT 1""",
            (device_id,),
        )
        row = cur.fetchone()
        if not row:
            return {"device_id": device_id, "sample": None}
        return {"device_id": device_id, "ts": row[0].isoformat(), "sample": row[1]}


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
def enqueue_cmd(device_id: str, body: CommandBody, authorization: Optional[str] = Header(default=None)):
    auth(authorization)
    with db() as conn, conn.cursor() as cur:
        cur.execute(
            "INSERT INTO commands(device_id, cmd) VALUES(%s, %s::jsonb)",
            (device_id, json.dumps(body.cmd)),
        )
        conn.commit()
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
