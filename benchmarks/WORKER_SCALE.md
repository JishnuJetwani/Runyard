# Worker-scale benchmarks

Measured September 19, 2026 with the default lease, retry, polling, and launch settings.
Docker execution used 12 logical CPU workers on one Apple M4 host. A separate
protocol benchmark used synthetic GPU inventories with the coordinator and PostgreSQL.

| Measurement | Observed result |
|---|---|
| Concurrent Docker workers | All 12 received work and had overlapping active attempts during both fault batches |
| Docker dispatch | 2.050 s p50 and 3.082 s p95 over 360 executions |
| Fault-to-replacement recovery | 37.715–39.842 s over six interrupted attempts |
| GPU allocation RPC | 5.791 ms p50 and 8.832 ms p95 over 120 requests |

## Docker execution

The [raw Docker record](results/worker-scale/docker-12-workers.json) contains
397 successful runs: 12 warmup runs, three 120-run measurement sweeps, one
agent-only crash run, and two 12-run fault sweeps. Six runs required replacement
attempts, giving 403 total attempts. All other runs succeeded on their first
attempt. The measurement sweeps used a warm fixture image and approximately
100 ms of intended experiment work per run.

| Measurement | Round 1 | Round 2 | Round 3 | Pooled |
|---|---:|---:|---:|---:|
| Executions | 120 | 120 | 120 | 360 |
| Dispatch p50 | 1.909 s | 1.797 s | 2.318 s | 2.050 s |
| Dispatch p95 | 2.632 s | 2.769 s | 3.906 s | 3.082 s |
| Dispatch p99 | 2.915 s | 3.215 s | 4.178 s | 4.034 s |
| Queue delay p95 | 52.671 s | 50.805 s | 57.087 s | N/A |
| Submission-to-claim p95 | 54.778 s | 53.063 s | 60.769 s | N/A |

Dispatch is durable attempt creation to runner claim, using PostgreSQL timestamps
for both ends. This includes Docker launch work and excludes waiting in the
queue. A claim precedes the experiment child process starting. Queue delay is
submission to attempt creation. The 120-run bursts exceed the 12 single-slot
workers' capacity, so submission-to-claim includes substantial queueing.
Percentiles use linear interpolation over individual samples; the pooled result
is not an average of the three round percentiles.

### Failure recovery

Each fault sweep first waited until all 12 workers had a running experiment.
One Docker kill command then sent SIGKILL to three agents and their three runner
containers. This injects process loss on the shared Docker engine.

| Batch | Restart after fault command began | Restart after fault command returned |
|---|---|---|
| 1 | 37.715, 38.068, 37.863 s | 36.729, 37.082, 36.877 s |
| 2 | 39.842, 39.109, 39.202 s | 39.002, 38.269, 38.362 s |

All six original attempts recorded `FAILED / LEASE_EXPIRED`. All six replacements
ran on surviving workers and succeeded. The earlier fault-command boundary is
used for the summary because the precise death time of each process falls inside
that command's execution window. The table reports all six observations.

The deployment used the default 30 s attempt lease, 5 s heartbeat, 5 s initial
retry delay, and 1 s recovery scan. The coordinator must wait for ownership to
expire before retrying an unreachable runner. Recovery includes lease expiry,
retry delay, capacity availability, and container startup.

Killing only an agent allowed its still-healthy runner to finish the original
attempt, without an unnecessary retry. Agent availability and runner ownership
are separate; an agent-process crash alone need not restart an experiment.

### Environment and measurement limits

- One Apple M4 Mac, macOS 15.6.1, arm64, 10 logical CPUs.
- Docker 29.1.5, aarch64 VM, 10 CPUs and 8,217,382,912 bytes of RAM.
- Twelve agents, each advertising 250 millicores and 256 MiB; fixtures requested
  250 millicores and 128 MiB for CPU fixture execution.
- One shared Docker engine, storage, network, and host. Existing Runyard services
  and local Kubernetes containers remained running; their count is recorded.
- Separate database, network, credentials, and uniquely identified worker/runtime
  containers. All benchmark-owned resources were removed after measurement.
- Eight concurrent API readers fetched run/attempt snapshots and paused 250 ms
  between complete sweeps. This observation load is included in the timings.
- Recovery compares host and database clocks. Before/after clock probes bounded
  the database-minus-host offset to approximately −0.019 through +0.250 s.
  Dispatch uses one
  database clock and does not require host-clock alignment.
- Checkout `3da9336`, with runtime image IDs and fixture digest recorded in the
  raw JSON. Only benchmark tooling and records were added during this work.

## GPU allocation and ownership

The [GPU protocol record](results/worker-scale/gpu-12-workers.json) contains ten
waves of 12 concurrent assignments through gRPC and PostgreSQL. Worker/device
inventories and runner clients were synthetic. The benchmark measures the
allocation and ownership protocol.

Every wave verified exact, exclusive UUID assignment, replay before launch
acknowledgement, duplicate runner rejection, retained reservations during inventory
errors, cancellation fencing, and device reuse only after confirmed cleanup.
Duplicate UUID advertisement by another worker was rejected.

Assignment-only `Poll` RPC duration was 5.791 ms p50 and 8.832 ms p95 over 120
requests. This is a different measurement from Docker dispatch: it excludes
agent polling cadence, container startup, and runner claim.
