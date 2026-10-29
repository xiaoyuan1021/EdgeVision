import json
import queue
from types import SimpleNamespace
from unittest.mock import Mock

import pytest
import mqtt_handler


def test_connect_and_disconnect(handler, state):
    client = Mock()
    handler.on_connect(client, None, None, 0)
    assert state["mqtt_connected"] is True
    client.subscribe.assert_called_once()
    handler.on_disconnect(client, None, None, 0)
    assert state["mqtt_connected"] is False


def test_failed_connect(handler, state):
    client = Mock()
    handler.on_connect(client, None, None, 5)
    assert state["mqtt_connected"] is False
    client.subscribe.assert_not_called()


def test_heartbeat_reconnect_preserves_results(handler, state):
    handler._handle_heartbeat({"deviceId": "edge", "uptime": 10})
    state["devices"]["edge"].update(status="offline", result_count=4)
    handler._handle_heartbeat({"deviceId": "edge", "uptime": 20})
    assert state["devices"]["edge"]["status"] == "online"
    assert state["devices"]["edge"]["result_count"] == 4
    assert state["devices"]["edge"]["uptime"] == 20
    assert mqtt_handler.db_insert_heartbeat.call_count == 2


@pytest.mark.parametrize("passed", [True, False])
def test_result_stats_and_duplicate(handler, state, passed):
    payload = {"reportId": "one", "deviceId": "edge", "passed": passed,
               "roiName": "part", "itemResults": [{"itemName": "code", "passed": False}]}
    handler._handle_result(payload)
    handler._handle_result(payload)
    assert len(state["results_deque"]) == 1
    assert state["stats"]["total"] == 1
    assert state["stats"]["passed"] == int(passed)
    assert state["stats"]["failed"] == int(not passed)
    assert state["stats"]["today"] == 1
    assert state["stats"]["by_roi"]["part"]["total"] == 1
    assert state["results_deque"][0]["failedItemNames"] == "code"
    mqtt_handler.db_insert_result.assert_called_once()


def test_historical_result_not_counted_today(handler, state):
    handler._handle_result({"dateTime": "2000-01-01 00:00:00"})
    assert state["stats"]["total"] == 1
    assert state["stats"]["today"] == 0


def test_sse_removes_full_queue(handler, state):
    full = queue.Queue(maxsize=1); full.put("occupied")
    available = queue.Queue()
    state["sse_clients"] = [full, available]
    handler._broadcast_sse("result", {"passed": True})
    assert state["sse_clients"] == [available]
    assert available.get_nowait() == ("result", {"passed": True})


def test_invalid_json_ignored(handler, state):
    handler.on_message(None, None, SimpleNamespace(payload=b"{invalid", topic="anything"))
    assert state["devices"] == {}
    mqtt_handler.db_insert_result.assert_not_called()


def test_message_dispatch(handler):
    handler._handle_heartbeat = Mock()
    payload = {"deviceId": "edge"}
    handler.on_message(None, None, SimpleNamespace(
        payload=json.dumps(payload).encode(), topic=mqtt_handler.MQTT_CONFIG["topics"]["heartbeat"]))
    handler._handle_heartbeat.assert_called_once_with(payload)
