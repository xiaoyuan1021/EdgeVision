"""State ownership shared by HTTP and MQTT within one dashboard instance."""
import threading
from collections import defaultdict, deque


def new_state(max_results):
    return {
        "devices": {},
        "results_deque": deque(maxlen=max_results),
        "stats": {
            "total": 0, "passed": 0, "failed": 0,
            "today": 0, "today_passed": 0, "today_failed": 0,
            "by_roi": defaultdict(lambda: {"total": 0, "passed": 0, "failed": 0}),
            "by_hour": defaultdict(lambda: {"total": 0, "passed": 0, "failed": 0}),
        },
        "mqtt_connected": False,
        "sse_clients": [],
        "data_lock": threading.RLock(),
        "sse_lock": threading.Lock(),
    }
