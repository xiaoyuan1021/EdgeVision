"""Production app wiring, lifecycle, and transaction boundaries without a broker."""
from copy import deepcopy
import importlib
import threading
from unittest.mock import Mock

import pytest

from app import create_app
from db import Database
from runtime import DashboardRuntime


def test_import_has_no_side_effects(monkeypatch):
    import app
    init = Mock(side_effect=AssertionError("import opened database"))
    start = Mock(side_effect=AssertionError("import started thread"))
    monkeypatch.setattr(Database, "init_db", init)
    monkeypatch.setattr(threading.Thread, "start", start)
    importlib.reload(app)
    init.assert_not_called()
    start.assert_not_called()


def test_two_apps_are_isolated_and_restore_persisted_state(tmp_path):
    first = DashboardRuntime(database=Database(tmp_path / "a" / "data.db"))
    second = DashboardRuntime(database=Database(tmp_path / "b" / "data.db"))
    client_a = create_app({"TESTING": True}, runtime=first).test_client()
    client_b = create_app({"TESTING": True}, runtime=second).test_client()
    first.handler._handle_heartbeat({"deviceId": "edge"})
    first.handler._handle_result({"deviceId": "edge", "reportId": "a", "passed": True})
    assert client_a.get("/api/stats").json["total"] == 1
    assert client_b.get("/api/stats").json["total"] == 0
    assert client_b.get("/api/devices").json == []
    assert first.mqtt_config["client_id"] != second.mqtt_config["client_id"]
    first.mqtt_config["topics"]["results"] = "changed"
    assert second.mqtt_config["topics"]["results"] == "visiontool/results"
    restored = DashboardRuntime(database=Database(first.database.path))
    client = create_app({"TESTING": True}, runtime=restored).test_client()
    assert client.get("/api/stats").json["total"] == 1
    assert client.get("/api/results").json[0]["reportId"] == "a"
    assert client.get("/api/devices").json[0]["device_id"] == "edge"


def test_start_stop_restart_are_idempotent(database):
    clients = [Mock(), Mock()]
    factory = Mock(side_effect=clients)
    runtime = DashboardRuntime(database=database, client_factory=factory)
    try:
        runtime.start()
        monitor = runtime._monitor
        runtime.start()
        assert factory.call_count == 1
        assert runtime.running and monitor.is_alive()
        clients[0].connect_async.assert_called_once()
        clients[0].loop_start.assert_called_once()
        runtime.stop()
        runtime.stop()
        assert not monitor.is_alive()
        assert not runtime.running
        assert "mqtt_client" not in runtime.state
        clients[0].disconnect.assert_called_once()
        clients[0].loop_stop.assert_called_once()
        runtime.start()
        assert runtime.running and runtime._monitor.is_alive()
        assert runtime.state["mqtt_client"] is clients[1]
    finally:
        runtime.stop()
    clients[1].loop_stop.assert_called_once()


def test_failed_start_can_retry_without_stale_client(database):
    bad, good = Mock(), Mock()
    bad.connect_async.side_effect = OSError("connection setup failed")
    runtime = DashboardRuntime(database=database, client_factory=Mock(side_effect=[bad, good]))
    with pytest.raises(OSError, match="connection setup"):
        runtime.start()
    assert not runtime.running
    assert "mqtt_client" not in runtime.state
    bad.loop_stop.assert_called_once()
    try:
        runtime.start()
        assert runtime.running
    finally:
        runtime.stop()


def test_cache_reload_failure_keeps_previous_snapshot(api, state, database, monkeypatch):
    database.db_insert_result({"reportId": "new", "passed": True})
    before = deepcopy(state["stats"])
    monkeypatch.setattr(database, "db_load_by_hour", Mock(side_effect=OSError("read failed")))
    with state["data_lock"], pytest.raises(OSError):
        api.runtime.reload_results_locked()
    assert state["stats"] == before
    assert list(state["results_deque"]) == []


def test_roi_transaction_rolls_back_partial_insert(client, database, state, monkeypatch):
    assert client.post("/api/debug/add-roi", json={"name": "old", "total": 2, "passed": 1}).status_code == 200
    before = deepcopy(state["stats"])
    recent = list(state["results_deque"])
    insert = database._insert_debug_results

    def fail_after_insert(conn, name, total, passed):
        insert(conn, name, total, passed)
        raise OSError("disk failure")

    monkeypatch.setattr(database, "_insert_debug_results", fail_after_insert)
    response = client.post("/api/debug/update-roi", json={
        "oldName": "old", "newName": "new", "total": 4, "passed": 3})
    assert response.status_code == 500
    assert database.db_load_by_roi() == {"old": {"total": 2, "passed": 1, "failed": 1}}
    assert state["stats"] == before
    assert list(state["results_deque"]) == recent


def test_failed_device_save_does_not_publish_to_memory(client, database, state, monkeypatch):
    monkeypatch.setattr(database, "db_upsert_device", Mock(side_effect=OSError("disk failure")))
    assert client.post("/api/debug/add-device", json={"deviceId": "edge"}).status_code == 500
    assert state["devices"] == {}


@pytest.mark.parametrize("body", [[], {"name": "r", "total": -1},
    {"name": "r", "total": True}, {"name": "r", "total": 1, "passed": 2},
    {"name": "r", "total": "bad"}])
def test_invalid_management_data_never_changes_storage(client, database, state, body):
    assert client.post("/api/debug/add-roi", json=body).status_code == 400
    assert state["stats"]["total"] == database.db_load_total_stats()["total"] == 0


def test_clear_also_clears_deduplication(client, api):
    record = {"deviceId": "edge", "reportId": "reused", "passed": True}
    api.runtime.handler._handle_result(record)
    assert client.post("/api/debug/clear-data", json={}).status_code == 200
    api.runtime.handler._handle_result(record)
    assert client.get("/api/stats").json["total"] == 1
    assert client.get("/api/results").json[0]["reportId"] == "reused"
