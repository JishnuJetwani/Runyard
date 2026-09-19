# Install Runyard locally

The `./runyard` launcher manages the Compose deployment. It runs on
macOS or Linux and needs Python 3.9+, Docker with Linux containers, and Compose
2.20+. Docker Desktop includes Compose. Allow roughly 8 GiB of Docker memory for
the stack and small experiments. Larger workloads need more host capacity.
You do not need a local C++ compiler, Node, PostgreSQL, or Python packages.

## Start and inspect your first experiment

From the repository root:

```sh
./runyard up
```

The launcher checks Docker, preserves or creates `.env` credentials, prepares
images, applies numbered migrations, and starts the dashboard and three workers.
It waits for the services to be ready before submitting the example. Missing
images are built with Docker. The first build can take a while; later builds
reuse the cache. Run `./runyard up --build` after changing the code.

Open `http://127.0.0.1:3000`. Obtain the owner key with `./runyard key` and paste
it into **Connection settings**. On macOS, `./runyard key | pbcopy` copies it
without displaying it. Normal setup/status output does not print credentials.

The example runs a CPU workload, writes five metric samples and logs,
and creates `result.json`. Setup downloads the result and verifies its size,
SHA-256 checksum, and input seed. It prints a link to the completed run. Inspect
its Metrics, Logs, and Artifacts tabs, or choose Rerun to create another run.

Files in `.local/install/`:

| File | Purpose |
|---|---|
| `example.json` | Immutable specification, also importable in New experiment |
| `example-result.json` | Run ID, image digest, and verification summary |
| `result.json` | Downloaded experiment output |
| `images.json` | Selected registry/version when using published images |

Running `up` again reuses the example's request key, so a lost response does not
create a duplicate run. `./runyard example` checks the result again;
`up --skip-example` starts services without submitting a run.
If you cancelled the example, use the dashboard's Rerun action.

## Published images

Once the repository's image release is published:

```sh
./runyard up --image-root ghcr.io/your-user/runyard --version v0.1.0
```

Replace the registry root and tag with the published release. The launcher pulls
server, agent, runner-base, fixture, CLI, and UI images for the host architecture.
It saves that selection for later `up`, `example`, and `cli` commands.
If the release is unavailable, setup fails. `up --build` switches back to source
builds.

To publish arm64 and amd64 images, run the **release-images** workflow with a
version such as `v0.1.0` and `publish=true`. Make the six GHCR packages public for
anonymous installation, or log Docker into the private registry first.

## Everyday commands

```sh
./runyard doctor                  # Docker/Compose prerequisites
./runyard status                  # Service state and API readiness
./runyard logs server --follow    # Recent logs, then follow
./runyard cli runs list           # Full C++ CLI, against this installation
./runyard cli capacity --json
./runyard stop                    # Stop services and local experiment containers
./runyard up                      # Restart with existing history and artifacts
```

The stop command stops agents first, then experiment containers identified by
both the Runyard label and this installation's network. It does not remove
volumes or containers belonging to other installations. Interrupted experiments
remain recorded and may retry according to their lease/retry policy on restart.
Remote workers have a separate lifecycle; see [worker setup](WORKERS.md).

## Configuration and troubleshooting

Defaults bind HTTP 8080, gRPC 9090, dashboard 3000, and registry 5001 to loopback.
Set `RUNYARD_LOCAL_HTTP_PORT`, `RUNYARD_LOCAL_GRPC_PORT`, `RUNYARD_LOCAL_UI_PORT`,
or `RUNYARD_LOCAL_REGISTRY_PORT` in `.env` before starting to change host ports.
Internal service ports stay the same. The local profile uses plaintext;
use an SSH tunnel or TLS for connections between machines.

- **Docker not reachable:** start Docker Desktop/the daemon, then run `doctor`.
- **Port already allocated:** stop the conflicting service or change its local
  port setting. Run `up` again; data and credentials are preserved.
- **Image pull fails:** check the release tag, package visibility, and Docker
  registry login. For a source checkout, `up --build` builds locally.
- **Readiness or example timeout:** inspect `logs server` and `logs agent-1`.
  Migrations must finish and all three local workers must be available and
  undrained. Resume a drained worker through the dashboard's Capacity page.
- **Dashboard returns 401:** use the key from this installation's `key` command.
  Keep the original `.env` with its database; replacing keys disconnects agents.

For isolated checks, `RUNYARD_ENV_FILE` selects another configuration/credential
file, and `RUNYARD_INSTALL_STATE` selects another result directory. Put a unique
`RUNYARD_PROJECT_NAME` and unused local ports in that file. The launcher assigns
distinct worker IDs for the project. The installer tests use these options.

Verification commands, after local images exist:

```sh
python3 tests/install_test.py
python3 tests/install_smoke.py
```

The smoke test creates a fresh, isolated database, verifies repeat installation,
the first experiment, CLI access, active-run shutdown, and persistence after
restart, then removes only its own resources. It does not rebuild images.
