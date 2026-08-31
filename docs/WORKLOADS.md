# Packaging experiments

Use a locally built or published runner base image of the target architecture.
For reproducible shared images, pin the `FROM` image by digest too. For example:

```dockerfile
FROM runyard-runner-base:dev
USER root
COPY my-experiment /usr/local/bin/my-experiment
USER 10001:10001
```

The base image supplies the runner entrypoint. Submit `command` as an argument
array; it runs without shell expansion. Include a shell in the command if needed.
The image must contain all runtime libraries. Source metadata is saved for
reference and does not trigger image builds.

## Specification

The version-1 specification supports `name`, `image`, `command`, `parameters`,
`environment`, `labels`, `resources`, `timeout_seconds`, `priority`, `retry`, and
`source`. See [OpenAPI](../api/openapi.yaml) for field limits. Defaults are:

| Setting | Default |
|---|---|
| CPU | 1000 millicores (1 CPU) |
| Memory | 512 MiB, including the runner |
| GPUs | 0 |
| Timeout | 1800 seconds |
| Priority | 0, on a scale of 0 to 9; higher runs first |
| Attempts | 3 total |

CPU and memory limits include runner overhead. Environment entries must be
non-secret and cannot use the reserved `RUNYARD_` prefix.

## Inputs and output

The runner writes parameters to `RUNYARD_PARAMETERS_PATH`. The experiment appends
metric JSONL records to `RUNYARD_METRICS_PATH` and writes output files under
`RUNYARD_OUTPUT_DIR`. Metric steps must be nonnegative integers and values must be
finite numbers. Flush each record with a newline. Malformed records produce a
notice; a final record without a newline is ignored.

stdout and stderr are captured as logs and archived under `_runyard/stdout.log`
and `_runyard/stderr.log`. The `_runyard` artifact namespace is reserved.

| Output | Limit per attempt |
|---|---|
| Log archives | 100 MiB combined |
| Telemetry buffer | 4 MiB; dropped records are marked |
| Metric file | 100,000 records, at most 64 KiB per line |
| Artifacts | 1000 files, 256 MiB per file, 1 GiB total |

Symlinks and unsafe paths are rejected. A machine failure can lose output that
has not reached the coordinator.

## Sweeps and retries

A sweep file contains `base` (a normal specification) and `grid` (parameter names
mapped to arrays of values). Every combination becomes a run. The sweep is created
in one transaction, with a limit of 1000 runs and an estimated 16 MiB of expanded
specifications. Use `runyard sweep file.json`, then `runyard sweeps get SWEEP_ID`.

`retry.retry_exit=true` enables retries for nonzero application exits.
`retry.retry_timeout=true` enables timeout retries. Retries run from the original
specification and do not resume checkpoints. A manual `rerun` creates a new run
linked to its terminal predecessor and preserves the old run's entire history.

## GPU workloads and image environments

Set `resources.gpu_count` to an integer from 1 through 64 to request whole NVIDIA
GPUs on one worker or node. CPU and memory requests still include runner overhead.
The [GPU training example](../examples/gpu-training/README.md) supplies a pinned
PyTorch environment, scalar parameters, epoch metrics, and model/summary artifacts.

The child inherits only these image/runtime environment values before workload
overrides: `PATH`, `LD_LIBRARY_PATH`, `PYTHONPATH`, `VIRTUAL_ENV`, `CUDA_HOME`,
`CUDA_PATH`, `CUDA_VISIBLE_DEVICES`, `NVIDIA_VISIBLE_DEVICES`, and
`NVIDIA_DRIVER_CAPABILITIES`. The final child PATH resolves command names, including
virtual-environment executables. Platform paths and NVIDIA exposure values are
applied last. New specifications cannot override the two NVIDIA variables or the
`RUNYARD_` namespace; the child receives no inherited coordinator credentials.
CPU containers set `NVIDIA_VISIBLE_DEVICES=void`, including when using CUDA images.
