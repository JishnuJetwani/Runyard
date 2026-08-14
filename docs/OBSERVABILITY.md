# Monitoring

The coordinator serves Prometheus metrics at `/metrics`. This route and the health
checks do not require an owner key, so keep the listener private. `/health/live`
checks the HTTP process; `/health/ready` also requires the database leadership lock.
The application API requires owner authentication.

Metrics cover runs, attempts, retries, failures, queue age, worker capacity,
database pool use, RPC timing, and process CPU and peak memory use.
Queue and dispatch p50/p95/p99 values cover first attempts from the past hour.
Queue delay ends at admission; dispatch delay runs from admission to runner claim.
History totals are gauges because deleting old records can reduce them.
RPC counters and histograms reset on coordinator restart.

Prometheus labels contain only bounded RPC method/status values. Run IDs and
attempt IDs appear in structured JSON logs and durable events, never as metric
labels. Application logs include timestamp, level, and event/message fields;
HTTP request logs carry request IDs, and claim/launch/completion events carry
attempt identifiers. Third-party gRPC/Drogon diagnostics retain their own format.

Metrics are collected outside HTTP event loops. Kubernetes API calls run separately
from recovery, so a slow cluster API does not delay lease expiry or readiness updates.
