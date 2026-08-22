#!/usr/bin/env bash
set -euo pipefail
child=
cleanup() {
  if [[ -n "$child" ]]; then
    kill "$child" 2>/dev/null || true
    wait "$child" 2>/dev/null || true
  fi
}
trap cleanup EXIT
trap 'exit 0' INT TERM
# Port forwarding binds to a Pod; reconnect when a coordinator rollout replaces it.
while :; do
  kubectl --context kind-runyard -n runyard port-forward service/server 8081:8080 &
  child=$!
  wait "$child" || true
  child=
  sleep 1
done
