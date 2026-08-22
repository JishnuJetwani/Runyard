# Building the backend

`scripts/build-images.sh` builds the server, agent, runner base, fixture, and CLI
images from the same source tree. Docker BuildKit is required. The Ubuntu base
image and vcpkg baseline are pinned. Ubuntu 24.04 repositories supply build tools
and OS security packages. Use the built image's digest in workload specifications.

The first build takes a while, mostly because of gRPC. Builds use three compiler
jobs to limit memory use. BuildKit caches dependencies and object files separately
for each architecture, including dependencies built before a later package fails.
Finished images include everything they need at runtime.

Linux prerequisites include GCC/Clang with C++20, CMake, Ninja, pkg-config,
Autoconf, Automake, Libtool, **Bison and Flex**, Python, Git, curl, zip/unzip, and
development headers. Bison/Flex are required by the pinned PostgreSQL client port.
The Dockerfile and CI install them. To build from source:

```sh
scripts/bootstrap-vcpkg.sh
cmake --preset vcpkg
cmake --build --preset vcpkg -j 3
```

All four runtime programs use the same CMake targets on Linux arm64/amd64. The
release workflow builds each architecture on a native runner and publishes only
when `publish=true`. See [testing](TESTING.md) for local test commands.

The Docker build runs GoogleTests. Tests needing PostgreSQL or S3 skip when their
endpoints are absent. The integration workflow runs those tests with PostgreSQL
and Moto, then checks the Linux service images against fresh databases.
