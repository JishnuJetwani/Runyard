# Failure and recovery

PostgreSQL records accepted work and final outcomes. Execution is at least once,
so a workload may run more than once. If it writes to an external system, make
those writes safe to repeat.

| Failure | Behavior |
|---|---|
| Submission response lost | Reuse its request key to retrieve the same run/sweep/rerun. A different normalized payload conflicts. |
| Runtime create response lost | Discover the deterministic container/Job before attempting another create. |
| Duplicate runner starts | Only one instance claims the attempt; other instances do not start the child. |
| Runner stops reporting | Its 30-second lease expires; recovery records failure and follows retry policy. |
| Late report/completion | Generation, instance, active-attempt, deadline, and lease checks reject stale ownership. |
| Coordinator restart | Queued runs, attempts, launch intent, telemetry, artifacts, and retry delays remain in PostgreSQL. |
| Database unavailable | Readiness and new dispatch fail; runners stop the child before their conservative lease expires. |
| Agent restart | New session fences old agent requests; still-reporting runners remain live and reserved. |
| Cancel during completion | The first committed terminal decision wins. Physical cleanup can remain pending. |
| Runtime cannot be reached | Accepted cancellation remains CANCELLED; cleanup stays PENDING until absence is confirmed. |
| Partial artifact upload | No metadata is published. The same path/content can be transferred again while ownership is valid. |
| Telemetry retransmission | Persisted sequence prefixes are acknowledged; duplicates do not create records and gaps request the missing suffix. |
| Nonzero application exit | Preserve exit code and completed output, then follow retry_exit. |
| Execution timeout | Stop the process group and classify TIMEOUT; retry only with retry_timeout. |
| Kubernetes Job terminal or missing | Fence the current attempt and use infrastructure retry policy; an expired execution deadline retains timeout policy. |
| Kubernetes API unavailable | Desired attempts remain durable, admission is bounded, and launch/lease deadlines still run separately. |

The defaults are heartbeat 5 s, lease 30 s, unavailable-worker threshold 15 s,
launch timeout 300 s, finalization timeout 300 s, recovery scan 1 s, and retry delay
5 s doubling to a maximum of 60 s. Process termination allows 10 s before SIGKILL.
See [operations](OPERATIONS.md) for the corresponding timing settings.
A lease must exceed heartbeat + termination grace + a two-second safety margin.

The runner uses a monotonic deadline measured conservatively from before each
renewal request. An expired lease cannot be revived. Transport deadlines do not
replace process-group termination. Runner finalization and transfer retries retain
the same ownership; scheduling retries always create another attempt.

Docker reservations remain until cleanup is confirmed, even after a lease expires.
A missing heartbeat does not prove the workload has stopped. Restore the worker
connection so it can check its containers and finish cleanup.

A PostgreSQL advisory lock allows one active coordinator. A second process can
answer liveness checks but cannot become ready or dispatch. Automatic leader
failover, untrusted workloads, and recovery of unsent logs are not supported.
Operators must manage retention and clean up unreferenced objects and download caches.
