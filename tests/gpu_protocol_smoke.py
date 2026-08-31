"""Exercise durable GPU ownership through the public REST and gRPC boundaries."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import urllib.request
import uuid


def main():
    root = pathlib.Path(__file__).resolve().parents[1]
    binaries = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="runyard-gpu-protocol-") as tmp:
        subprocess.run([sys.executable, "-m", "grpc_tools.protoc", "-I", str(root / "proto"),
                        "--python_out=" + tmp, "--grpc_python_out=" + tmp,
                        str(root / "proto/runyard.proto")], check=True)
        sys.path.insert(0, tmp)
        import grpc
        import runyard_pb2 as p
        import runyard_pb2_grpc as g

        env = dict(os.environ, RUNYARD_DATABASE_URL=os.environ["RUNYARD_TEST_DATABASE"],
                   RUNYARD_PROFILE="development", RUNYARD_OWNER_TOKEN="gpu-protocol-owner-token",
                   RUNYARD_WORKER_TOKEN="gpu-protocol-worker-token", RUNYARD_HTTP_PORT="18080",
                   RUNYARD_GRPC_PORT="19090", RUNYARD_SIGNING_KEY="gpu-protocol-signing-key-long-enough-32",
                   RUNYARD_ARTIFACT_ROOT=tmp + "/artifacts")
        subprocess.run([str(binaries / "runyard-server"), "migrate"], env=env, check=True)
        root.joinpath(".local").mkdir(exist_ok=True)
        log = open(root / ".local/gpu-protocol-smoke.log", "w")
        server = None
        channel = None
        auth = [("authorization", "Bearer " + env["RUNYARD_WORKER_TOKEN"])]

        def api(path, payload=None):
            request = urllib.request.Request("http://localhost:18080/v1/" + path, headers={
                "Authorization": "Bearer " + env["RUNYARD_OWNER_TOKEN"],
                "Content-Type": "application/json", "Idempotency-Key": str(uuid.uuid4())},
                data=None if payload is None else json.dumps(payload).encode())
            with urllib.request.urlopen(request, timeout=10) as response:
                return json.load(response)

        def start_server():
            nonlocal server, channel
            server = subprocess.Popen([str(binaries / "runyard-server"), "serve"],
                                      env=env, stdout=log, stderr=log)
            channel = grpc.insecure_channel("localhost:19090")
            grpc.channel_ready_future(channel).result(timeout=20)
            return g.AgentServiceStub(channel), g.AttemptServiceStub(channel)

        def rejected(call):
            try:
                call()
            except grpc.RpcError as error:
                assert error.code() in (grpc.StatusCode.ABORTED, grpc.StatusCode.FAILED_PRECONDITION,
                                        grpc.StatusCode.PERMISSION_DENIED, grpc.StatusCode.ALREADY_EXISTS), error
            else:
                raise AssertionError("stale or conflicting operation was accepted")

        try:
            agent, runner = start_server()
            worker = p.WorkerIdentity(id="gpu-protocol", session=str(uuid.uuid4()))
            agent.Register(p.RegisterRequest(worker=worker, cpu_millis=4000, memory_mib=4096,
                                             engine_id="engine", gpu_capable=True), metadata=auth, timeout=5)
            device = p.GpuDevice(uuid="GPU-protocol", name="Protocol device", memory_mib=24576, eligible=True)

            def inventory(sequence, ready=True):
                agent.ReportGpuInventory(p.GpuInventory(worker=worker, sequence=sequence, ready=ready,
                                                         devices=[device] if ready else []), metadata=auth, timeout=5)

            spec = {"name": "gpu-protocol", "image": "fixture@sha256:" + "a" * 64,
                    "command": ["true"], "resources": {"gpu_count": 1}, "priority": 9}
            first = api("runs", spec)
            second = api("runs", spec)
            assert not agent.Poll(worker, metadata=auth, timeout=5).has_work
            agent.Reconcile(p.Inventory(worker=worker), metadata=auth, timeout=5)
            inventory(1)
            cli_env = dict(env, RUNYARD_URL="http://localhost:18080")
            displayed = subprocess.check_output([str(binaries / "runyard"), "workers", "list"], env=cli_env, text=True)
            assert "GPU-protocol" in displayed and "available=1" in displayed and "fresh=true" in displayed
            assigned = agent.Poll(worker, metadata=auth, timeout=5).assignment
            assert assigned.run_id == first["id"]
            assert list(assigned.gpu_uuids) == [device.uuid]
            assert agent.Poll(worker, metadata=auth, timeout=5).assignment == assigned

            # Restart before acknowledging launch: persisted device intent must replay unchanged.
            channel.close()
            server.terminate()
            server.wait(timeout=15)
            agent, runner = start_server()
            agent.Heartbeat(worker, metadata=auth, timeout=5)
            inventory(2)
            replay = agent.Poll(worker, metadata=auth, timeout=5).assignment
            assert replay.attempt_id == assigned.attempt_id
            assert list(replay.gpu_uuids) == [device.uuid]
            agent.ReportRuntime(p.RuntimeReport(worker=worker, attempt_id=assigned.attempt_id,
                                                runtime_id="controlled-runtime"), metadata=auth, timeout=5)
            owner = p.Owner(attempt_id=assigned.attempt_id, generation=assigned.generation, instance_id="first")
            capability = [("authorization", "Bearer " + assigned.capability)]
            runner.Start(owner, metadata=capability, timeout=5)
            duplicate = p.Owner(attempt_id=owner.attempt_id, generation=owner.generation, instance_id="duplicate")
            rejected(lambda: runner.Start(duplicate, metadata=capability, timeout=5))
            inventory(3, False)
            inventory(2)
            assert api("capacity")["gpu"]["available_estimate"] is None
            assert api("capacity")["gpu"]["reserved"] == 1
            assert not agent.Poll(worker, metadata=auth, timeout=5).has_work

            # A new agent session cannot change the device ownership of a live runner.
            previous = p.WorkerIdentity(id=worker.id, session=worker.session)
            worker.session = str(uuid.uuid4())
            agent.Register(p.RegisterRequest(worker=worker, cpu_millis=4000, memory_mib=4096,
                                             engine_id="engine", gpu_capable=True), metadata=auth, timeout=5)
            rejected(lambda: agent.ReportGpuInventory(p.GpuInventory(worker=previous, sequence=99, ready=True,
                                                                       devices=[device]), metadata=auth, timeout=5))
            agent.Reconcile(p.Inventory(worker=worker, attempts=[owner.attempt_id]), metadata=auth, timeout=5)
            inventory(1)
            runner.Heartbeat(owner, metadata=capability, timeout=5)
            assert api("runs/" + first["id"] + "/cancel", {})["status"] == "CANCELLED"
            rejected(lambda: runner.Heartbeat(owner, metadata=capability, timeout=5))
            rejected(lambda: runner.Complete(p.Completion(owner=owner), metadata=capability, timeout=5))
            assert api("capacity")["gpu"]["reserved"] == 1
            assert not agent.Poll(worker, metadata=auth, timeout=5).has_work
            agent.ReportRuntime(p.RuntimeReport(worker=worker, attempt_id=owner.attempt_id, stopped=True),
                                metadata=auth, timeout=5)
            assert api("capacity")["gpu"]["reserved"] == 0
            next_assignment = agent.Poll(worker, metadata=auth, timeout=5).assignment
            assert next_assignment.run_id == second["id"]
            assert list(next_assignment.gpu_uuids) == [device.uuid]
            history = api("runs/" + first["id"] + "/attempts")["items"][0]
            assert history["cleanup_status"] == "DONE"
            assert history["gpu_allocations"][0]["released_at"]
            api("runs/" + second["id"] + "/cancel", {})
            agent.ReportRuntime(p.RuntimeReport(worker=worker, attempt_id=next_assignment.attempt_id, stopped=True),
                                metadata=auth, timeout=5)
            print("GPU protocol passed: restart replay, session fencing, duplicate claim, inventory failure, cancellation, cleanup and reuse")
        finally:
            if channel:
                channel.close()
            if server and server.poll() is None:
                server.terminate()
                server.wait(timeout=15)
            log.close()


if __name__ == "__main__":
    main()
