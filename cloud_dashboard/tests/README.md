# Cloud dashboard tests

Install dependencies and run from the repository root:

```powershell
python -m pip install -r cloud_dashboard/requirements-test.txt
python -m pytest cloud_dashboard/tests -q
```

Database tests use a fresh temporary SQLite file per test. MQTT handler unit
tests replace database writes with mocks and never connect to a broker.
The production dashboard database is not opened by these tests.

Current coverage: database initialization, device upserts, result ordering,
filtering, pagination, aggregates, heartbeat persistence, MQTT connection
callbacks, message dispatch, duplicate reports, result counters, and SSE queues.
HTTP tests cover status, statistics, recent results, history parameter mapping,
remote command validation/publishing and SSE disconnect cleanup. HTTP tests
mock external effects and do not start a server or connect to a broker.
MQTT boundary tests also cover invalid UTF-8/JSON shapes, malformed result fields,
deduplication cache eviction, timeout thresholds and result persistence failures.
Administrative route integration tests cover ROI add/update/rename/delete,
device updates, clearing the temporary database, exports, diagnostics, statistics
reloads and simulated message counts. Each application receives its own Database and DashboardRuntime.

Collect production-only statement and branch coverage from the repository root:

```powershell
python -m pytest cloud_dashboard/tests --cov --cov-config=cloud_dashboard/.coveragerc --cov-report=term-missing
```

Lifecycle tests cover import without IO, isolated app instances, startup/stop/restart,
failed startup recovery, cache read failures, partial transaction rollback, and
invalid management input. MQTT networking uses a client factory mock.
