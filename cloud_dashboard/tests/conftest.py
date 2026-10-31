import sys
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


@pytest.fixture
def database(tmp_path):
    from db import Database
    database = Database(tmp_path / "test.db")
    database.init_db()
    return database


@pytest.fixture
def state():
    from state import new_state
    return new_state(100)


@pytest.fixture
def handler(state):
    from mqtt_handler import MQTTHandler
    from db import Database
    return MQTTHandler(state, Mock(spec=Database))


@pytest.fixture
def api(state, database):
    from app import create_app
    from runtime import DashboardRuntime
    runtime = DashboardRuntime(database=database, state=state)
    return SimpleNamespace(app=create_app({"TESTING": True}, runtime=runtime, initialize=False), runtime=runtime)


@pytest.fixture
def client(api):
    return api.app.test_client()
