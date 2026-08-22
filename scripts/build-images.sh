#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
for target in server agent runner-base fixture cli; do
  docker build -f deploy/docker/Dockerfile --target "$target" -t "runyard-$target:dev" .
done
