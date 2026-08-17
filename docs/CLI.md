# Command-line workflow

Set `RUNYARD_URL`, `RUNYARD_OWNER_TOKEN`, and (for a private CA) `RUNYARD_TLS_CA`.
Plain HTTP is accepted only with `RUNYARD_PROFILE=development`.
Run `runyard --help` for commands and command-specific `--help` for options.

```
runyard submit experiment.json
runyard sweep sweep.json
runyard runs list --status RUNNING
runyard runs get RUN_ID --attempts
runyard runs get RUN_ID --events
runyard logs RUN_ID --follow
runyard metrics RUN_ID --format csv > metrics.csv
runyard artifacts list RUN_ID
runyard artifacts download ARTIFACT_ID --output result.json
runyard workers list
runyard workers drain WORKER_ID
runyard workers drain WORKER_ID --resume
runyard cancel RUN_ID
runyard rerun RUN_ID
```

`--json` selects machine-readable responses. Metrics in JSON format and logs with
`--json` emit one JSON object per line for bounded-memory exports. Add `--attempt`
to inspect historical attempt output. Following a run switches to each new attempt;
following an explicit attempt stops when it is replaced or the run becomes terminal.
Downloads verify SHA-256 and publish the destination atomically; existing files
are never overwritten. Submit, sweep, and rerun display the idempotency key on
stderr; `--request-id` can reuse that key after an interrupted invocation.
