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
```

The protocol harness starts the coordinator and runner locally and exercises
telemetry delivery, stale ownership, and artifact transfer. Adapter fault tests
use controlled Docker and Kubernetes HTTP responses to check reconciliation.

## Container execution

The execution harness runs fixture experiments, interrupts attempts, restarts the
coordinator, and checks cancellation, resource limits, histories, and outputs.
Docker uses logical workers on one engine. Kubernetes uses the `kind-runyard`
context. Docker, kind, and kubectl must be installed for their respective backends.

```sh
python3 tests/execution_contract.py --backend docker --image "$RUNYARD_FIXTURE_IMAGE"
```

## Sanitizers and CI

The `asan` and `tsan` CMake presets use separate build directories. ASan/UBSan
checks memory and undefined behavior; TSan checks isolated concurrency components.
Configure them with the same dependency toolchain as the normal build.

GitHub Actions runs the build, tests, formatting, and selected static analysis.
Workflow files under `.github/workflows/` define the commands and dependencies.
