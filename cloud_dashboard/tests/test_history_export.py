import csv
import io
from datetime import datetime
from unittest.mock import Mock

import pytest
from flask import request


def record(number, **extra):
    return {"reportId": str(number), "deviceId": "edge", "imageName": f"part-{number}",
            "roiName": "part", "passed": True, "passedItems": 1, "failedItems": 0,
            "timestamp": number, "dateTime": "2026-11-19 12:00:00", **extra}


def csv_rows(response):
    assert response.status_code == 200
    return list(csv.DictReader(io.StringIO(response.data.decode("utf-8-sig"), newline="")))


def test_export_contains_all_matches_beyond_table_limit(client, database):
    for i in range(205):
        database.db_insert_result(record(i))
    page = client.get("/api/results/history?page_size=10000").json
    assert len(page["items"]) == 200
    response = client.get("/api/results/history/export")
    rows = csv_rows(response)
    assert len(rows) == 205
    assert [row["报告ID"] for row in rows] == [str(i) for i in reversed(range(205))]
    assert response.data.startswith(b"\xef\xbb\xbf")
    assert response.content_type == "text/csv; charset=utf-8"
    assert response.headers["Content-Disposition"].startswith("attachment;")
    assert response.headers["Content-Disposition"].endswith('.csv"')
    assert response.headers["Cache-Control"] == "no-store"


def test_export_matches_history_filters_and_inclusive_date_boundary(client, database, monkeypatch):
    import db

    class FrozenDatetime(datetime):
        @classmethod
        def now(cls, tz=None):
            return cls(2026, 11, 19, 12, 0, 0)

    monkeypatch.setattr(db, "datetime", FrozenDatetime)
    database.db_insert_result(record(1, passed=False, dateTime="2026-11-18 12:00:00"))
    database.db_insert_result(record(2, passed=False, dateTime="2026-11-18T12:00:00"))
    database.db_insert_result(record(3, passed=False, dateTime="2026-11-18 11:59:59"))
    database.db_insert_result(record(4, passed=False, dateTime="2026-11-18T11:59:59"))
    database.db_insert_result(record(5, passed=False, roiName="other"))
    database.db_insert_result(record(6, passed=False, deviceId="other"))
    database.db_insert_result(record(7, passed=True))
    filters = {"device": "edge", "roi": "part", "passed": "false", "days": "1"}
    history = client.get("/api/results/history", query_string=filters).json
    exported = csv_rows(client.get("/api/results/history/export", query_string=filters))
    assert [row["reportId"] for row in history["items"]] == ["2", "1"]
    assert [row["报告ID"] for row in exported] == ["2", "1"]
    assert all(row["结果"] == "不合格" for row in exported)
    assert len(csv_rows(client.get("/api/results/history/export?days=0"))) == 7
    assert csv_rows(client.get("/api/results/history/export?device=%27%20OR%201=1--")) == []


def test_export_preserves_chinese_quotes_newlines_and_failure_reason(client, database):
    image = '中文,图片"名称"\r\n第二行'
    reason = '长度,错误"偏差"\n需要复核'
    database.db_insert_result(record(1, passed=False, imageName=image, failReason=reason,
                                     failedItemNames='判定,"条码"', failedItems=2, passedItems=3))
    rows = csv_rows(client.get("/api/results/history/export"))
    assert len(rows) == 1
    assert rows[0]["图片"] == image
    assert rows[0]["失败原因"] == reason
    assert rows[0]["失败项"] == '判定,"条码"'
    assert rows[0]["检测项通过"] == "3"
    assert rows[0]["检测项失败"] == "2"
    assert rows[0]["时间戳(ms)"] == "1"
    assert None not in rows[0]


def test_empty_export_is_a_valid_header_only_csv(client):
    response = client.get("/api/results/history/export")
    assert csv_rows(response) == []
    assert "报告ID" in response.data.decode("utf-8-sig")


@pytest.mark.parametrize("query", ["days=-1", "days=bad", "passed=bad"])
def test_invalid_export_filters_fail_before_streaming(client, database, monkeypatch, query):
    iterate = Mock()
    monkeypatch.setattr(database, "db_iter_history", iterate)
    assert client.get("/api/results/history/export?" + query).status_code == 400
    iterate.assert_not_called()


def test_export_streams_rows_and_releases_connection_when_download_closes(client, database, monkeypatch):
    for i in range(5):
        database.db_insert_result(record(i))
    original = database.get_db
    state = {"read": 0, "closed": False, "cursor_closed": False}

    class TrackedCursor:
        def __init__(self, cursor):
            self.cursor = cursor

        def __iter__(self):
            return self

        def __next__(self):
            row = next(self.cursor)
            state["read"] += 1
            return row

        def close(self):
            self.cursor.close()
            state["cursor_closed"] = True

    class TrackedConnection:
        def __init__(self):
            self.connection = original()

        def execute(self, *args):
            return TrackedCursor(self.connection.execute(*args))

        def close(self):
            self.connection.close()
            state["closed"] = True

    monkeypatch.setattr(database, "get_db", TrackedConnection)
    response = client.get("/api/results/history/export", buffered=False)
    chunks = iter(response.response)
    next(chunks)  # Header does not load records.
    assert state["read"] == 0
    next(chunks)
    assert state["read"] == 1
    response.close()
    assert state["closed"] and state["cursor_closed"]


def test_export_iterator_keeps_one_snapshot_during_new_inserts(database):
    for i in range(3):
        database.db_insert_result(record(i))
    rows = database.db_iter_history()
    try:
        assert next(rows)["reportId"] == "2"
        database.db_insert_result(record(3))
        assert [row["reportId"] for row in rows] == ["1", "0"]
    finally:
        rows.close()


def test_application_access_guard_also_protects_export(api, database, monkeypatch):
    # The stock app has public read APIs; deployments can protect them with an app guard.
    @api.app.before_request
    def require_access():
        if request.headers.get("Authorization") != "Bearer test-access":
            return "unauthorized", 401

    iterate = Mock(wraps=database.db_iter_history)
    monkeypatch.setattr(database, "db_iter_history", iterate)
    with api.app.test_client() as client:
        assert client.get("/api/results/history").status_code == 401
        assert client.get("/api/results/history/export").status_code == 401
        iterate.assert_not_called()
        response = client.get("/api/results/history/export", headers={"Authorization": "Bearer test-access"})
        assert csv_rows(response) == []
        iterate.assert_called_once()
