#!/usr/bin/env python3
"""Exercise GPU ownership, outputs, cancellation, and recovery on an existing deployment."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend", choices=("docker", "kubernetes"), required=True)
    parser.add_argument("--image", required=True)
    parser.add_argument("--cli", default="runyard")
    parser.add_argument("--kube-context")
    parser.add_argument("--namespace", default="runyard")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--output", default=".local/gpu-acceptance.json")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:/-]*@sha256:[a-f0-9]{64}", args.image):
        parser.error("--image must be an immutable image digest")
    if args.backend == "kubernetes" and not args.kube_context:
        parser.error("--kube-context is required for Kubernetes interruption")
    if args.timeout < 60:
        parser.error("--timeout must be at least 60 seconds")

    def raw(*command):
        return subprocess.check_output([args.cli, "--json", *command], text=True, timeout=45)

    def call(*command):
        return json.loads(raw(*command))

    capacity = call("capacity")
    if capacity["backend"] != args.backend:
        raise RuntimeError("The coordinator execution mode differs from --backend")
    if not capacity["gpu"]["fresh"] or capacity["gpu"]["available_estimate"] != 1 or capacity["gpu"]["reserved"] != 0:
        raise RuntimeError("Use an otherwise idle Runyard deployment with exactly one available GPU")

    created = []
    evidence = {"backend": args.backend, "image": args.image, "capacity": capacity, "runs": []}
    deadline = time.monotonic() + args.timeout

    def wait(run, predicate):
        while time.monotonic() < deadline:
            current = call("runs", "get", run)
            attempts = call("runs", "get", run, "--attempts")["items"]
            if predicate(current, attempts):
                return current, attempts
            if current["status"] == "FAILED":
                raise RuntimeError(f"Run failed: {current['id']} {attempts}")
            time.sleep(0.2)
        raise TimeoutError(f"GPU acceptance deadline exceeded for {run}")

    with tempfile.TemporaryDirectory(prefix="runyard-gpu-") as directory:
        root = Path(directory)

        def submit(name, epochs):
            spec = {"name": name, "image": args.image, "command": ["python", "/opt/experiment/train.py"],
                    "resources": {"cpu_millis": 4000, "memory_mib": 8192, "gpu_count": 1},
                    "parameters": {"epochs": epochs, "seed": 7, "batch_size": 32},
                    "timeout_seconds": min(args.timeout, 604800)}
            path = root / (name + ".json")
            path.write_text(json.dumps(spec))
            run = call("submit", str(path))["id"]
            created.append(run)
            return run

        try:
            blocker = submit("gpu-queue-owner", 1000)
            wait(blocker, lambda run, _: run["status"] == "RUNNING")
            follower = submit("gpu-queued-training", 2)
            time.sleep(1)
            first = call("runs", "get", blocker)
            second = call("runs", "get", follower)
            if first["status"] != "RUNNING" or second["status"] not in ("QUEUED", "STARTING"):
                raise RuntimeError("Competing experiments did not preserve exclusive GPU placement")
            call("cancel", blocker)
            cancelled, cancelled_attempts = wait(blocker, lambda run, attempts: run["status"] == "CANCELLED" and all(a["cleanup_status"] == "DONE" for a in attempts))
            result, history = wait(follower, lambda run, attempts: run["status"] == "SUCCEEDED" and all(a["cleanup_status"] == "DONE" for a in attempts))
            artifacts = call("artifacts", "list", follower)["items"]
            summary_artifact = next(a for a in artifacts if a["path"] == "summary.json")
            raw("artifacts", "download", summary_artifact["id"], "--output", str(root / "summary.json"))
            summary = json.loads((root / "summary.json").read_text())
            if summary["device"]["type"] != "cuda" or summary["device"]["visible_count"] != 1:
                raise RuntimeError("Workload did not observe its one-GPU allocation")
            model = next(a for a in artifacts if a["path"] == "model.pt")
            raw("artifacts", "download", model["id"], "--output", str(root / "model.pt"))
            metrics = [json.loads(line) for line in raw("metrics", follower).splitlines()]
            if {m["name"] for m in metrics} != {"loss", "accuracy"} or not all(math.isfinite(m["value"]) for m in metrics):
                raise RuntimeError("Training metrics are incomplete")
            if "epoch=" not in raw("logs", follower):
                raise RuntimeError("Training logs were not retained")
            retry = submit("gpu-recovery", 1000)
            _, active = wait(retry, lambda run, _: run["status"] == "RUNNING")
            attempt = active[-1]["id"]
            if args.backend == "docker":
                container = json.loads(subprocess.check_output(["docker", "inspect", "runyard-" + attempt], text=True, timeout=15))[0]
                if container["Config"]["Labels"].get("runyard.attempt") != attempt:
                    raise RuntimeError("Container identity mismatch")
                subprocess.run(["docker", "kill", container["Id"]], check=True, timeout=20)
            else:
                command = ["kubectl", "--context", args.kube_context, "-n", args.namespace]
                pods = json.loads(subprocess.check_output(command + ["get", "pods", "-l", "runyard.attempt=" + attempt, "-o", "json"], text=True, timeout=20))["items"]
                if not pods:
                    raise RuntimeError("Attempt Pod was not found")
                for pod in pods:
                    subprocess.run(command + ["delete", "pod", pod["metadata"]["name"], "--wait=true", "--timeout=30s"], check=True, timeout=40)
            recovered, attempts = wait(retry, lambda run, attempts: run["status"] == "SUCCEEDED" and len(attempts) >= 2 and all(a["cleanup_status"] == "DONE" for a in attempts))
            if attempts[0]["status"] != "FAILED" or attempts[-1]["status"] != "SUCCEEDED":
                raise RuntimeError("Retry history is inconsistent")
            evidence.update({"summary": summary, "runs": [cancelled, result, recovered],
                             "attempts": [cancelled_attempts, history, attempts], "metrics": metrics,
                             "completed_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})
            destination = Path(args.output)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(json.dumps(evidence, indent=2) + "\n")
            print(f"GPU acceptance passed; evidence: {destination}")
        finally:
            for run in created:
                try:
                    current = call("runs", "get", run)
                    if current["status"] not in ("SUCCEEDED", "FAILED", "CANCELLED"):
                        call("cancel", run)
                except (subprocess.SubprocessError, ValueError) as error:
                    print(f"Cleanup request for {run}: {error}")


if __name__ == "__main__":
    main()
