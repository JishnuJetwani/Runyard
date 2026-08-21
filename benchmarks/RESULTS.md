# Local measurements

These CPU fixture measurements use logical workers on one shared local host.
Each backend has one recorded sample; see the raw files for exact configuration.

Each backend uses its recorded admission settings. CPU and memory figures
cover the coordinator process.

| Backend | Runs | Runs/s | Queue p50 / p95 (s) | Dispatch p50 / p95 (s) | CPU time (s) | Peak RSS (MiB) |
|---|---:|---:|---:|---:|---:|---:|
| docker | 100 | 1.502 | 31.760 / 61.968 | 0.816 / 1.083 | 4.855 | 33.605 |
| kubernetes | 100 | 2.340 | 20.271 / 39.048 | 0.218 / 0.277 | 3.634 | 39.449 |

## docker

Recorded 2026-09-18T20:05:34.836522+00:00; runtime source revision `79b30f44a39a940e0a1ed30210b34a52ba0535fc`.

Host: Apple M4, 10 logical CPUs.
Docker VM: 10 CPUs, 7.65 GiB RAM.
Non-Runyard containers running: 1.

Interruption-to-replacement claim (seconds): 34.124, 37.017.

[Raw observations and exact settings](results/docker-local.json).

- Shared local host and Docker VM; logical workers are not separate machines.
- Small fixture workload; polling and container startup affect throughput.
- CPU fixture workload with a warm image cache.
- One observed sample, not a statistically isolated capacity study.
- Recovery compares the host fault clock to PostgreSQL timestamps; local clocks are assumed aligned.

## kubernetes

Recorded 2026-09-18T20:02:18.891902+00:00; runtime source revision `79b30f44a39a940e0a1ed30210b34a52ba0535fc`.

Host: Apple M4, 10 logical CPUs.
Docker VM: 10 CPUs, 7.65 GiB RAM.
Non-Runyard containers running: 1.

Interruption-to-replacement claim (seconds): 8.860, 11.019.

[Raw observations and exact settings](results/kubernetes-local.json).

- Shared local host and Docker VM; logical workers are not separate machines.
- Small fixture workload; polling and container startup affect throughput.
- CPU fixture workload with a warm image cache.
- One observed sample, not a statistically isolated capacity study.
- Recovery compares the host fault clock to PostgreSQL timestamps; local clocks are assumed aligned.

Regenerate with `python3 benchmarks/report.py`. Definitions and workload instructions
are in [README.md](README.md).
