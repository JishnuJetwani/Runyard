#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ -z "${RUNYARD_OWNER_TOKEN:-}" ]]; then
  RUNYARD_OWNER_TOKEN=$(sed -n 's/^RUNYARD_OWNER_TOKEN=//p' .env)
fi
export RUNYARD_OWNER_TOKEN
docker run --rm --network runyard --user "$(id -u):$(id -g)" \
  -e RUNYARD_OWNER_TOKEN -e RUNYARD_PROFILE=development \
  -v "$PWD:/workspace" -w /workspace runyard-cli:dev --url http://server:8080 "$@"
