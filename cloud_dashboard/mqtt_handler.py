"""
MQTT 回调与设备/结果处理
"""

import json
import queue
import time
import threading
from collections import deque
from copy import deepcopy
from datetime import datetime

from config import MQTT_CONFIG, DEVICE_TIMEOUT_SECONDS

import logging
_log = logging.getLogger("dashboard").info


class MQTTHandler:
    def __init__(self, app_state, database, mqtt_config=None):
        self.s = app_state
        self.database = database
        self.config = deepcopy(mqtt_config if mqtt_config is not None else MQTT_CONFIG)
        self._recent_results = set()
        self._recent_order = deque()
        self._recent_lock = threading.Lock()

    def on_connect(self, client, userdata, flags, reason_code, properties=None):
        if reason_code == 0:
            with self.s["data_lock"]:
                self.s["mqtt_connected"] = True
            _log(f"[MQTT] 已连接 {self.config['host']}:{self.config['port']}")
            topics = [
                (self.config["topics"]["results"], self.config["qos"]),
                (self.config["topics"]["heartbeat"], self.config["qos"]),
            ]
            client.subscribe(topics)
            self._broadcast_sse("mqtt_status", {"connected": True})
        else:
            _log(f"[MQTT] 连接失败, reason_code={reason_code}")

    def on_disconnect(self, client, userdata, flags, reason_code, properties=None):
        with self.s["data_lock"]:
            self.s["mqtt_connected"] = False
        _log(f"[MQTT] 断开连接 (reason_code={reason_code})")
        self._broadcast_sse("mqtt_status", {"connected": False})

    def on_message(self, client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return
        if not isinstance(payload, dict):
            return
        if msg.topic == self.config["topics"]["heartbeat"]:
            self._handle_heartbeat(payload)
        elif msg.topic == self.config["topics"]["results"]:
            self._handle_result(payload)

    def _handle_heartbeat(self, payload):
        if not isinstance(payload.get("deviceId", "unknown"), str):
            return
        device_id = payload.get("deviceId", "unknown")
        now = time.time()
        now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        devices = self.s["devices"]

        with self.s["data_lock"]:
            if device_id not in devices:
                devices[device_id] = {
                    "device_id": device_id, "status": "online",
                    "first_seen": now_str, "last_heartbeat": now_str,
                    "last_ts": now, "uptime": payload.get("uptime", 0),
                    "result_count": 0, "last_result": None,
                }
            else:
                dev = devices[device_id]
                dev["status"] = "online"
                dev["last_heartbeat"] = now_str
                dev["last_ts"] = now
                dev["uptime"] = payload.get("uptime", dev["uptime"])

            self.database.db_upsert_device(device_id, devices[device_id])
            self.database.db_insert_heartbeat(device_id, "online", payload.get("uptime", 0),
                                payload.get("ts", int(now * 1000)))
        self._broadcast_sse("heartbeat", {"deviceId": device_id})

    def _handle_result(self, payload):
        # Deduplication and publication must not race a management clear/reset.
        with self.s["data_lock"]:
            self._handle_result_locked(payload)

    def _handle_result_locked(self, payload):
        # Validate before updating deduplication, counters or device state.
        for field in ("reportId", "deviceId", "roiName", "imageName", "dateTime", "failReason"):
            if field in payload and not isinstance(payload[field], str):
                return
        if not isinstance(payload.get("passed", True), bool):
            return
        items = payload.get("itemResults", [])
        if not isinstance(items, list):
            return
        for item in items:
            if (not isinstance(item, dict)
                    or not isinstance(item.get("passed", True), bool)
                    or not isinstance(item.get("itemName", ""), str)):
                return
        report_id = payload.get("reportId", "")
        if report_id:
            with self._recent_lock:
                if report_id in self._recent_results:
                    return
                self._recent_results.add(report_id)
                self._recent_order.append(report_id)
                if len(self._recent_results) > 500:
                    self._recent_results.remove(self._recent_order.popleft())

        device_id = payload.get("deviceId", "unknown")
        passed = payload.get("passed", True)
        roi_name = payload.get("roiName", "全图")
        image_name = payload.get("imageName", "")
        dt_str = payload.get("dateTime", datetime.now().strftime("%Y-%m-%d %H:%M:%S"))

        # ★ 解析检测项结果
        item_results = payload.get("itemResults", [])
        total_items = payload.get("totalItems", len(item_results))
        passed_items = payload.get("passedItems", 0)
        failed_items = payload.get("failedItems", 0)

        # 提取失败的检测项信息
        failed_item_names = []
        for item in item_results:
            if not item.get("passed", True):
                failed_item_names.append(item.get("itemName", "未知"))

        record = {
            "reportId": report_id,
            "deviceId": device_id,
            "imageName": image_name,
            "roiName": roi_name,
            "passed": passed,
            "failReason": payload.get("failReason", ""),
            "totalItems": total_items,
            "passedItems": passed_items,
            "failedItems": failed_items,
            "failedItemNames": ",".join(failed_item_names),
            "itemResults": item_results,  # 完整检测项结果（用于详情展示）
            "timestamp": payload.get("timestamp", int(time.time() * 1000)),
            "dateTime": dt_str,
        }

        today_str = datetime.now().strftime("%Y-%m-%d")
        is_today = dt_str.startswith(today_str)

        with self.s["data_lock"]:
            self.s["results_deque"].append(record)
            devices = self.s["devices"]
            stats = self.s["stats"]

            if device_id not in devices:
                now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                devices[device_id] = {
                    "device_id": device_id, "status": "online",
                    "first_seen": now_str, "last_heartbeat": now_str,
                    "last_ts": time.time(), "uptime": 0,
                    "result_count": 0, "last_result": None,
                }

            stats["total"] += 1
            stats["passed"] += passed
            stats["failed"] += (0 if passed else 1)

            if is_today:
                stats["today"] += 1
                stats["today_passed"] += passed
                stats["today_failed"] += (0 if passed else 1)

            roi_stat = stats["by_roi"][roi_name]
            roi_stat["total"] += 1
            roi_stat["passed"] += passed
            roi_stat["failed"] += (0 if passed else 1)

            hour_key = datetime.now().strftime("%Y-%m-%d %H:00")
            hour_stat = stats["by_hour"][hour_key]
            hour_stat["total"] += 1
            hour_stat["passed"] += passed
            hour_stat["failed"] += (0 if passed else 1)

            if len(stats["by_hour"]) > 168:
                sorted_hours = sorted(stats["by_hour"].keys())
                for old_hour in sorted_hours[:-168]:
                    del stats["by_hour"][old_hour]

            dev = devices[device_id]
            dev["result_count"] = dev.get("result_count", 0) + 1
            dev["last_result"] = {"passed": passed, "roiName": roi_name, "dateTime": dt_str}

            self.database.db_upsert_device(device_id, devices[device_id])
            try:
                self.database.db_insert_result(record)
            except Exception as e:
                _log(f"[MQTT] db_insert_result FAILED: {e}")
        self._broadcast_sse("result", record)

    def expire_devices(self, now=None):
        now = time.time() if now is None else now
        now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        expired = []
        with self.s["data_lock"]:
            for device_id, dev in self.s["devices"].items():
                if dev["status"] == "online" and now - dev.get("last_ts", 0) > DEVICE_TIMEOUT_SECONDS:
                    dev["status"] = "offline"
                    dev["last_heartbeat"] = now_str
                    self.database.db_upsert_device(device_id, dict(dev))
                    expired.append(device_id)
        for device_id in expired:
            self._broadcast_sse("heartbeat", {"deviceId": device_id, "status": "offline"})

    def check_device_timeout(self, stop_event, interval=15):
        while not stop_event.wait(interval):
            try:
                self.expire_devices()
            except Exception:
                logging.getLogger("dashboard").exception("Device timeout sweep failed")

    def clear_recent_results(self):
        with self._recent_lock:
            self._recent_results.clear()
            self._recent_order.clear()

    def _broadcast_sse(self, event, data):
        with self.s["sse_lock"]:
            dead = []
            for q in list(self.s["sse_clients"]):
                try:
                    q.put_nowait((event, data))
                except queue.Full:
                    dead.append(q)
            for q in dead:
                try:
                    self.s["sse_clients"].remove(q)
                except ValueError:
                    pass
