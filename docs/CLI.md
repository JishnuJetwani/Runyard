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
runyard capacity
runyard capacity --json --limit 20
runyard workers drain WORKER_ID
runyard workers drain WORKER_ID --resume
runyard cancel RUN_ID
runyard rerun RUN_ID
```

Use `--json` for machine-readable output. Metrics and logs in JSON format print
one object per line. Add `--attempt` to inspect an earlier attempt. Following a
run switches to each new attempt; following a specific attempt stops when it is
replaced or the run finishes.

Downloads check SHA-256 before publishing the file and never overwrite an existing
destination. Submit, sweep, and rerun print their request key to stderr. Reuse it
with `--request-id` if the command is interrupted.

Capacity totals cover the full deployment. Use `--limit` and `--after` to page
through workers or nodes. Run inspection shows GPU requests; `--attempts` adds
Docker device allocations and release times, or the Kubernetes node. Worker
details show devices, reservations, availability, and inventory age.
See [GPU operations](GPU.md).
