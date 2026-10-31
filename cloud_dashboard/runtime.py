"""Explicit ownership and lifecycle for one dashboard's background services."""
from copy import deepcopy
from pathlib import Path
import threading
import time
import uuid

import paho.mqtt.client as mqtt

from config import DB_PATH, MAX_RESULTS, MQTT_CONFIG
from db import Database
from mqtt_handler import MQTTHandler
from state import new_state


class DashboardRuntime:
    def __init__(self, database=None, state=None, mqtt_config=None, client_factory=None):
        self.database = database if database is not None else Database(DB_PATH)
        self.state = state if state is not None else new_state(MAX_RESULTS)
        self.mqtt_config = deepcopy(mqtt_config if mqtt_config is not None else MQTT_CONFIG)
        if mqtt_config is None:
            self.mqtt_config["client_id"] = f"cloud-dashboard-{uuid.uuid4().hex[:8]}"
        self.handler = MQTTHandler(self.state, self.database, self.mqtt_config)
        self.clock = time.time
        self._client_factory = client_factory or mqtt.Client
        self._lifecycle_lock = threading.RLock()
        self._stop_event = threading.Event()
        self._monitor = None
        self._client = None
        self._initialized = False
        self.running = False

    def initialize(self):
        with self._lifecycle_lock:
            if self._initialized:
                return
            Path(self.database.path).parent.mkdir(parents=True, exist_ok=True)
            self.database.init_db()
            with self.state["data_lock"]:
                devices = self.database.db_load_devices()
                self.reload_results_locked()
                self.state["devices"].update(devices)
            self._initialized = True

    def reload_results_locked(self):
        """Caller holds data_lock so MQTT cannot interleave with cache replacement."""
        # Finish all reads before publishing anything: a failed read must not
        # leave a mixture of old and new counters in the HTTP cache.
        totals = self.database.db_load_total_stats()
        today = self.database.db_load_today_stats()
        by_roi = self.database.db_load_by_roi()
        by_hour = self.database.db_load_by_hour()
        results = self.state["results_deque"]
        recent = self.database.db_load_recent_results(results.maxlen)
        stats = self.state["stats"]
        stats.update(totals)
        stats.update(today=today["total"], today_passed=today["passed"], today_failed=today["failed"])
        stats["by_roi"].clear()
        stats["by_roi"].update(by_roi)
        stats["by_hour"].clear()
        stats["by_hour"].update(by_hour)
        results.clear()
        results.extend(recent)

    def start(self):
        with self._lifecycle_lock:
            if self.running:
                return
            self.initialize()
            cfg = self.mqtt_config
            client = self._client_factory(
                client_id=cfg["client_id"], protocol=mqtt.MQTTv311,
                callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            )
            client.on_connect = self.handler.on_connect
            client.on_disconnect = self.handler.on_disconnect
            client.on_message = self.handler.on_message
            client.reconnect_delay_set(min_delay=1, max_delay=30)
            self._stop_event.clear()
            self._client = client
            with self.state["data_lock"]:
                self.state["mqtt_client"] = client
            try:
                # The network loop retries initial connection failures as well as
                # later disconnects; HTTP startup never waits for broker IO.
                client.connect_async(cfg["host"], cfg["port"], cfg["keepalive"])
                client.loop_start()
                self._monitor = threading.Thread(
                    target=self.handler.check_device_timeout, args=(self._stop_event,),
                    name="dashboard-device-timeout", daemon=True,
                )
                self._monitor.start()
                self.running = True
            except Exception:
                self._stop_event.set()
                client.loop_stop()
                with self.state["data_lock"]:
                    self.state.pop("mqtt_client", None)
                self._client = None
                raise

    def stop(self):
        with self._lifecycle_lock:
            self._stop_event.set()
            try:
                if self._client is not None:
                    try:
                        self._client.disconnect()
                    finally:
                        self._client.loop_stop()
            finally:
                if self._monitor is not None:
                    self._monitor.join(timeout=5)
                    if self._monitor.is_alive():
                        raise RuntimeError("Device monitor did not stop")
                self._monitor = None
                self._client = None
                with self.state["data_lock"]:
                    self.state["mqtt_connected"] = False
                    self.state.pop("mqtt_client", None)
                self.running = False
