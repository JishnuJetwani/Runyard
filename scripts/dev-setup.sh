#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ ! -f .env ]]; then
  umask 077
  {
    echo "RUNYARD_OWNER_TOKEN=$(openssl rand -hex 24)"
    echo "RUNYARD_WORKER_TOKEN=$(openssl rand -hex 24)"
    echo "RUNYARD_SIGNING_KEY=$(openssl rand -hex 32)"
  } > .env
fi
if ! rg -q '^RUNYARD_GRAFANA_PASSWORD=' .env; then
  printf 'RUNYARD_GRAFANA_PASSWORD=%s\n' "$(openssl rand -hex 24)" >> .env
fi
echo 'Local credentials are in .env. Build and start with: docker compose up --build -d'
