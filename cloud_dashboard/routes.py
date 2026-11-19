"""Public HTTP API and event stream. Dependencies belong to the app factory."""
import csv
import io
import json
from contextlib import closing
import logging
import os
import queue
from datetime import datetime
from flask import Blueprint, jsonify, render_template, request, Response, stream_with_context
from werkzeug.exceptions import BadRequest
from http_helpers import query_integer, get_lan_ip, json_object, body_integer

_log = logging.getLogger("dashboard").info


def create_api(runtime):
    api = Blueprint("dashboard", __name__)
    app_state = runtime.state

    def now_ts():
        return int(runtime.clock() * 1000)

    @api.route("/")
    def index():
        return render_template("index.html")


    @api.route("/api/status")
    def api_status():
        with app_state["data_lock"]:
            return jsonify({
                "mqtt_connected": app_state["mqtt_connected"],
                "device_count": len(app_state["devices"]),
                "online_count": sum(1 for d in app_state["devices"].values() if d["status"] == "online"),
                "total_results": app_state["stats"]["total"],
            })


    @api.route("/api/host-info")
    def api_host_info():
        return jsonify({"lanIp": get_lan_ip(), "port": int(os.environ.get("PORT", 5000))})


    @api.route("/api/stats")
    def api_stats():
        with app_state["data_lock"]:
            total = app_state["stats"]["total"]
            passed = app_state["stats"]["passed"]
            failed = app_state["stats"]["failed"]
            device_count = len(app_state["devices"])
            online_count = sum(1 for d in app_state["devices"].values() if d["status"] == "online")
        pass_rate = round(passed / total * 100, 1) if total else 0
        _log(f"[DEBUG] GET /api/stats → total={total}, passed={passed}, failed={failed}, passRate={pass_rate}")
        return jsonify({
            "total": total,
            "passed": passed,
            "failed": failed,
            "passRate": pass_rate,
            "deviceCount": device_count,
            "onlineCount": online_count,
        })


    @api.route("/api/stats/today")
    def api_stats_today():
        with app_state["data_lock"]:
            today_total = app_state["stats"]["today"]
            today_passed = app_state["stats"]["today_passed"]
            today_failed = app_state["stats"]["today_failed"]
            device_count = len(app_state["devices"])
            online_count = sum(1 for d in app_state["devices"].values() if d["status"] == "online")
        pass_rate = round(today_passed / today_total * 100, 1) if today_total else 0
        return jsonify({
            "total": today_total, "passed": today_passed,
            "failed": today_failed, "passRate": pass_rate,
            "deviceCount": device_count, "onlineCount": online_count,
        })


    @api.route("/api/devices")
    def api_devices():
        with app_state["data_lock"]:
            return jsonify(list(app_state["devices"].values()))


    @api.route("/api/results")
    def api_results():
        limit = query_integer("limit", 50, 0)
        with app_state["data_lock"]:
            return jsonify(list(app_state["results_deque"])[-limit:] if limit else [])


    @api.route("/api/results/recent")
    def api_results_recent():
        return api_results()


    @api.route("/api/stats/by-roi")
    def api_stats_by_roi():
        return jsonify(runtime.database.db_load_by_roi())


    @api.route("/api/stats/by-hour")
    def api_stats_by_hour():
        with app_state["data_lock"]:
            has_hour = bool(app_state["stats"]["by_hour"])
        if has_hour:
            with app_state["data_lock"]:
                return jsonify(dict(app_state["stats"]["by_hour"]))
        return jsonify(runtime.database.db_load_by_hour())


    def history_filters():
        passed_str = request.args.get("passed")
        passed = None
        if passed_str is not None:
            if passed_str.lower() not in ("1", "true", "pass", "0", "false", "fail"):
                raise BadRequest("passed must be true or false")
            passed = passed_str.lower() in ("1", "true", "pass")
        return (request.args.get("device"), request.args.get("roi"), passed,
                query_integer("days", None, 0))

    @api.route("/api/results/history")
    def api_results_history():
        filters = history_filters()
        page = query_integer("page", 1, 1)
        page_size = min(query_integer("page_size", 50, 1), 200)
        return jsonify(runtime.database.db_query_history(*filters, page, page_size))

    @api.get("/api/results/history/export")
    def api_export_history():
        filters = history_filters()

        def generate():
            buffer = io.StringIO(newline="")
            writer = csv.writer(buffer)

            def csv_row(values):
                buffer.seek(0)
                buffer.truncate(0)
                writer.writerow(values)
                return buffer.getvalue()

            yield "\ufeff" + csv_row(["时间", "设备", "图片", "ROI", "结果", "检测项通过",
                                      "检测项失败", "失败项", "失败原因", "报告ID", "时间戳(ms)"])
            with closing(runtime.database.db_iter_history(*filters)) as records:
                for item in records:
                    yield csv_row([item["dateTime"], item["deviceId"], item["imageName"], item["roiName"],
                                   "合格" if item["passed"] else "不合格", item["passedItems"],
                                   item["failedItems"], item["failedItemNames"], item["failReason"],
                                   item["reportId"], item["timestamp"]])

        filename = "inspection_history_" + datetime.now().strftime("%Y-%m-%d") + ".csv"
        return Response(stream_with_context(generate()), content_type="text/csv; charset=utf-8",
                        headers={"Content-Disposition": f'attachment; filename="{filename}"',
                                 "Cache-Control": "no-store"})


    @api.route("/api/results/summary")
    def api_results_summary():
        days = request.args.get("days", 7, type=int)
        total_stats = runtime.database.db_load_total_stats()
        pass_rate = round(total_stats["passed"] / total_stats["total"] * 100, 1) if total_stats["total"] else 0
        with app_state["data_lock"]:
            device_count = len(app_state["devices"])
            online_count = sum(1 for d in app_state["devices"].values() if d["status"] == "online")
        return jsonify({
            "total": total_stats["total"], "passed": total_stats["passed"],
            "failed": total_stats["failed"], "passRate": pass_rate,
            "deviceCount": device_count, "onlineCount": online_count,
            "period": f"{days}天" if days else "全部",
        })


    @api.route("/stream")
    def stream():
        def event_stream():
            q = queue.Queue(maxsize=128)
            with app_state["sse_lock"]:
                app_state["sse_clients"].append(q)
            try:
                yield "event: connected\ndata: " + json.dumps({"ok": True}) + "\n\n"
                with app_state["data_lock"]:
                    connected = app_state["mqtt_connected"]
                yield "event: mqtt_status\ndata: " + json.dumps({"connected": connected}) + "\n\n"
                while True:
                    try:
                        event, data = q.get(timeout=30)
                        yield f"event: {event}\ndata: {json.dumps(data)}\n\n"
                    except queue.Empty:
                        yield ": keepalive\n\n"
            except GeneratorExit:
                pass
            finally:
                with app_state["sse_lock"]:
                    if q in app_state["sse_clients"]:
                        app_state["sse_clients"].remove(q)

        return Response(
            stream_with_context(event_stream()),
            mimetype="text/event-stream",
            headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"},
        )


    @api.route("/api/command", methods=["POST"])
    def api_send_command():
        with app_state["data_lock"]:
            client = app_state.get("mqtt_client")
            connected = app_state["mqtt_connected"]
        if client is None or not connected:
            return jsonify({"error": "MQTT 未连接"}), 503
        body = request.get_json(force=True)
        if not isinstance(body, dict):
            return jsonify({"error": "Expected a JSON object"}), 400
        cmd = body.get("cmd", "")
        allowed = {"capture", "start_detection", "stop_detection", "ping"}
        if not isinstance(cmd, str) or cmd not in allowed:
            return jsonify({"error": f"无效指令: {cmd}，允许: {allowed}"}), 400
        payload = json.dumps({"cmd": cmd, "ts": now_ts()})
        client.publish(runtime.mqtt_config["topics"]["commands"], payload, qos=1)
        return jsonify({"ok": True, "cmd": cmd})


    @api.route("/api/simulate", methods=["POST"])
    def api_simulate():
        import random, uuid
        body = json_object()
        count = min(body_integer(body, "count", 1), 20)
        device_ids = ["edge-device-01", "edge-device-02"]
        roi_names = ["PCB-焊点检测", "元件定位", "条码识别", "外观瑕疵"]
        fail_reasons = ["区域面积超出阈值", "圆度不达标", "条码无法解码", "模板匹配失败"]
        for _ in range(count):
            device_id = random.choice(device_ids)
            passed = random.random() < 0.88
            roi_name = random.choice(roi_names)
            payload = {
                "reportId": uuid.uuid4().hex[:16], "deviceId": device_id,
                "imageName": random.choice(["test.jpg", "code.jpg", "sample.png"]),
                "roiName": roi_name, "timestamp": now_ts(),
                "dateTime": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                "passed": passed, "failReason": "" if passed else random.choice(fail_reasons),
                "itemResults": [
                    {"itemName": "Blob分析", "detectionType": "Blob", "passed": passed,
                     "failReason": "" if passed else "Blob数量不足"}
                ],
                "totalItems": 1, "passedItems": 1 if passed else 0,
                "failedItems": 0 if passed else 1, "failedItemNames": "" if passed else "Blob分析",
            }
            runtime.handler._handle_heartbeat({"deviceId": device_id, "status": "online",
                                       "ts": now_ts(), "uptime": random.randint(60, 36000)})
            runtime.handler._handle_result(payload)
        return jsonify({"ok": True, "count": count})


    return api
