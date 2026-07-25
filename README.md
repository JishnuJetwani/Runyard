# Runyard

Runyard is a private platform for CPU experiments. The design uses a C++
coordinator, PostgreSQL, and Docker workers or Kubernetes Jobs. A shared runner
collects logs, metrics, and artifacts. Users submit work through a C++ CLI.

See [architecture](docs/ARCHITECTURE.md) for the system design.

## Scope

The planned backend includes a REST API, gRPC worker protocol, CLI, filesystem
and S3 storage, monitoring, CI, benchmarks, and AWS infrastructure definitions.
It is for one trusted owner and one active coordinator. Public, untrusted
workloads are outside the scope.

## Development

Build with C++20, CMake, Ninja, and vcpkg. Add focused tests with each feature.
