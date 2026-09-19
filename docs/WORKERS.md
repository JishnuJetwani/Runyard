# Connect another Docker worker

The default installation already starts three logical workers on one Docker
engine. To add compute on another machine, use a Linux host with Docker and a
stable, unique worker ID. Each host advertises its own CPU/memory budget. For
NVIDIA device discovery and GPU profiles, see [GPU operations](GPU.md).

## Connect over SSH

The worker-only Compose file uses a loopback SSH tunnel. Both the agent and its
experiment containers use Linux host networking to reach that tunnel. Workloads
are trusted-owner code; this is not a sandbox for strangers' submissions.

On the new worker, clone the repository and create a tunnel to the machine running
the coordinator (its SSH server must be reachable):

```sh
ssh -NT -o ExitOnForwardFailure=yes -o ServerAliveInterval=30 \
  -L 127.0.0.1:19090:127.0.0.1:9090 user@coordinator-host
```

Keep that session running. Replace the destination port 9090 if the coordinator
uses another local gRPC port. The plaintext development endpoint stays on
loopback; SSH encrypts the connection between hosts. This profile is for a Linux
worker, even if the coordinator runs on a Mac.

In another terminal on the worker, create `.secrets/worker.env` with permissions
0600. Copy only the coordinator's `RUNYARD_WORKER_TOKEN` from its `.env`. Keep
the owner credential, signing key, and database credentials on the coordinator.

```dotenv
RUNYARD_WORKER_ID=lab-worker-1
RUNYARD_WORKER_TOKEN=replace-with-the-coordinators-worker-token
RUNYARD_AGENT_IMAGE=ghcr.io/your-user/runyard/agent:v0.1.0
RUNYARD_CPU_MILLIS=2000
RUNYARD_MEMORY_MIB=4096
```

Use your actual published agent image. The worker ID must be a lowercase Compose
project name and remain stable across restarts; assign a different ID per worker.
Two thousand millicores means two CPUs, and 4096 MiB means 4 GiB. Reserve enough
host capacity for Docker and the OS, and avoid counting the same host capacity
twice across logical workers.

```sh
chmod 600 .secrets/worker.env
docker compose --env-file .secrets/worker.env \
  -f deploy/worker-ssh.compose.yaml up -d
```

The worker should appear as available in the dashboard's Capacity page or
`./runyard cli workers list` on the coordinator. To diagnose registration:

```sh
docker compose --env-file .secrets/worker.env \
  -f deploy/worker-ssh.compose.yaml logs --tail 100 agent
```

Experiment images must also be available by digest from a registry reachable by
the worker's Docker daemon. The published-image setup uses the release fixture
digest. The source-build example's `localhost:5001` registry only exists on the
coordinator host; publish a workload to a shared registry and submit its digest
before running experiments on remote workers. Pull private images on the worker
host after authenticating Docker, since workload credentials are not forwarded.

## Restart and disconnect

Restarting the agent keeps its identity and reconciles existing containers.
Healthy runners continue reporting independently of the agent. To disconnect,
drain the worker in Capacity, wait for its attempts to finish and cleanup to
complete, then stop the worker Compose service and close the SSH tunnel.

For an unattended installation, supervise the SSH tunnel with the host's service
manager, or configure coordinator TLS and worker CA trust as described in
[operations](OPERATIONS.md). The local launcher manages local workers only;
it does not shut down machines connected over SSH.
