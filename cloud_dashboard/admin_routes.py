"""Existing dashboard management endpoints, backed by transactional storage."""
from datetime import datetime
import logging
import os
import time
from flask import Blueprint, jsonify
from werkzeug.exceptions import BadRequest, HTTPException
from http_helpers import json_object, body_integer, required_name


def create_admin_api(runtime):
    api = Blueprint("dashboard_admin", __name__, url_prefix="/api/debug")
    state, database = runtime.state, runtime.database

    @api.errorhandler(Exception)
    def error(exception):
        if isinstance(exception, HTTPException):
            return jsonify(error=exception.description), exception.code
        logging.getLogger("dashboard").exception("Dashboard management operation failed")
        return jsonify(error=str(exception)), 500

    def counts(body):
        total = body_integer(body, "total")
        passed = body_integer(body, "passed")
        if passed > total:
            raise BadRequest("passed cannot exceed total")
        return total, passed

    @api.get("/diag")
    def diagnostics():
        with state["data_lock"]:
            total, today = database.db_load_total_stats(), database.db_load_today_stats()
            return jsonify({
                "memory_stats": {key: state["stats"][key] for key in ("total", "passed", "failed")},
                "db_total": total, "db_today": today, "api_stats_now": total,
                "devices_in_mem": len(state["devices"]), "deque_len": len(state["results_deque"]),
                "by_roi_count": len(state["stats"]["by_roi"]),
                "by_hour_count": len(state["stats"]["by_hour"]),
                "db_path": os.path.abspath(database.path),
            })

    @api.post("/update-stats")
    def update_stats():
        body = json_object()
        values = {key: body_integer(body, key) for key in ("total", "passed", "failed")}
        with state["data_lock"]:
            today = database.db_load_today_stats()
            recent = database.db_load_recent_results(state["results_deque"].maxlen)
            state["stats"].update(values)
            state["stats"].update(today=today["total"], today_passed=today["passed"], today_failed=today["failed"])
            state["results_deque"].clear()
            state["results_deque"].extend(recent)
        return jsonify(ok=True)

    @api.post("/update-roi")
    def update_roi():
        body = json_object()
        old = required_name(body, "oldName")
        new = body.get("newName") or old
        if not isinstance(new, str) or not new.strip():
            raise BadRequest("newName must be a nonempty string")
        total, passed = counts(body)
        with state["data_lock"]:
            database.replace_roi_results(old, new.strip(), total, passed)
            runtime.reload_results_locked()
        return jsonify(ok=True)

    @api.post("/add-roi")
    def add_roi():
        body = json_object()
        name = required_name(body, "name")
        total, passed = counts(body)
        with state["data_lock"]:
            database.add_roi_results(name, total, passed)
            runtime.reload_results_locked()
        return jsonify(ok=True)

    @api.post("/delete-roi")
    def delete_roi():
        name = required_name(json_object(), "name")
        with state["data_lock"]:
            removed = database.delete_roi_results(name)
            runtime.reload_results_locked()
        return jsonify(ok=True, removed=removed)

    @api.post("/update-device")
    @api.post("/add-device")
    def save_device():
        body = json_object()
        device_id = required_name(body, "deviceId")
        count = body_integer(body, "resultCount")
        uptime = body_integer(body, "uptime")
        status = body.get("status", "online")
        if status not in ("online", "offline"):
            raise BadRequest("status must be online or offline")
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        with state["data_lock"]:
            device = dict(state["devices"].get(device_id, {}))
            device.setdefault("first_seen", now)
            device.setdefault("last_result", None)
            device.update(device_id=device_id, status=status, last_heartbeat=now,
                          last_ts=time.time(), uptime=uptime, result_count=count)
            database.db_upsert_device(device_id, device)
            state["devices"][device_id] = device
        runtime.handler._broadcast_sse("heartbeat", {"deviceId": device_id})
        return jsonify(ok=True)

    @api.post("/clear-data")
    def clear_data():
        with state["data_lock"]:
            database.clear_data()
            state["devices"].clear()
            runtime.reload_results_locked()
            runtime.handler.clear_recent_results()
        runtime.handler._broadcast_sse("heartbeat", {"deviceId": "all", "status": "cleared"})
        return jsonify(ok=True)

    @api.get("/export")
    def export():
        with state["data_lock"]:
            return jsonify({
                "stats": {
                    **{key: state["stats"][key] for key in ("total", "passed", "failed")},
                    "by_roi": dict(state["stats"]["by_roi"]), "by_hour": dict(state["stats"]["by_hour"]),
                },
                "devices": list(state["devices"].values()),
                "results": list(state["results_deque"])[-100:],
            })

    return api
