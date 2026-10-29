import sys
import threading
from collections import defaultdict, deque
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


@pytest.fixture
def database(tmp_path, monkeypatch):
    import db
    monkeypatch.setattr(db, "DB_PATH", str(tmp_path / "test.db"))
    db.init_db()
    return db


@pytest.fixture
def state():
    return {
        "devices": {}, "results_deque": deque(maxlen=100),
        "stats": {
            "total": 0, "passed": 0, "failed": 0,
            "today": 0, "today_passed": 0, "today_failed": 0,
            "by_roi": defaultdict(lambda: {"total": 0, "passed": 0, "failed": 0}),
            "by_hour": defaultdict(lambda: {"total": 0, "passed": 0, "failed": 0}),
        },
        "mqtt_connected": False, "sse_clients": [],
        "data_lock": threading.Lock(), "sse_lock": threading.Lock(),
    }


@pytest.fixture
def handler(state, monkeypatch):
    import mqtt_handler
    from unittest.mock import Mock
    for name in ("db_upsert_device", "db_insert_result", "db_insert_heartbeat"):
        monkeypatch.setattr(mqtt_handler, name, Mock())
    return mqtt_handler.MQTTHandler(state)
