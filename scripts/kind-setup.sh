#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if ! kind get clusters | grep -qx runyard; then
  previous_context=$(kubectl config current-context 2>/dev/null || true)
  kind create cluster --name runyard --config deploy/kind/cluster.yaml --image kindest/node:v1.34.0@sha256:7416a61b42b1662ca6ca89f02028ac133a309a2a30ba309614e8ec94d976dc5a
  if [[ -n "$previous_context" ]]; then kubectl config use-context "$previous_context" >/dev/null; fi
fi
for node in $(kind get nodes --name runyard); do docker start "$node" >/dev/null; done
scripts/wait-kind.sh
scripts/dev-setup.sh
docker compose up -d registry
registry_id=$(docker compose ps -q registry)
docker network connect kind "$registry_id" 2>/dev/null || true
for node in $(kind get nodes --name runyard); do
  docker exec "$node" mkdir -p /etc/containerd/certs.d/localhost:5001
  docker exec -i "$node" sh -c 'cat > /etc/containerd/certs.d/localhost:5001/hosts.toml' <<HOSTS
[host."http://runyard-registry-1:5000"]
  capabilities = ["pull", "resolve"]
HOSTS
done
postgres_image=postgres:17.9-bookworm@sha256:47f917f7409eacd22fc5dfb1dee634e1b55cf0c01d1a7eb701be2227a03e0641
if ! docker image inspect "$postgres_image" >/dev/null 2>&1; then docker pull "$postgres_image"; fi
kind load docker-image runyard-server:dev --name runyard
# The upstream index lists platforms absent from the host's partial image cache.
# Import the node's platform instead of kind's default --all-platforms import.
for node in $(kind get nodes --name runyard); do
  docker image save "$postgres_image" | docker exec --privileged -i "$node" \
    ctr --namespace=k8s.io images import --base-name docker.io/library/postgres \
      --digests --snapshotter=overlayfs -
done
kubectl --context kind-runyard apply -k deploy/kubernetes/overlays/local
umask 077
mkdir -p .local
if [[ ! -f .local/kind-postgres-password ]]; then openssl rand -hex 24 > .local/kind-postgres-password; fi
set -a
source .env
set +a
python3 - <<'PY'
import json, os, pathlib
password=pathlib.Path('.local/kind-postgres-password').read_text().strip()
secrets={'RUNYARD_DATABASE_URL': f'host=postgres user=runyard password={password} dbname=runyard connect_timeout=3'}
secrets.update({key:os.environ[key] for key in ('RUNYARD_OWNER_TOKEN','RUNYARD_WORKER_TOKEN','RUNYARD_SIGNING_KEY')})
for name,values in [('runyard-secrets',secrets),('postgres-secret',{'password':password})]:
    pathlib.Path('.local/'+name+'.json').write_text(json.dumps({'apiVersion':'v1','kind':'Secret','metadata':{'name':name,'namespace':'runyard'},'stringData':values}))
PY
kubectl --context kind-runyard apply -f .local/runyard-secrets.json -f .local/postgres-secret.json
kubectl --context kind-runyard -n runyard rollout status statefulset/postgres --timeout=180s
kubectl --context kind-runyard -n runyard delete job migrate --ignore-not-found
kubectl --context kind-runyard apply -f deploy/kind/migrate.yaml
kubectl --context kind-runyard -n runyard wait --for=condition=complete job/migrate --timeout=120s
kubectl --context kind-runyard -n runyard scale deployment/server --replicas=1
kubectl --context kind-runyard -n runyard rollout status deployment/server --timeout=180s
echo 'Ready. In a separate terminal: scripts/kind-forward.sh'
