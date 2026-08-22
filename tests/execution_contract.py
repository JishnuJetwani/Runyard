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


def verify_download(artifact):
    headers = {"Authorization": "Bearer " + os.environ["RUNYARD_OWNER_TOKEN"]}
    url = os.environ.get("RUNYARD_URL", "http://127.0.0.1:8080")
    req = urllib.request.Request(url + "/v1/artifacts/" + artifact["id"] + "/download", headers=headers)
    with urllib.request.urlopen(req, timeout=15) as response:
        data = response.read()
    assert hashlib.sha256(data).hexdigest() == artifact["sha256"]
    assert len(data) == artifact["size"]


def runtime_configuration(run, backend):
    name = "runyard-" + run["active_attempt"]
    expected = run["spec"]["resources"]
    if backend == "docker":
        host = json.loads(command("docker", "inspect", "--format", "{{json .HostConfig}}", name))
        assert host["NanoCpus"] == expected["cpu_millis"] * 1_000_000
        assert host["Memory"] == expected["memory_mib"] * 1024 * 1024
        return {key: host[key] for key in ("NanoCpus", "Memory", "MemorySwap", "PidsLimit", "CapDrop")}
    job = json.loads(command("kubectl", "--context", "kind-runyard", "-n", "runyard", "get", "job", name, "-o", "json"))
    pod = job["spec"]["template"]["spec"]
    resources = pod["containers"][0]["resources"]
    assert resources["requests"] == resources["limits"]
    assert resources["limits"]["cpu"] == str(expected["cpu_millis"]) + "m"
    assert resources["limits"]["memory"] == str(expected["memory_mib"]) + "Mi"
    assert job["spec"]["backoffLimit"] == 0
    assert pod["restartPolicy"] == "Never" and not pod["automountServiceAccountToken"]
    return {"resources": resources, "backoffLimit": 0, "restartPolicy": "Never"}


def verify_policies(base):
    scenarios = {
        "application_failure": dict(base, parameters={"mode": "fail"}),
        "application_retry": dict(base, parameters={"mode": "fail_once"},
                                  retry={"max_attempts": 2, "retry_exit": True}),
        "timeout": dict(base, parameters={"mode": "hang"}, timeout_seconds=3),
        "log_flood": dict(base, parameters={"mode": "noisy", "steps": 20, "delay_ms": 0}),
    }
    submitted = {name: request("/v1/runs", spec, str(uuid.uuid4())) for name, spec in scenarios.items()}
    result = {}
    for name, run in submitted.items():
        def finished():
            current = request("/v1/runs/" + run["id"])
            return current if terminal(current) else None
        completed = wait_for(finished)
        attempts = request("/v1/runs/" + run["id"] + "/attempts")["items"]
        result[name] = {"run": completed, "attempts": attempts}
    assert result["application_failure"]["run"]["status"] == "FAILED"
    assert len(result["application_failure"]["attempts"]) == 1
    assert result["application_failure"]["attempts"][0]["exit_code"] == 7
    assert result["application_retry"]["run"]["status"] == "SUCCEEDED"
    assert [a["status"] for a in result["application_retry"]["attempts"]] == ["FAILED", "SUCCEEDED"]
    retried = result["application_retry"]
    history_path = "/v1/runs/" + retried["run"]["id"] + "/artifacts"
    previous = request(history_path + "?attempt=" + retried["attempts"][0]["id"])["items"]
    assert any(artifact["path"] == "result.json" for artifact in previous)
    assert all(artifact["attempt_id"] == retried["attempts"][1]["id"]
               for artifact in request(history_path)["items"])
    for artifact in previous:
        verify_download(artifact)
    assert result["timeout"]["run"]["status"] == "FAILED"
    assert len(result["timeout"]["attempts"]) == 1
    assert result["timeout"]["attempts"][0]["reason"] == "TIMEOUT"
    assert result["log_flood"]["run"]["status"] == "SUCCEEDED"
    artifacts = request("/v1/runs/" + result["log_flood"]["run"]["id"] + "/artifacts")["items"]
    stdout = next(a for a in artifacts if a["path"] == "_runyard/stdout.log")
    assert stdout["size"] >= 4_000_000
    verify_download(stdout)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", choices=["docker", "kubernetes"], required=True)
    parser.add_argument("--image", required=True, help="immutable fixture image digest")
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--output", default=".local/acceptance.json")
    args = parser.parse_args()
    if not 2 <= args.count <= 1000:
        parser.error("count must be 2..1000 for two interrupted attempts")
    base = {"name": "acceptance", "image": args.image, "command": ["/usr/local/bin/runyard-fixture"],
            "parameters": {"steps": 5, "delay_ms": 1500, "mode": "success"},
            "resources": {"cpu_millis": 250, "memory_mib": 128}, "priority": 8}
    key = str(uuid.uuid4())
    sweep = request("/v1/sweeps", {"base": base, "grid": {"seed": list(range(args.count))}}, key)
    assert request("/v1/sweeps", {"base": base, "grid": {"seed": list(range(args.count))}}, key)["id"] == sweep["id"]
    ids = sweep["run_ids"]
    started = time.time()
    def active_pair():
        active = []
        for run_id in ids:
            run = request("/v1/runs/" + run_id)
            if run["status"] == "RUNNING":
                active.append(run)
                if len(active) == 2:
                    return active
        return None
    active = wait_for(active_pair)
    interrupted = [run["id"] for run in active]
    runtime_settings = runtime_configuration(active[-1], args.backend)
    fault_at = time.time()
    if args.backend == "docker":
        command("docker", "kill", *("runyard-" + run["active_attempt"] for run in active))
        command("docker", "compose", "restart", "server", "agent-1")
    else:
        command("kubectl", "--context", "kind-runyard", "-n", "runyard", "delete", "pod", "-l",
                "runyard.attempt in (" + ",".join(run["active_attempt"] for run in active) + ")", "--wait=false")
        command("kubectl", "--context", "kind-runyard", "-n", "runyard", "rollout", "restart", "deployment/server")
    wait_for(lambda: request("/v1/runs/" + interrupted[0]), 90)
    # Cancellation is tested on a separate hanging workload to avoid completion races.
    hanging = dict(base, parameters={"mode": "hang"}, priority=9)
    cancelled = request("/v1/runs", hanging, str(uuid.uuid4()))
    wait_for(lambda: request("/v1/runs/" + cancelled["id"])["status"] == "RUNNING")
    assert request("/v1/runs/" + cancelled["id"] + "/cancel", {})["status"] == "CANCELLED"
    runs = wait_for(lambda: (rows if all(terminal(r) for r in rows) else None)
                    if (rows := [request("/v1/runs/" + i) for i in ids]) else None, 600)
    assert all(r["status"] == "SUCCEEDED" for r in runs), runs
    histories = {r["id"]: request("/v1/runs/" + r["id"] + "/attempts")["items"] for r in runs}
    assert all(len(histories[run_id]) >= 2 for run_id in interrupted)
    for run in runs:
        attempts = histories[run["id"]]
        assert len({a["id"] for a in attempts}) == len(attempts)
        assert sum(a["status"] == "SUCCEEDED" for a in attempts) == 1
        assert request("/v1/runs/" + run["id"] + "/metrics")["items"]
        assert request("/v1/runs/" + run["id"] + "/logs")["items"]
        artifacts = request("/v1/runs/" + run["id"] + "/artifacts")["items"]
        assert any(a["path"] == "result.json" for a in artifacts)
        for artifact in artifacts:
            verify_download(artifact)
    wait_for(lambda: all(a["cleanup_status"] == "DONE" for a in
                        request("/v1/runs/" + cancelled["id"] + "/attempts")["items"]))
    policies = verify_policies(base)
    result = {"backend": args.backend, "count": args.count, "elapsed_seconds": time.time() - started,
              "interrupted_runs": interrupted, "fault_at_unix": fault_at, "sweep": sweep, "runs": runs, "attempts": histories,
              "cancelled_run": request("/v1/runs/" + cancelled["id"]),
              "cancelled_attempts": request("/v1/runs/" + cancelled["id"] + "/attempts")["items"],
              "policy_scenarios": policies,
              "runtime_settings": runtime_settings,
              "hardware_note": "Logical workers share the local Docker VM and host."}
    pathlib.Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(args.output).write_text(json.dumps(result, indent=2))
    print(f"{args.backend} acceptance passed for {args.count} runs; evidence: {args.output}")


if __name__ == "__main__":
    main()
