# Benchmarks

Use the standard Compose or kind configuration, a warm pinned fixture image, and
an otherwise idle Runyard queue. Export the development API URL and owner token.

```
python3 benchmarks/run.py --backend docker --image "$RUNYARD_FIXTURE_IMAGE" \
  --count 100 --recovery-evidence .local/acceptance-docker.json \
  --output benchmarks/results/docker-local.json
```

The script records timestamps, the workload specification and image digest, Git
revision, worker settings, host/VM details, and before/after Prometheus snapshots.
Percentiles use linear interpolation.

| Measurement | Definition |
|---|---|
| Queue delay | Submission to first admission |
| Dispatch latency | Admission to runner claim |
| Throughput | Successful runs divided by elapsed time from first submission to last committed completion |
| Recovery | Interruption to replacement claim, including lease expiry and retry delay |
| CPU | Coordinator process CPU-time increase, including benchmark polling |
| Memory | Peak resident memory since coordinator startup, not just during the benchmark |

Operational snapshots refresh once per recovery scan.

Logical Docker workers share one engine, VM, disk, and physical host. A kind
cluster on that VM also shares those resources. Results describe the recorded
fixture workload and include scheduling and launch overhead.

Run `python3 benchmarks/report.py` to generate the Markdown comparison.
Recovery measurements come from a separate fault-injection run and do not affect
the steady-workload throughput measurement.

## Worker scale and recovery checks

`worker_scale.py` creates an isolated Compose project with a private database and random
credentials. It uses the existing `runyard-server:dev` and `runyard-agent:dev`
images and a locally available fixture digest. Build and publish those images
with the root quickstart first. It does not restart or clear the normal deployment.

```bash
python3 benchmarks/worker_scale.py --image "$RUNYARD_FIXTURE_IMAGE" \
  --workers 12 --count 120 --rounds 3 --fault-batches 2 \
  --output benchmarks/results/worker-scale/docker-12-workers.json
```

Ports 18081 and 19091 must be free (override with `--http-port`/`--grpc-port`).
Allow roughly ten minutes. Each worker reserves 250 millicores and 256 MiB of
logical capacity; all workers share the Docker engine. The script warms the
fixture cache, measures three independent sweeps, kills one agent while its
runner stays healthy, then kills three agents and their runners together in
each fault batch. It checks successful replacement attempts on surviving workers.
The longer fault workload also checks that all 12 workers execute concurrently.
Cleanup removes only this invocation's containers, network, and database volume.

Recovery records the start and end of the kill command, plus database and host
clock readings. The summary uses the earlier boundary. Dispatch uses PostgreSQL
timestamps for assignment and runner claim; it does not measure when the experiment
process starts. Eight API readers poll with 250 ms pauses, and that traffic is part
of the measured load. Lease and retry settings keep their defaults. The script
saves intermediate results even if a later check fails.

The separate GPU protocol check requires `tests/requirements.txt` in a Python
virtual environment and free ports 18082/19092:

```bash
.local/venv/bin/python benchmarks/gpu_allocation.py \
  --workers 12 --waves 10 \
  --output benchmarks/results/worker-scale/gpu-12-workers.json
```

This uses synthetic worker/device inventories and runner RPC clients against an
isolated coordinator and PostgreSQL. It verifies exclusive allocation,
assignment replay, duplicate claim rejection, cancellation fencing, and reuse
only after cleanup. This is a protocol-level allocation benchmark.
The recorded `Poll` RPC duration measures assignment only; it is separate from
the Docker assignment-to-runner dispatch measurement. See the
[worker benchmark report](WORKER_SCALE.md) for observed results and scope.
