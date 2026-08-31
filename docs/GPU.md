# GPU execution

Set `resources.gpu_count` to request 0 through 64 whole NVIDIA GPUs. The default
is zero. CPU, memory, and GPUs must all fit on one worker or Kubernetes node.
Sweeps use the same resources as their base specification.

Placement uses GPU count, not model or memory size. GPU sharing, MIG, MPS, and
distributed training are not supported.

For setup, see [Docker workers](#nvidia-docker-worker) or
[Kubernetes nodes](#nvidia-kubernetes-nodes). The
[training example](../examples/gpu-training/README.md) provides a workload.

## Allocation and recovery

PostgreSQL stores inventory and allocation history. Apply migration 006 before
starting the updated server. Each device UUID belongs to one worker and can have
only one unreleased allocation. Inventory updates must match the worker session
and have a newer sequence number. Failed discovery pauses GPU assignments but
keeps the last inventory and existing reservations. A worker cannot change Docker
engines while it has pending cleanup.

`gpu_count` is part of the version-1 specification. Omitting it and setting it to
zero produce the same submission fingerprint. Runs, sweeps, and reruns preserve
the count. New submissions cannot override NVIDIA device visibility variables.

Discovery loads NVML at runtime and identifies devices by UUID. Inaccessible
devices are omitted; MIG devices and devices with failed metadata queries are
ineligible. A UUID allowlist divides devices between logical workers. If an
allowlisted device is missing, the refresh is marked unavailable.

Agents report GPU launch support when they register. After reconciling existing
containers, they refresh inventory every ten seconds, separately from heartbeats.
Older agents receive CPU work only. GPU assignments contain exact device UUIDs.

The scheduler selects a run that fits CPU, memory, and GPU capacity, ordered by
priority then submission time. It reserves all devices in one transaction.
Inventory older than 30 seconds cannot receive new GPU work. Lost launch replies
return the original assignment. Cancellation, lease expiry, draining, and restarts
keep allocations until runtime cleanup is confirmed.
The lock order is worker, run, attempt, then devices sorted by UUID. Runtime calls
happen after the transaction commits.

The runner preserves selected image settings for PATH, libraries, Python, and CUDA.
It resolves commands using the child's final PATH and working directory. Platform
paths and GPU visibility take precedence over workload values. The child does not
inherit coordinator or runner credentials. See [workload environments](WORKLOADS.md#gpu-workloads-and-image-environments).

Docker launches request only the assigned UUIDs. CPU containers set
`NVIDIA_VISIBLE_DEVICES=void`, including when using CUDA images. After a restart,
the agent checks the image, worker, attempt, and GPU request before reusing a container.

Kubernetes Jobs use equal requests and limits for `nvidia.com/gpu`. They select
nodes labeled `runyard.io/gpu-mode=exclusive` and tolerate the
`nvidia.com/gpu:NoSchedule` taint. The device plugin controls GPU visibility.
The runner that claims the attempt records its node through the Downward API;
duplicate runners cannot replace that record.

## Capacity

Kubernetes capacity refreshes every 15 seconds using read-only Node and Pod access
across namespaces. Scans use pagination, with a limit of 10,000 resources per kind
and ten seconds per scan. An in-flight request can take another ten seconds.
Only resource summaries are retained.

Bound, nonterminal Pods count as reservations, including terminating Pods and
init/sidecar resource peaks. Unscheduled demand is reported separately. A failed
refresh or a snapshot older than 45 seconds makes availability unknown while
preserving the last observation. Kubernetes still handles placement.

`runyard capacity [--json] [--limit N] [--after CURSOR]` requires the owner key.
Totals cover the whole deployment; pagination applies to worker or node details.
Kubernetes reservations include other namespaces. Check per-node counts for
multi-GPU requests, since available GPUs may be on different nodes. Unknown
availability is JSON `null`.

Docker pending demand counts queued and retry-wait runs. Kubernetes pending demand
counts GPU requests from unscheduled Pods. Run and attempt views show requested
counts, Docker device allocation history, and Kubernetes node placement.

## NVIDIA Docker worker

Install the host driver and [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html).
Set `RUNYARD_GPU_MODE=nvidia` on the agent to enable discovery and GPU launches.
The supplied Compose profile sets this for you. Use `RUNYARD_GPU_UUIDS` to limit
the advertised devices.
Configure the Docker runtime with `nvidia-ctk runtime configure --runtime=docker`
and restart Docker during the host's maintenance window. Choose the UUIDs dedicated
to Runyard and configure their worker's CPU/memory budget. Each physical UUID must
belong to one logical worker; allowlists on a shared daemon must be disjoint.

```sh
export RUNYARD_GPU_UUIDS=GPU-your-device-uuid
scripts/dev-setup.sh
scripts/build-images.sh
docker compose -f compose.yaml -f deploy/gpu.compose.yaml --profile gpu \
  up -d postgres migrate server registry agent-gpu
runyard capacity
```

The NVIDIA runtime gives the agent discovery access. Keep advertised devices
dedicated to Runyard containers. The Compose profile uses local development
networking; configure TLS for remote access.

## NVIDIA Kubernetes nodes

Configure the NVIDIA Container Toolkit for containerd and restart that node's
container runtime during maintenance. Its runtime handler must be named `nvidia`.
Label the GPU node `runyard.io/gpu-mode=exclusive`. Use one device-plugin installation
per node, with whole-device allocation, MIG disabled, and no sharing configuration.
The supplied installation pins NVIDIA device plugin v0.17.1 by image digest.

```sh
kubectl --context "$GPU_CONTEXT" label node "$GPU_NODE" runyard.io/gpu-mode=exclusive
kubectl --context "$GPU_CONTEXT" apply -k deploy/kubernetes/nvidia
kubectl --context "$GPU_CONTEXT" apply -k deploy/kubernetes/overlays/gpu
```

The GPU overlay uses the local database and artifact setup. It leaves server
replicas at zero until configuration and migrations are ready. Follow the setup in
[KUBERNETES.md](KUBERNETES.md): create secrets, load or publish the server image,
run migrations, then scale the coordinator to one.
For an existing installation, set `RUNYARD_KUBERNETES_GPU_RUNTIME_CLASS=nvidia` in
its coordinator configuration instead of installing another application stack.
Capacity access is read-only. Workload Pods receive no Kubernetes API token.

## Hardware acceptance

Use an otherwise idle deployment exposing exactly one GPU. Build and publish the
[training image](../examples/gpu-training/README.md), then configure the normal CLI
URL, credentials, and CA. Run from the Docker host or a machine with access to the
explicit Kubernetes context:

```sh
scripts/gpu-acceptance.py --backend docker --cli build/dev/src/runyard \
  --image "$GPU_TRAINING_DIGEST"
scripts/gpu-acceptance.py --backend kubernetes --kube-context "$GPU_CONTEXT" \
  --image "$GPU_TRAINING_DIGEST" --output .local/gpu-kubernetes.json
```

The command checks exclusive placement, cancellation cleanup, GPU reuse, visible
device count, metrics/logs, checksum-verified artifacts, and recovery after interrupting
an attempt. It only cancels or interrupts runs it created and does not provision
infrastructure. The output file records the observed runs and hardware summary.

## Upgrade and rollback

1. Apply migration `006` using `runyard-server migrate` with the coordinator's
   existing database configuration. Normal server startup does not migrate.
2. Deploy the coordinator, agents, runner images, and CLI together. Publish new
   workload image digests containing the updated runner.
3. Exercise the ordinary CPU fixture workflow before enabling NVIDIA mode.
4. Enable the Docker profile or Kubernetes node/runtime configuration above.
5. Confirm fresh `runyard capacity` output and inspect the worker/node details.
6. Submit the one-GPU training specification, then a small parameter sweep.

Before returning to CPU-only binaries, stop GPU submissions and drain GPU workers
or finish/cancel their runs. Wait until every GPU attempt reports cleanup `DONE`
and no unreleased allocations remain. Keep migration 006 and historical device
allocations; do not erase them as part of a binary rollback.

## Diagnosing queued work

For Docker, check worker heartbeats, drain state, inventory age, allowlists, and
CPU/memory headroom together. A stopped or missing NVML library makes GPU discovery
unavailable while CPU work continues. A UUID advertised by two workers is rejected;
use stable worker identities and disjoint allowlists. Pending cleanup can reserve
a device after its run is terminal. Restore the worker's runtime connection so it
can confirm cleanup; a failed inventory refresh is not evidence that a device is free.

For Kubernetes, inspect per-node availability, readiness, cordoning, the exclusive
label, RuntimeClass, and device-plugin Pods. Two GPUs free on separate nodes cannot
satisfy one two-GPU request. Capacity includes other namespaces' Pod reservations;
RBAC or API failures retain the previous observation and mark availability unknown.
GPU queueing and stale inventory are also visible in the monitoring dashboard.
