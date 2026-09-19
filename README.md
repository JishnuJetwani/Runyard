# Runyard

Runyard runs containerized CPU and GPU experiments on your own machines.
Submit a job, run it on Docker workers or Kubernetes, and keep its configuration,
logs, metrics, and output files in one place.

The backend is written in C++20, with a React/TypeScript dashboard and a C++ CLI.

## Features

- Schedule jobs by priority and available CPU, memory, and NVIDIA GPUs.
- Submit parameter sweeps and compare results in the dashboard.
- Retry interrupted jobs, enforce timeouts, and cancel running experiments.
- Keep the history and output of every attempt.
- Store artifacts on the filesystem or in S3 and verify downloads with SHA-256.
- Monitor worker capacity, queue demand, and failures with Prometheus and Grafana.

## Quick start

Requires Git, Python 3.9+, and Docker with Compose 2.20+.
Use Docker Desktop on macOS or Docker Engine on Linux.

```bash
git clone https://github.com/JishnuJetwani/Runyard.git
cd Runyard
./runyard up
```

Setup starts PostgreSQL, the coordinator, three local workers, and the dashboard.
It also runs an example and checks its logs, metrics, and output file.

Open the [dashboard](http://127.0.0.1:3000). Get your API key with:

```bash
./runyard key
```

Paste the key into **Connection settings**. Setup also prints a link to the example run.

The first start builds missing images in Docker and can take a while.
Later starts reuse them. Stopping the installation keeps your credentials,
run history, and artifacts.

```bash
./runyard status
./runyard logs server --follow
./runyard stop
./runyard up
```

See the [installation guide](docs/INSTALL.md) for settings, prebuilt images,
and troubleshooting. To add another machine, follow the
[worker setup guide](docs/WORKERS.md).

## Run an experiment

Build your experiment image from the Runyard runner base. Describe its image
digest, command, parameters, resources, and timeout in a JSON file.

```bash
./runyard cli submit experiment.json
./runyard cli runs get RUN_ID --attempts
./runyard cli logs RUN_ID --follow
./runyard cli metrics RUN_ID --format csv
./runyard cli artifacts list RUN_ID
./runyard cli capacity
```

The runner passes parameters through a JSON file, reads metrics from JSONL,
and collects output files as artifacts. Workloads do not need a Runyard SDK.
See the [workload guide](docs/WORKLOADS.md) and [CLI reference](docs/CLI.md).

Set `resources.gpu_count` to request whole NVIDIA GPUs. Docker workers reserve
specific devices; Kubernetes uses the NVIDIA device plugin.
See the [GPU setup guide](docs/GPU.md) and the
[PyTorch example](examples/gpu-training/README.md).

## Architecture

The coordinator stores run state in PostgreSQL. Docker agents request work;
in Kubernetes mode, the coordinator creates Jobs. Both use the same runner to
start the experiment, collect logs and metrics, and upload artifacts.

Leases prevent old attempts from reporting results after a retry. An accepted
cancellation takes effect immediately, but cleanup stays pending until the process
has stopped.
See [failure behavior](docs/FAILURES.md) for the recovery rules.

Runyard is designed for private deployments with trusted workloads and one active
coordinator. The local setup runs three logical workers on one Docker engine.

| Area | Technologies |
|---|---|
| Backend | C++20, Drogon, gRPC/Protobuf, libpqxx, libcurl |
| Dashboard | React, TypeScript, TanStack Query, Recharts |
| Execution and storage | Docker, Kubernetes, NVML, PostgreSQL, S3 |
| Operations and builds | Prometheus, Grafana, CMake, vcpkg, GitHub Actions, Terraform |

## Documentation

- [Architecture](docs/ARCHITECTURE.md) and [source builds](docs/BUILD.md)
- [Dashboard development](docs/UI.md) and [REST API](api/openapi.yaml)
- [Kubernetes setup](docs/KUBERNETES.md) and [AWS infrastructure](docs/AWS.md)
- [Operations](docs/OPERATIONS.md) and [monitoring](docs/OBSERVABILITY.md)
- [Tests](docs/TESTING.md) and [benchmarks](benchmarks/README.md)
