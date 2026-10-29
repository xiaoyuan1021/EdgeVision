from datetime import datetime

import pytest


def record(number, passed=True, **extra):
    return {"reportId": str(number), "deviceId": "edge", "roiName": "part",
            "passed": passed, "timestamp": number,
            "dateTime": "2020-01-02 03:04:05", **extra}


def test_empty_and_repeated_init(database):
    database.init_db()
    assert database.db_load_devices() == {}
    assert database.db_load_recent_results() == []
    assert database.db_load_total_stats() == {"total": 0, "passed": 0, "failed": 0}
    assert database.db_load_today_stats() == {"total": 0, "passed": 0, "failed": 0}


def test_device_upsert_preserves_first_seen(database):
    database.db_upsert_device("edge", {"first_seen": "first", "uptime": 5})
    database.db_upsert_device("edge", {"first_seen": "second", "status": "offline",
                                      "last_result": {"passed": False}, "result_count": 7})
    devices = database.db_load_devices()
    assert len(devices) == 1
    assert devices["edge"]["first_seen"] == "first"
    assert devices["edge"]["status"] == "offline"
    assert devices["edge"]["last_result"] == {"passed": False}
    assert devices["edge"]["result_count"] == 7


def test_recent_results_order_and_limit(database):
    for i in range(5):
        database.db_insert_result(record(i, i % 2 == 0))
    rows = database.db_load_recent_results(2)
    assert [r["reportId"] for r in rows] == ["3", "4"]
    assert [r["passed"] for r in rows] == [False, True]
    assert database.db_load_recent_results(0) == []


@pytest.mark.parametrize("filters,expected", [
    ({"device_id": "other"}, ["3"]),
    ({"roi_name": "second"}, ["2"]),
    ({"passed": False}, ["2"]),
    ({"device_id": "edge", "passed": True}, ["1"]),
    ({"device_id": "' OR 1=1 --"}, []),
    ({"page": 2, "page_size": 2}, ["1"]),
])
def test_history_filters(database, filters, expected):
    database.db_insert_result(record(1))
    database.db_insert_result(record(2, False, roiName="second"))
    database.db_insert_result(record(3, deviceId="other"))
    result = database.db_query_history(**filters)
    assert [r["reportId"] for r in result["items"]] == expected


def test_aggregates_and_today(database):
    database.db_insert_result(record(1))
    database.db_insert_result(record(2, False, dateTime="2020-01-02T03:30:00"))
    database.db_insert_result(record(3, dateTime=datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
    assert database.db_load_total_stats() == {"total": 3, "passed": 2, "failed": 1}
    assert database.db_load_by_roi()["part"] == {"total": 3, "passed": 2, "failed": 1}
    assert database.db_load_by_hour()["2020-01-02 03:00"] == {"total": 2, "passed": 1, "failed": 1}
    assert database.db_load_today_stats() == {"total": 1, "passed": 1, "failed": 0}
    assert database.db_query_history(days=1)["total"] == 1


def test_heartbeat_persistence(database):
    database.db_insert_heartbeat("edge", "online", 12.5, 12345)
    connection = database.get_db()
    try:
        row = connection.execute("SELECT * FROM heartbeats").fetchone()
        assert (row["device_id"], row["uptime"], row["ts"]) == ("edge", 12.5, 12345)
    finally:
        connection.close()
