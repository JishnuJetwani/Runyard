"""Black-box Docker/kind acceptance. Runtime CLI calls inject faults, never execute production work."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import time
import urllib.error
import urllib.request
import uuid


def request(path, payload=None, key=None):
    headers = {"Authorization": "Bearer " + os.environ["RUNYARD_OWNER_TOKEN"], "Content-Type": "application/json"}
    if key:
        headers["Idempotency-Key"] = key
    req = urllib.request.Request(os.environ.get("RUNYARD_URL", "http://127.0.0.1:8080") + path,
                                 data=None if payload is None else json.dumps(payload).encode(), headers=headers)
    with urllib.request.urlopen(req, timeout=15) as response:
        return json.load(response)


def wait_for(predicate, timeout=180):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            result = predicate()
            if result:
                return result
        except (OSError, urllib.error.HTTPError):
            pass
        time.sleep(1)
    raise AssertionError("acceptance deadline exceeded")


def terminal(run):
    return run["status"] in ("SUCCEEDED", "FAILED", "CANCELLED")


def command(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", choices=["docker", "kubernetes"], required=True)
    parser.add_argument("--image", required=True, help="immutable fixture image digest")
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--output", default=".local/acceptance.json")
    args = parser.parse_args()
    base = {"name": "acceptance", "image": args.image, "command": ["/usr/local/bin/runyard-fixture"],
            "parameters": {"steps": 5, "delay_ms": 150, "mode": "success"},
            "resources": {"cpu_millis": 250, "memory_mib": 128}, "priority": 8}
    key = str(uuid.uuid4())
    sweep = request("/v1/sweeps", {"base": base, "grid": {"seed": list(range(args.count))}}, key)
    assert request("/v1/sweeps", {"base": base, "grid": {"seed": list(range(args.count))}}, key)["id"] == sweep["id"]
    ids = sweep["run_ids"]
    started = time.time()
    active = wait_for(lambda: next((r for r in (request("/v1/runs/" + i) for i in ids)
                                    if r["status"] == "RUNNING"), None))
    interrupted = active["id"]
    if args.backend == "docker":
        command("docker", "kill", "runyard-" + active["active_attempt"])
        command("docker", "compose", "restart", "server", "agent-1")
    else:
        command("kubectl", "--context", "kind-runyard", "-n", "runyard", "delete", "pod", "-l",
                "runyard.attempt=" + active["active_attempt"], "--wait=false")
        command("kubectl", "--context", "kind-runyard", "-n", "runyard", "rollout", "restart", "deployment/server")
    wait_for(lambda: request("/v1/runs/" + interrupted), 90)
    # Cancellation is tested on a separate hanging workload to avoid completion races.
    hanging = dict(base, parameters={"mode": "hang"}, priority=9)
    cancelled = request("/v1/runs", hanging, str(uuid.uuid4()))
    wait_for(lambda: request("/v1/runs/" + cancelled["id"])["status"] == "RUNNING")
    assert request("/v1/runs/" + cancelled["id"] + "/cancel", {})["status"] == "CANCELLED"
    runs = wait_for(lambda: (rows if all(terminal(r) for r in rows) else None)
                    if (rows := [request("/v1/runs/" + i) for i in ids]) else None, 600)
    assert all(r["status"] == "SUCCEEDED" for r in runs), runs
    histories = {r["id"]: request("/v1/runs/" + r["id"] + "/attempts")["items"] for r in runs}
    assert len(histories[interrupted]) >= 2
    for run in runs:
        attempts = histories[run["id"]]
        assert len({a["id"] for a in attempts}) == len(attempts)
        assert sum(a["status"] == "SUCCEEDED" for a in attempts) == 1
        assert request("/v1/runs/" + run["id"] + "/metrics")["items"]
        assert request("/v1/runs/" + run["id"] + "/logs")["items"]
        artifacts = request("/v1/runs/" + run["id"] + "/artifacts")["items"]
        assert any(a["path"] == "result.json" for a in artifacts)
    wait_for(lambda: all(a["cleanup_status"] == "DONE" for a in
                        request("/v1/runs/" + cancelled["id"] + "/attempts")["items"]))
    result = {"backend": args.backend, "count": args.count, "elapsed_seconds": time.time() - started,
              "interrupted_run": interrupted, "sweep": sweep, "runs": runs, "attempts": histories,
              "hardware_note": "Logical workers share the local Docker VM and host."}
    pathlib.Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(args.output).write_text(json.dumps(result, indent=2))
    print(f"{args.backend} acceptance passed for {args.count} runs; evidence: {args.output}")


if __name__ == "__main__":
    main()
