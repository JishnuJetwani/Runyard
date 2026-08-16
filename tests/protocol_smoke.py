"""Native network/runner integration against an empty disposable PostgreSQL database."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import ssl
import urllib.request
import uuid

root = pathlib.Path(__file__).resolve().parents[1]
bin_dir = pathlib.Path(sys.argv[1]).resolve()
tls = '--tls' in sys.argv[2:]
scheme = 'https' if tls else 'http'
ca = root / '.secrets/tls/ca.crt'
ssl_context = ssl.create_default_context(cafile=str(ca)) if tls else None
with tempfile.TemporaryDirectory(prefix="runyard-protocol-") as tmp:
    subprocess.run([sys.executable, "-m", "grpc_tools.protoc", "-I", str(root / "proto"),
                    "--python_out=" + tmp, "--grpc_python_out=" + tmp, str(root / "proto/runyard.proto")], check=True)
    sys.path.insert(0, tmp)
    import grpc
    import runyard_pb2 as p
    import runyard_pb2_grpc as g
    env = dict(os.environ, RUNYARD_DATABASE_URL=os.environ["RUNYARD_TEST_DATABASE"], RUNYARD_PROFILE="development",
               RUNYARD_OWNER_TOKEN="protocol-owner-token", RUNYARD_WORKER_TOKEN="protocol-worker-token",
               RUNYARD_SIGNING_KEY="protocol-signing-key-long-enough-32", RUNYARD_HTTP_PORT="18080", RUNYARD_GRPC_PORT="19090",
               RUNYARD_ARTIFACT_ROOT=tmp + "/artifacts")
    if tls:
        env.update(RUNYARD_PROFILE='production', RUNYARD_TLS_CERT=str(root/'.secrets/tls/tls.crt'),
                   RUNYARD_TLS_KEY=str(root/'.secrets/tls/tls.key'), RUNYARD_TLS_CA=str(ca))
    subprocess.run([str(bin_dir / "runyard-server"), "migrate"], env=env, check=True)
    log = open(root / ".local/protocol-smoke.log", "w")
    server = subprocess.Popen([str(bin_dir / "runyard-server"), "serve"], env=env, stdout=log, stderr=log)
    processes = []
    try:
        channel = grpc.secure_channel("localhost:19090", grpc.ssl_channel_credentials(ca.read_bytes())) if tls else grpc.insecure_channel("localhost:19090")
        grpc.channel_ready_future(channel).result(timeout=20)
        agent, runner = g.AgentServiceStub(channel), g.AttemptServiceStub(channel)
        auth = [("authorization", "Bearer " + env["RUNYARD_WORKER_TOKEN"])]
        worker = p.WorkerIdentity(id="protocol-worker", session=str(uuid.uuid4()))
        agent.Register(p.RegisterRequest(worker=worker, cpu_millis=1000, memory_mib=512), metadata=auth, timeout=5)
        def api(path, payload=None):
            req = urllib.request.Request(scheme + "://localhost:18080" + path, headers={
                "Authorization": "Bearer " + env["RUNYARD_OWNER_TOKEN"], "Content-Type": "application/json",
                "Idempotency-Key": str(uuid.uuid4())}, data=None if payload is None else json.dumps(payload).encode())
            with urllib.request.urlopen(req, timeout=10, context=ssl_context) as response:
                return json.load(response)
        run = api("/v1/runs", {"name": "protocol", "image": "fixture@sha256:" + "a" * 64,
                  "command": [str(bin_dir / "runyard-fixture")], "priority": 9,
                  "parameters": {"steps": 10, "delay_ms": 100}})
        assignment = agent.Poll(worker, metadata=auth, timeout=5).assignment
        assert assignment.run_id == run["id"]
        assert agent.Poll(worker, metadata=auth, timeout=5).assignment.attempt_id == assignment.attempt_id
        runner_env = dict(env, RUNYARD_COORDINATOR="localhost:19090", RUNYARD_ATTEMPT_ID=assignment.attempt_id,
                          RUNYARD_RUN_ID=run["id"], RUNYARD_GENERATION=str(assignment.generation),
                          RUNYARD_CAPABILITY=assignment.capability, RUNYARD_WORK_ROOT=tmp + "/runner")
        child = subprocess.Popen([str(bin_dir / "runyard-runner")], env=runner_env, stdout=log, stderr=log)
        processes.append(child)
        deadline = time.monotonic() + 30
        while api("/v1/runs/" + run["id"])["status"] == "STARTING" and time.monotonic() < deadline:
            time.sleep(.05)
        duplicate = subprocess.run([str(bin_dir / "runyard-runner")], env=runner_env, stdout=log, stderr=log, timeout=10)
        assert duplicate.returncode != 0
        assert child.wait(timeout=45) == 0
        assert api("/v1/runs/" + run["id"])["status"] == "SUCCEEDED"
        metrics = api("/v1/runs/" + run["id"] + "/metrics")["items"]
        assert len(metrics) == 10, metrics
        artifact = next(a for a in api("/v1/runs/" + run["id"] + "/artifacts")["items"] if a["path"] == "result.json")
        client_env = dict(env, RUNYARD_URL=scheme+"://localhost:18080")
        subprocess.run([str(bin_dir / "runyard"), "artifacts", "download", artifact["id"], "--output", tmp + "/result.json"], env=client_env, check=True)
        assert json.loads(pathlib.Path(tmp + "/result.json").read_text())
        print(("TLS " if tls else "") + "Native protocol passed: replay, duplicate runner fencing, process, telemetry, artifacts, CLI checksum download")
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
            process.wait()
        server.terminate()
        server.wait(timeout=15)
        log.close()
