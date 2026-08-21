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
