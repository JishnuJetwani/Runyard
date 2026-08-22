# Operations

Use one active coordinator and one execution mode per database. A PostgreSQL
advisory lock controls dispatch and readiness. Keep worker IDs stable across
restarts. Registering again replaces the agent session but leaves healthy runners
and their leases alone.

## Configuration

The server reads environment variables. Secret values belong in ignored local
files or deployment Secrets. Normal startup never applies migrations.

| Setting | Default / meaning |
|---|---|
| `RUNYARD_DATABASE_URL` | Required libpq connection string |
| `RUNYARD_OWNER_TOKEN` | Required private REST credential, at least 16 characters |
| `RUNYARD_WORKER_TOKEN` | Separate required worker credential, at least 16 characters |
| `RUNYARD_SIGNING_KEY` | Required attempt capability key, at least 32 characters |
| `RUNYARD_EXECUTION_MODE` | `docker` or `kubernetes` |
| `RUNYARD_PROFILE` | Set to `development` only for explicit plaintext local use |
| `RUNYARD_BIND` | `127.0.0.1`; container deployments use `0.0.0.0` |
| `RUNYARD_HTTP_PORT`, `RUNYARD_GRPC_PORT` | `8080`, `9090` |
| `RUNYARD_TLS_CERT`, `RUNYARD_TLS_KEY` | Required outside development; configure together |
| `RUNYARD_HEARTBEAT_SECONDS` | `5` |
| `RUNYARD_LEASE_SECONDS` | `30`; must exceed heartbeat + termination grace + 2 |
| `RUNYARD_WORKER_SECONDS` | `15`, stale workers receive no new work |
| `RUNYARD_LAUNCH_SECONDS` | `300` |
| `RUNYARD_FINALIZATION_SECONDS` | `300` |
| `RUNYARD_RETRY_BASE_SECONDS`, `RUNYARD_RETRY_MAX_SECONDS` | `5`, `60` |
| `RUNYARD_TERMINATION_SECONDS` | `10`, then SIGKILL |
| `RUNYARD_RECOVERY_SCAN_MILLIS` | `1000`, recovery/leadership and Kubernetes scans |
| `RUNYARD_ARTIFACT_ROOT` | `.local/artifacts`, staging/cache and filesystem object root |
| `RUNYARD_STORAGE` | `filesystem` or `s3`; see [storage](STORAGE.md) |
| `RUNYARD_MAX_ACTIVE_JOBS` | `16`; checked-in Kubernetes deployment uses `8` |

Docker agents need `RUNYARD_COORDINATOR`, `RUNYARD_WORKER_ID`, worker token, and
configured `RUNYARD_CPU_MILLIS`/`RUNYARD_MEMORY_MIB`. TLS clients use
`RUNYARD_TLS_CA`. The Docker socket is available only to the agent. Drain a worker
before maintenance so existing experiments can finish. CPU and memory capacity
come from these settings; the agent does not discover the host's available budget.

## Startup, upgrades, and shutdown

For Compose, `docker compose up -d` starts PostgreSQL, runs the explicit migration
service, and then starts the coordinator and agents. For a source deployment run
`runyard-server migrate --directory migrations` before `runyard-server serve`.
Migrations use a lock and recorded checksums; never edit an applied migration.

Back up PostgreSQL and artifact objects together before an upgrade. Stop admission
or drain workers, wait for active runs, stop the server, apply migrations, and
start the updated deployment. Keep signing keys for active attempts. To rotate a
key, stop new work and let active attempts finish; only one signing key is supported.
Short restarts can preserve active work. If the outage lasts beyond a runner's
lease, it stops and the attempt follows its retry policy.

`docker compose stop` preserves local database, registry, and artifact volumes.
`docker compose down` also preserves named volumes. Adding `--volumes` destroys
local history and stored results; use it only for an intentionally disposable setup.
For kind, `kind delete cluster --name runyard` destroys that cluster and its local
database/artifact volumes. Cloud teardown is documented separately in [AWS.md](AWS.md).

## Troubleshooting

| Symptom | Inspect / action |
|---|---|
| Run remains QUEUED | Worker availability/drain status, requested capacity, execution mode, readiness |
| Run remains STARTING | Agent/coordinator structured logs, image digest/registry access, Pod events, launch deadline |
| Repeated infrastructure retries | Attempt reasons, worker connectivity, runner lease renewal, memory limits |
| CANCELLED with cleanup pending | Restore Docker/Kubernetes connectivity; inspect the named runtime before manual removal |
| Readiness returns 503 | Database connectivity or another coordinator holding the leadership lock |
| Artifacts unavailable | Coordinator object/staging disk, S3 endpoint/IAM configuration, checksum error logs |
| TLS verification fails | Matching certificate hostname, CA chain, certificate lifetime, mounted trust files |
| Kubernetes port forward disappears | Use `scripts/kind-forward.sh`, which reconnects after Pod replacement |

Logs carry request/run/attempt identities where applicable. Prometheus labels stay
bounded; [monitoring](OBSERVABILITY.md) explains dashboards and alerts. Health and
metrics endpoints are operational endpoints on the private listener. Do not expose
the service to the public internet.

Retention is manual. Monitor disk use for PostgreSQL, artifacts, staging files,
and the download cache. Failed publication can also leave unreferenced objects.
Never remove objects referenced by artifact metadata. Plan backups, restore checks,
and cleanup for deployments that keep a large history.
