# GPU execution

Set `resources.gpu_count` to request 0 through 64 whole NVIDIA GPUs. The default
is zero. CPU, memory, and GPUs must all fit on one worker or Kubernetes node.
Sweeps use the same resources as their base specification.

Placement uses GPU count, not model or memory size. GPU sharing, MIG, MPS, and
distributed training are not supported.
