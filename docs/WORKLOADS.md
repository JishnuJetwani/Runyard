# Packaging experiments

Use a locally built or published runner base image of the target architecture.
For reproducible shared images, pin the `FROM` image by digest too. For example:

```dockerfile
FROM runyard-runner-base:dev
USER root
COPY my-experiment /usr/local/bin/my-experiment
USER 10001:10001
```

The platform supplies the runner entrypoint. The submitted `command` is an argv
array executed directly, without shell expansion. Explicitly submit a shell only
if the workload actually requires one. Images must include every required runtime
library. Source metadata is recorded but does not trigger image builds.

The version-1 specification supports `name`, `image`, `command`, `parameters`,
`environment`, `labels`, `resources`, `timeout_seconds`, `priority`, `retry`, and
`source`. See OpenAPI for field limits. Defaults are 1 CPU, 512 MiB, a 30-minute
timeout, priority 0, and three total attempts. Priority ranges from 0 to 9, with
higher values first. CPU and memory limits include the runner. Environment values
must be non-secret and cannot use the `RUNYARD_` prefix.

The runner writes scalar parameters to `RUNYARD_PARAMETERS_PATH`. The child writes
append-only metric JSONL to `RUNYARD_METRICS_PATH` and regular artifacts below
`RUNYARD_OUTPUT_DIR`. Metric steps must be nonnegative integers and values finite
numbers. Flush complete newline-terminated records. Malformed records generate
visible notices; trailing unterminated records are not samples.

stdout/stderr are captured as telemetry and archived under `_runyard/stdout.log`
and `_runyard/stderr.log`. That artifact namespace is reserved. Archives are capped
at 100 MiB combined. Telemetry buffering is capped at 4 MiB and reports dropped
records explicitly. A line over 64 KiB is rejected by the metric reader; it accepts
at most 100,000 metric-file records per attempt. Each artifact is limited to 256 MiB,
with at most 1000 files and 1 GiB published per attempt. Symlinks and unsafe paths
are rejected. Unacknowledged output can be lost when a machine disappears.

A sweep file contains `base` (a normal specification) and `grid` (parameter names
to arrays of scalar values). The Cartesian product becomes ordinary immutable
runs atomically. The maximum is 1000 runs and an estimated 16 MiB expanded spec
budget. Use `runyard sweep file.json`, then `runyard sweeps get SWEEP_ID`.

`retry.retry_exit=true` enables retries for nonzero application exits.
`retry.retry_timeout=true` enables timeout retries. Retries run from the original
specification and do not resume checkpoints. A manual `rerun` creates a new run
linked to its terminal predecessor and preserves the old run's entire history.
