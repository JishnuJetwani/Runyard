"""Black-box API checks against a disposable database supplied by the caller."""
import json
import os
import pathlib
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid

binary = pathlib.Path(sys.argv[1]).resolve()
environment = dict(os.environ, RUNYARD_OWNER_TOKEN="runyard-api-test-owner", RUNYARD_PROFILE="development",
                   RUNYARD_HTTP_PORT="18080", RUNYARD_GRPC_PORT="19090",
                   RUNYARD_WORKER_TOKEN="runyard-api-test-worker", RUNYARD_SIGNING_KEY="runyard-test-signing-key-at-least-32-characters")
environment["RUNYARD_DATABASE_URL"] = os.environ["RUNYARD_TEST_DATABASE"]
base = "http://127.0.0.1:18080"


def request(path, payload=None, key=None, authorized=True):
    headers = {"Content-Type": "application/json"}
    if authorized:
        headers["Authorization"] = "Bearer " + environment["RUNYARD_OWNER_TOKEN"]
    if key:
        headers["Idempotency-Key"] = key
    req = urllib.request.Request(base + path, data=json.dumps(payload).encode() if payload is not None else None,
                                 headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            return response.status, json.load(response)
    except urllib.error.HTTPError as error:
        return error.code, json.load(error)


def start(log):
    process = subprocess.Popen([str(binary), "serve"], env=environment, stdout=log, stderr=log)
    for _ in range(100):
        if process.poll() is not None:
            raise RuntimeError("coordinator exited during startup")
        try:
            if request("/health/ready")[0] == 200:
                return process
        except OSError:
            pass
        time.sleep(0.1)
    process.terminate()
    process.wait(timeout=10)
    raise RuntimeError("coordinator readiness deadline exceeded")


def stop(process):
    process.terminate()
    process.wait(timeout=10)


subprocess.run([str(binary), "migrate"], env=environment, check=True)
pathlib.Path(".local").mkdir(exist_ok=True)
with open(".local/api-smoke.log", "w") as log:
    process = start(log)
    try:
        assert request("/v1/runs", authorized=False)[0] == 401
        spec = {"name": "api-smoke", "image": "fixture@sha256:" + "a" * 64, "command": ["true"]}
        key = str(uuid.uuid4())
        status, run = request("/v1/runs", spec, key)
        assert status == 200, run
        assert request("/v1/runs", spec, key)[1]["id"] == run["id"]
        assert request("/v1/runs", dict(spec, name="changed"), key)[0] == 409
        assert request("/v1/runs", dict(spec, image="mutable:latest"), str(uuid.uuid4()))[0] == 400
        assert request("/v1/runs?limit=500")[0] == 400
        assert request("/v1/runs/" + run["id"] + "/events")[1]["items"][0]["sequence"] == 1
        stop(process)
        process = start(log)
        assert request("/v1/runs/" + run["id"])[1]["status"] == "QUEUED"
        if len(sys.argv) > 2:
            cli = pathlib.Path(sys.argv[2]).resolve()
            client_env = dict(environment, RUNYARD_URL=base)
            result = subprocess.check_output([str(cli), "--json", "runs", "get", run["id"]], env=client_env)
            assert json.loads(result)["id"] == run["id"]
            spec_file = pathlib.Path(".local/cli-smoke.json")
            spec_file.write_text(json.dumps(spec))
            result = subprocess.check_output([str(cli), "--json", "submit", str(spec_file)], env=client_env)
            assert json.loads(result)["spec"]["name"] == spec["name"]
            print("CLI smoke passed: submission and inspection through public HTTP")
        print("API smoke passed: auth, validation, idempotency, event cursor, restart durability")
    finally:
        if process.poll() is None:
            stop(process)
