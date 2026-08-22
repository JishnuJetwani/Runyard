#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/dev-setup.sh >&2
docker compose up -d registry >&2
docker tag runyard-fixture:dev localhost:5001/runyard-fixture:dev
docker push localhost:5001/runyard-fixture:dev >&2
# This digest is the immutable workload input shared by Docker and kind.
docker image inspect localhost:5001/runyard-fixture:dev --format '{{json .RepoDigests}}' | python3 -c 'import json,sys; print(next(value for value in json.load(sys.stdin) if value.startswith("localhost:5001/runyard-fixture@")))'
