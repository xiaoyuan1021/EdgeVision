"""Route/database integration tests, using only the per-test temporary DB."""
import pytest
from contextlib import closing

pytestmark = pytest.mark.usefixtures("database")


@pytest.mark.parametrize("route,body", [
    ("add-roi", {"name": ""}), ("update-roi", {}), ("add-device", {"deviceId": "  "}),
])
def test_required_names(client, route, body):
    assert client.post("/api/debug/" + route, json=body).status_code == 400


def test_roi_add_delete(client, database, state):
    response = client.post("/api/debug/add-roi", json={"name": "part", "total": 3, "passed": 2})
    assert response.status_code == 200
    expected = {"total": 3, "passed": 2, "failed": 1}
    assert state["stats"]["by_roi"]["part"] == expected
    assert database.db_load_total_stats() == expected
    response = client.post("/api/debug/delete-roi", json={"name": "part"})
    assert response.json == {"ok": True, "removed": expected}
    assert state["stats"]["total"] == 0
    assert database.db_load_total_stats()["total"] == 0
    assert client.post("/api/debug/delete-roi", json={"name": "missing"}).status_code == 200


@pytest.mark.parametrize("new_total,new_passed", [(4, 3), (3, 1)])
def test_roi_rename_updates_totals_once(client, database, state, new_total, new_passed):
    assert client.post("/api/debug/add-roi", json={"name": "old", "total": 3, "passed": 2}).status_code == 200
    response = client.post("/api/debug/update-roi", json={
        "oldName": "old", "newName": "new", "total": new_total, "passed": new_passed})
    assert response.status_code == 200, response.json
    assert "old" not in state["stats"]["by_roi"]
    expected = {"total": new_total, "passed": new_passed, "failed": new_total - new_passed}
    assert database.db_load_total_stats() == expected
    for key, value in expected.items():
        assert state["stats"][key] == value


def test_device_add_update(client, database, state):
    assert client.post("/api/debug/add-device", json={"deviceId": " edge ", "uptime": 10}).status_code == 200
    assert "edge" in database.db_load_devices()
    assert client.post("/api/debug/update-device", json={
        "deviceId": "edge", "status": "offline", "resultCount": 4, "uptime": 20}).status_code == 200
    assert database.db_load_devices()["edge"]["status"] == "offline"
    assert state["devices"]["edge"]["result_count"] == 4


def test_roi_rename_into_existing_roi(client, database, state):
    for name in ("old", "new"):
        assert client.post("/api/debug/add-roi", json={"name": name, "total": 2, "passed": 1}).status_code == 200
    response = client.post("/api/debug/update-roi", json={
        "oldName": "old", "newName": "new", "total": 4, "passed": 3})
    assert response.status_code == 200
    assert database.db_load_total_stats() == {"total": 4, "passed": 3, "failed": 1}
    assert state["stats"]["total"] == 4
    assert state["stats"]["passed"] == 3


def test_clear_data(client, api, database, state):
    api.handler._handle_heartbeat({"deviceId": "edge"})
    api.handler._handle_result({"deviceId": "edge", "reportId": "one"})
    assert client.post("/api/debug/clear-data", json={}).status_code == 200
    assert state["devices"] == {}
    assert list(state["results_deque"]) == []
    assert database.db_load_devices() == {}
    assert database.db_load_total_stats()["total"] == 0
    assert state["stats"]["today"] == 0
    with closing(database.get_db()) as conn:
        assert conn.execute("SELECT count(*) FROM heartbeats").fetchone()[0] == 0


def test_export_limits_to_last_100(client, state):
    state["results_deque"].extend({"reportId": i} for i in range(105))
    data = client.get("/api/debug/export").json
    assert len(data["results"]) == 100
    assert data["results"][0] == {"reportId": 5}


def test_update_stats_reloads_recent_and_today(client, database, state):
    database.db_insert_result({"reportId": "saved", "passed": True})
    state["results_deque"].append({"reportId": "stale"})
    response = client.post("/api/debug/update-stats", json={"total": 10, "passed": 7, "failed": 3})
    assert response.status_code == 200
    assert state["stats"]["total"] == 10
    assert state["stats"]["today"] == 1
    assert [r["reportId"] for r in state["results_deque"]] == ["saved"]


def test_diag_and_summary(client, database):
    database.db_insert_result({"reportId": "one", "passed": False})
    data = client.get("/api/debug/diag").json
    assert data["db_total"] == {"total": 1, "passed": 0, "failed": 1}
    summary = client.get("/api/results/summary").json
    assert summary["total"] == 1
    assert summary["passRate"] == 0


def test_simulation_caps_count(client, api, monkeypatch):
    from unittest.mock import Mock
    fake_handler = Mock()
    monkeypatch.setattr(api, "handler", fake_handler)
    monkeypatch.setattr(api.time, "sleep", lambda _: None)
    response = client.post("/api/simulate", json={"count": 25})
    assert response.json == {"ok": True, "count": 20}
    assert fake_handler._handle_result.call_count == 20
    assert fake_handler._handle_heartbeat.call_count == 20
