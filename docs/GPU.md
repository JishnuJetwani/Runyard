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
