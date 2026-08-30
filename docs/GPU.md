# GPU execution

Set `resources.gpu_count` to request 0 through 64 whole NVIDIA GPUs. The default
is zero. CPU, memory, and GPUs must all fit on one worker or Kubernetes node.
Sweeps use the same resources as their base specification.

Placement uses GPU count, not model or memory size. GPU sharing, MIG, MPS, and
distributed training are not supported.

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
inherit coordinator or runner credentials.

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
