# GPU execution

Set `resources.gpu_count` to request 0 through 64 whole NVIDIA GPUs. The default
is zero. CPU, memory, and GPUs must all fit on one worker or Kubernetes node.
Sweeps use the same resources as their base specification.

Placement uses GPU count, not model or memory size. GPU sharing, MIG, MPS, and
distributed training are not supported.

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

Set `RUNYARD_GPU_MODE=nvidia` on a Linux Docker agent to enable discovery and GPU
launches. `RUNYARD_GPU_UUIDS=GPU-...,GPU-...` optionally limits the advertised devices.
The host needs the NVIDIA driver and Container Toolkit. DeviceRequests contain
assigned UUIDs, never an unrestricted device count. CPU containers explicitly use
`NVIDIA_VISIBLE_DEVICES=void`. Restart reconciliation compares image, worker,
attempt, and GPU requests before reusing a deterministic container.

Kubernetes Jobs request and limit `nvidia.com/gpu` equally. GPU Jobs select nodes
labeled `runyard.io/gpu-mode=exclusive` and tolerate the `nvidia.com/gpu:NoSchedule`
taint. Set `RUNYARD_KUBERNETES_GPU_RUNTIME_CLASS` when the cluster uses a dedicated
NVIDIA runtime. Runyard leaves device visibility to the device plugin. The winning
runner claim records its node through the Downward API; duplicate runners cannot
replace that placement record. Kubernetes owns physical device allocation.
