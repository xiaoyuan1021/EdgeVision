import json
from unittest.mock import Mock

import pytest


def test_empty_status(client):
    response = client.get("/api/status")
    assert response.status_code == 200
    assert response.json == {"mqtt_connected": False, "device_count": 0,
                             "online_count": 0, "total_results": 0}


@pytest.mark.parametrize("path", ["/api/stats", "/api/stats/today"])
def test_empty_stats(client, path):
    assert client.get(path).json == {"total": 0, "passed": 0, "failed": 0,
                                    "passRate": 0, "deviceCount": 0, "onlineCount": 0}


def test_stats_rounding_and_online_count(client, state):
    state["stats"].update(total=3, passed=2, failed=1)
    state["devices"] = {"a": {"status": "online"}, "b": {"status": "offline"}}
    assert client.get("/api/stats").json == {"total": 3, "passed": 2, "failed": 1,
                                           "passRate": 66.7, "deviceCount": 2, "onlineCount": 1}


@pytest.mark.parametrize("path", ["/api/results", "/api/results/recent"])
def test_recent_limits(client, state, path):
    state["results_deque"].extend([{"reportId": i} for i in range(5)])
    assert client.get(path + "?limit=2").json == [{"reportId": 3}, {"reportId": 4}]


def test_history_parameter_mapping(client, api, monkeypatch):
    query = Mock(return_value={"items": [], "total": 0})
    monkeypatch.setattr(api, "db_query_history", query)
    response = client.get("/api/results/history?device=edge&roi=part&passed=false&days=3&page=2&page_size=999")
    assert response.status_code == 200
    query.assert_called_once_with("edge", "part", False, 3, 2, 200)


def test_command_disconnected(client):
    assert client.post("/api/command", json={"cmd": "capture"}).status_code == 503


@pytest.mark.parametrize("command", ["capture", "start_detection", "stop_detection", "ping"])
def test_command_publish(client, api, state, monkeypatch, command):
    mqtt = Mock()
    state.update(mqtt_connected=True, mqtt_client=mqtt)
    monkeypatch.setattr(api, "now_ts", lambda: 12345)
    response = client.post("/api/command", json={"cmd": command})
    assert response.status_code == 200
    assert response.json == {"ok": True, "cmd": command}
    args, kwargs = mqtt.publish.call_args
    assert args[0] == api.MQTT_CONFIG["topics"]["commands"]
    assert json.loads(args[1]) == {"cmd": command, "ts": 12345}
    assert kwargs == {"qos": 1}


def test_invalid_command_does_not_publish(client, state):
    mqtt = Mock()
    state.update(mqtt_connected=True, mqtt_client=mqtt)
    assert client.post("/api/command", json={"cmd": "delete"}).status_code == 400
    mqtt.publish.assert_not_called()


def test_sse_connection_and_cleanup(client, state):
    response = client.get("/stream", buffered=False)
    try:
        chunks = iter(response.response)
        assert b"event: connected" in next(chunks)
        assert b"event: mqtt_status" in next(chunks)
        assert len(state["sse_clients"]) == 1
        state["sse_clients"][0].put(("result", {"passed": True}))
        assert b"event: result" in next(chunks)
    finally:
        response.close()
    assert state["sse_clients"] == []


@pytest.mark.parametrize("path", ["/api/results", "/api/results/recent"])
def test_zero_limit_is_empty(client, state, path):
    state["results_deque"].append({"reportId": "one"})
    assert client.get(path + "?limit=0").json == []


@pytest.mark.parametrize("query", ["limit=-1", "limit=invalid"])
@pytest.mark.parametrize("path", ["/api/results", "/api/results/recent"])
def test_invalid_limit(client, path, query):
    assert client.get(path + "?" + query).status_code == 400


@pytest.mark.parametrize("query", ["page=0", "page=-1", "page_size=0", "page_size=-1",
                                  "days=-1", "page=bad", "passed=bad"])
def test_invalid_history_parameters(client, api, monkeypatch, query):
    query_db = Mock()
    monkeypatch.setattr(api, "db_query_history", query_db)
    assert client.get("/api/results/history?" + query).status_code == 400
    query_db.assert_not_called()


@pytest.mark.parametrize("body", [None, [], 42, {"cmd": []}, {"cmd": {}}])
def test_command_requires_object_and_string(client, state, body):
    mqtt = Mock()
    state.update(mqtt_connected=True, mqtt_client=mqtt)
    response = client.post("/api/command", data=json.dumps(body), content_type="application/json")
    assert response.status_code == 400
    mqtt.publish.assert_not_called()
