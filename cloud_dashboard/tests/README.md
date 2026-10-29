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
Remaining coverage includes administrative routes, timeout transitions and
malformed message schemas.
