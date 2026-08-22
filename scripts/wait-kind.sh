#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .local
deadline=$((SECONDS + 180))
# A restarted API server can answer before its authorization caches are ready.
while (( SECONDS < deadline )); do
  if kubectl --context kind-runyard --request-timeout=5s wait \
      --for=condition=Ready node --all --timeout=5s > .local/kind-ready.log 2>&1; then
    cat .local/kind-ready.log
    exit 0
  fi
  sleep 2
done
cat .local/kind-ready.log >&2
exit 1
