# Testing

Build the test targets with the same dependencies as the backend:

```sh
cmake --preset vcpkg
cmake --build --preset vcpkg -j 3
ctest --preset vcpkg
```

Database tests require `RUNYARD_TEST_DATABASE` to point to an empty, disposable
PostgreSQL database. They apply migrations and truncate tables between cases.
Tests that need PostgreSQL or S3 skip when their endpoints are absent.

```sh
export RUNYARD_TEST_DATABASE='host=localhost port=55432 user=postgres password=runyard-dev-only dbname=runyard'
ctest --preset vcpkg
```

The suite covers state transitions, resource reservations, ownership, retries,
and cancellation. Process tests check output capture and child termination.
Storage tests check path validation and complete artifact publication.

## API and protocol

Install `tests/requirements.txt` in a Python virtual environment. Use a disposable
database for these harnesses as well; run each against empty application tables.

```sh
python3 tests/api_smoke.py build/vcpkg/src/runyard-server build/vcpkg/src/runyard
python3 tests/protocol_smoke.py build/vcpkg/src
python3 tests/gpu_protocol_smoke.py build/vcpkg/src
python3 tests/adapter_faults.py build/vcpkg/tests/backend_probe
```

The protocol harness starts the coordinator and runner locally and exercises
telemetry delivery, stale ownership, and artifact transfer. Adapter fault tests
use controlled Docker and Kubernetes HTTP responses to check reconciliation.

Repeat the runner protocol over TLS after generating local certificates:

```sh
scripts/tls-certificates.sh
python3 tests/protocol_smoke.py build/vcpkg/src --tls
```

## S3

`RUNYARD_BUILD_DIR=build/vcpkg scripts/test-s3.sh` starts the pinned Moto emulator
and runs the S3 adapter tests. These check object operations and failures;
deployment permissions and connectivity require checks against the target service.

## Container execution

The execution harness runs fixture experiments, interrupts attempts, restarts the
coordinator, and checks cancellation, resource limits, histories, and outputs.
Docker uses logical workers on one engine. Kubernetes uses the `kind-runyard`
context. Docker, kind, and kubectl must be installed for their respective backends.

```sh
scripts/acceptance.sh docker
scripts/acceptance.sh kubernetes
```

The acceptance script builds images unless `--skip-build` is supplied. Existing
application data requires `--reuse`; it is not erased. Results are written to
the ignored `.local/` directory. See the [benchmark guide](../benchmarks/README.md)
for timing definitions and reproduction commands.

## GPU contracts

GPU database tests cover exclusive allocation, concurrent reservations, inventory
loss, cleanup, and device reuse. NVIDIA discovery uses a test-only NVML library.
Capacity tests check Kubernetes resource accounting and snapshot freshness.

```sh
python3 tests/gpu_deployment_test.py
python3 tests/gpu_training_test.py
```

The training tests need the environment pinned in
`tests/gpu-training-requirements.lock`. Hardware execution uses the acceptance
command in [GPU operations](GPU.md), with an NVIDIA worker or Kubernetes node.

## Installation

```sh
python3 tests/install_test.py
python3 tests/install_smoke.py
```

The smoke test requires cached runtime images. It creates an isolated deployment,
checks startup, repeated setup, shutdown, and retained data, then removes its own
containers and volumes. Dashboard checks are described in [UI development](UI.md).

## Sanitizers and CI

The `asan` and `tsan` CMake presets use separate build directories. ASan/UBSan
checks memory and undefined behavior; TSan checks isolated concurrency components.
Configure them with the same dependency toolchain as the normal build.

GitHub Actions runs the build, tests, formatting, and selected static analysis.
Workflow files under `.github/workflows/` define the commands and dependencies.
