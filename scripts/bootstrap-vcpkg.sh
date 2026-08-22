#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .deps
if [[ ! -d .deps/vcpkg/.git ]]; then git clone --filter=blob:none https://github.com/microsoft/vcpkg.git .deps/vcpkg; fi
git -C .deps/vcpkg checkout fa8cecf91d7f31a1715a7a6524f208897ffb33ce
.deps/vcpkg/bootstrap-vcpkg.sh -disableMetrics
echo 'Set VCPKG_ROOT to the absolute path of .deps/vcpkg, then use the vcpkg CMake preset.'
