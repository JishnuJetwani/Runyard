# Architecture

## Programs

* `runyard-server`: serves the APIs, schedules work, stores results, and handles recovery.
* `runyard-agent`: registers a Docker host and manages its experiment containers.
* `runyard-runner`: supervises one experiment and reports its output.
* `runyard`: CLI that uses the public HTTP API.

HTTP, gRPC, and infrastructure adapters call application services through
interfaces. Those services use domain types. Domain code does not depend on HTTP,
gRPC, SQL, Docker, Kubernetes, or the filesystem. Each program's main function
wires these parts together.

Persistence, execution, process supervision, and artifact storage have explicit
interfaces. Database transactions stay within the PostgreSQL adapter.

Each deployment uses either Docker agents or Kubernetes Jobs. Both use the same
runner protocol.

## Ownership and durability

PostgreSQL is the source of truth. A run stores a specification that cannot change;
an attempt is one execution of it. Retries create new attempts. Manual reruns create
new runs linked to the original. A sweep creates all combinations of a parameter
grid in one transaction.

Repeating a submission key returns the same submission. Scheduling reserves
capacity in a transaction, which commits before any remote call. Container and Job
names are deterministic, so a lost launch response can be checked before retrying.

The runner must claim an attempt before starting the experiment. Reports must match
the attempt, generation, runner instance, and a valid lease. Expired leases cannot
be renewed. If two runners start, only the one that claims ownership runs the child.
When completion and cancellation race, the first committed decision wins.

Execution is at least once, so workload side effects can happen more than once.
An accepted cancellation rejects later results immediately. Cleanup stays pending
until the process is confirmed stopped.

Run lifecycle: `QUEUED -> STARTING -> RUNNING -> FINALIZING -> SUCCEEDED`, with
`RETRY_WAIT -> QUEUED`, `FAILED`, and `CANCELLED` branches. Infrastructure failure
retries by default; application failure/timeout retries require opt-in.

Defaults: heartbeat 5 s, lease 30 s, worker availability 15 s, launch deadline
300 s, recovery scan 1 s, retry delay 5 s doubling to 60 s, termination grace 10 s.
The runner uses a conservative monotonic lease deadline and stops before expiry.
An agent restart alone does not invalidate a live reporting runner.

Recovery and Kubernetes reconciliation use `RUNYARD_RECOVERY_SCAN_MILLIS`.
Retry delay uses `RUNYARD_RETRY_BASE_SECONDS` and `RUNYARD_RETRY_MAX_SECONDS`.
Background waits are interruptible, so longer scan intervals do not delay shutdown.

## Storage and interfaces

The public `/v1` HTTP API requires authentication and is documented in OpenAPI.
Lists use pagination; the CLI follows logs by polling with a cursor. Agents and
runners use gRPC for assignments, leases, output, and completion. RPCs have deadlines
and size limits. A bounded executor keeps SQL and remote calls off HTTP event loops.

Logs and metrics are acknowledged after they reach PostgreSQL. Sequence numbers
make resending safe. Artifacts are staged and checksummed before publication, with
the same rules for filesystem and S3 storage. A machine failure can lose output
that has not been acknowledged. Dropped or truncated output is marked.

Workload images include the runner. The child receives paths for parameters,
metrics, and output files. Commands run as argument arrays without shell expansion.
Metrics contain a name, step, and finite value. Only regular files under the output
directory can become artifacts. See [packaging experiments](WORKLOADS.md).

## Deployment

Linux arm64/amd64 are runtime targets; the CLI also supports macOS. Docker agents
use the Engine API through a Unix socket. Experiments never receive that socket.
Kubernetes Jobs use one completion, `restartPolicy: Never`, and `backoffLimit: 0`.
Runyard owns retries; Kubernetes owns node placement.

One coordinator holds a PostgreSQL advisory lock. Losing the database stops dispatch
and makes readiness fail. TLS is required outside the development profile. Owner,
agent, and attempt credentials are separate. Only the coordinator has database and
storage credentials.

AWS definitions cover VPC, EKS/CPU nodes, ECR, private single-AZ RDS, S3, and IAM.
Application services stay private. Infrastructure provisioning and application
installation are separate. See [AWS deployment](AWS.md) for the operator procedure.

## Whole-device GPU allocation

`Resources` includes a GPU count. Canonical JSON omits a zero count to preserve
existing CPU submission fingerprints. Runs, sweeps, and reruns store the count in
the column added by migration 006.

`GpuInventory` separates discovery from scheduling. Its NVIDIA adapter loads NVML
dynamically and uses RAII to manage the library and session. Devices use UUIDs
because indices can change after a reboot. After container reconciliation, the
agent reports inventory with its session and a sequence number. Discovery errors
pause GPU admission without deleting inventory or reservations.

The PostgreSQL adapter locks worker, run, attempt, then UUID-ordered device rows.
One transaction selects a feasible CPU/memory/GPU request and reserves all its
devices. A partial unique index prevents two unreleased allocations for one UUID.
An unacknowledged assignment is returned again before new work is assigned.
GPU allocations are released only when runtime cleanup is confirmed in the same
transaction. Released records remain in the attempt history.

Docker uses the assigned UUIDs in `DeviceRequests` and checks existing containers
before reuse. Kubernetes requests whole `nvidia.com/gpu` resources on exclusive
nodes. Its device plugin chooses the physical devices; Runyard records the node.
Both backends use the same runner protocol.

Kubernetes capacity collection runs in a separate thread with a mutex-protected
snapshot. It reads Nodes and Pods across namespaces using a read-only ClusterRole,
and stores only resource summaries. Requests include init containers and restartable
sidecars. Availability is an estimate and becomes unknown when stale. The collector
does not schedule work or block recovery.

The environment builder preserves selected library and Python settings, then
applies workload values and platform-controlled paths and GPU visibility.
It does not pass coordinator or runner credentials to the child.
See [GPU operations](GPU.md) for configuration and refresh intervals.
