#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
backend=${1:-}
if [[ "$backend" != docker && "$backend" != kubernetes ]]; then
  echo 'Usage: scripts/acceptance.sh docker|kubernetes [--skip-build] [--reuse]' >&2
  exit 2
fi
shift
skip_build=false
reuse=false
for option in "$@"; do
  case "$option" in
    --skip-build) skip_build=true ;;
    --reuse) reuse=true ;;
    *) echo "Unknown option: $option" >&2; exit 2 ;;
  esac
done
if ! "$reuse"; then
  if [[ "$backend" == docker ]]; then
    if docker volume inspect runyard_postgres >/dev/null 2>&1; then
      echo 'Database volume already exists; use --reuse for a repeat run. No data was removed.' >&2
      exit 1
    fi
  elif kind get clusters | grep -qx runyard; then
    for node in $(kind get nodes --name runyard); do docker start "$node" >/dev/null; done
    scripts/wait-kind.sh
    if kubectl --context kind-runyard -n runyard get pvc -o name 2>/dev/null | grep -q .; then
      echo 'Cluster already has persistent data; use --reuse for a repeat run. No data was removed.' >&2
      exit 1
    fi
  fi
fi
scripts/dev-setup.sh
if ! "$skip_build"; then scripts/build-images.sh; fi
image=$(scripts/publish-fixture.sh)
export RUNYARD_OWNER_TOKEN
RUNYARD_OWNER_TOKEN=$(sed -n 's/^RUNYARD_OWNER_TOKEN=//p' .env)
export RUNYARD_PROFILE=development
forward=
cleanup() {
  if [[ -n "$forward" ]]; then
    kill "$forward" 2>/dev/null || true
    wait "$forward" 2>/dev/null || true
  fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM
if [[ "$backend" == docker ]]; then
  docker compose up -d --no-build
  export RUNYARD_URL=http://127.0.0.1:8080
else
  scripts/kind-setup.sh
  scripts/kind-forward.sh > .local/kind-forward.log 2>&1 &
  forward=$!
  export RUNYARD_URL=http://127.0.0.1:8081
fi
python3 - <<'PY'
import os, time, urllib.request
for attempt in range(120):
    try:
        with urllib.request.urlopen(os.environ['RUNYARD_URL']+'/health/ready', timeout=2) as response:
            if response.status == 200:
                break
    except OSError:
        pass
    time.sleep(1)
else:
    raise RuntimeError('coordinator did not become ready')
PY
python3 tests/execution_contract.py --backend "$backend" --image "$image" \
  --output ".local/acceptance-$backend.json"
python3 benchmarks/run.py --backend "$backend" --image "$image" \
  --recovery-evidence ".local/acceptance-$backend.json" \
  --output "benchmarks/results/$backend-local.json"
echo "Acceptance and benchmark passed ($backend). Services and evidence remain available."
