#!/usr/bin/env python3
"""Measure GPU allocation RPCs using synthetic worker inventories."""
import argparse
import concurrent.futures
import datetime
import json
import pathlib
import subprocess
import sys
import tempfile
import time
import uuid

from worker_scale import Lab, ROOT, command
from run import summarize


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--workers', type=int, default=12)
    parser.add_argument('--waves', type=int, default=10)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if not 2 <= args.workers <= 24 or not 1 <= args.waves <= 20:
        parser.error('workers 2..24, waves 1..20')
    with tempfile.TemporaryDirectory(prefix='runyard-gpu-allocation-') as generated:
        subprocess.run([sys.executable, '-m', 'grpc_tools.protoc', '-I', str(ROOT / 'proto'),
                        '--python_out=' + generated, '--grpc_python_out=' + generated,
                        str(ROOT / 'proto/runyard.proto')], check=True)
        sys.path.insert(0, generated)
        import grpc
        import runyard_pb2 as p
        import runyard_pb2_grpc as g
        with Lab(0, 18082, 19092) as lab:
            channel = grpc.insecure_channel('localhost:19092')
            grpc.channel_ready_future(channel).result(timeout=15)
            agent, runner = g.AgentServiceStub(channel), g.AttemptServiceStub(channel)
            worker_auth = [('authorization', 'Bearer ' + lab.worker_token)]
            identities = [p.WorkerIdentity(id='synthetic-gpu-' + str(index), session=str(uuid.uuid4()))
                          for index in range(args.workers)]
            devices = [p.GpuDevice(uuid='GPU-' + str(uuid.uuid4()), name='Synthetic protocol device',
                                   memory_mib=24576, eligible=True) for _ in identities]
            def report(index, sequence, ready=True):
                agent.ReportGpuInventory(p.GpuInventory(worker=identities[index], sequence=sequence,
                                         ready=ready, devices=[devices[index]] if ready else []),
                                         metadata=worker_auth, timeout=5)
            def rejected(call):
                try:
                    call()
                except grpc.RpcError as error:
                    assert error.code() in (grpc.StatusCode.FAILED_PRECONDITION,
                                            grpc.StatusCode.PERMISSION_DENIED), error
                    return error.code().name
                raise AssertionError('conflicting GPU ownership was accepted')
            for index, identity in enumerate(identities):
                agent.Register(p.RegisterRequest(worker=identity, cpu_millis=1000, memory_mib=512,
                                                engine_id='synthetic-engine-' + str(index), gpu_capable=True),
                               metadata=worker_auth, timeout=5)
                agent.Reconcile(p.Inventory(worker=identity), metadata=worker_auth, timeout=5)
                report(index, 1)
            duplicate = rejected(lambda: agent.ReportGpuInventory(
                p.GpuInventory(worker=identities[1], sequence=2, ready=True, devices=[devices[0]]),
                metadata=worker_auth, timeout=5))
            result = {'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                      'revision': command('git', 'rev-parse', 'HEAD'), 'worker_count': args.workers,
                      'gpu_hardware_used': False, 'runtime_processes_launched': 0,
                      'mode': 'synthetic GPU inventories and runner RPC clients against real coordinator/PostgreSQL',
                      'duplicate_device_advertisement': duplicate, 'waves': []}
            latencies = []
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as executor:
                for wave in range(args.waves):
                    spec = {'name': 'gpu-allocation-contract', 'image': 'protocol-fixture@sha256:' + 'a' * 64,
                            'command': ['true'], 'resources': {'cpu_millis': 250, 'memory_mib': 128, 'gpu_count': 1}}
                    sweep = lab.api('sweeps', {'base': spec, 'grid': {'seed': list(range(args.workers))}})
                    for index, identity in enumerate(identities):
                        agent.Heartbeat(identity, metadata=worker_auth, timeout=5)
                        report(index, wave * 3 + 3)
                    def poll(index):
                        start = time.perf_counter()
                        reply = agent.Poll(identities[index], metadata=worker_auth, timeout=10)
                        elapsed = time.perf_counter() - start
                        assert reply.has_work
                        assert list(reply.assignment.gpu_uuids) == [devices[index].uuid]
                        return reply.assignment, elapsed
                    assignments = list(executor.map(poll, range(args.workers)))
                    latencies.extend(elapsed for _, elapsed in assignments)
                    assert {a.run_id for a, _ in assignments} == set(sweep['run_ids'])
                    assert len({a.attempt_id for a, _ in assignments}) == args.workers
                    assert len({a.gpu_uuids[0] for a, _ in assignments}) == args.workers
                    assert lab.api('capacity')['gpu']['reserved'] == args.workers
                    for index, (assignment, _) in enumerate(assignments):
                        # A lost launch acknowledgement replays the same allocation.
                        replay = agent.Poll(identities[index], metadata=worker_auth, timeout=5)
                        assert replay.assignment == assignment
                        agent.ReportRuntime(p.RuntimeReport(worker=identities[index],
                                            attempt_id=assignment.attempt_id, runtime_id='synthetic-runtime'),
                                            metadata=worker_auth, timeout=5)
                        auth = [('authorization', 'Bearer ' + assignment.capability)]
                        owner = p.Owner(attempt_id=assignment.attempt_id, generation=assignment.generation,
                                        instance_id=str(uuid.uuid4()))
                        runner.Start(owner, metadata=auth, timeout=5)
                        rejected(lambda: runner.Start(p.Owner(attempt_id=owner.attempt_id,
                                 generation=owner.generation, instance_id='duplicate'), metadata=auth, timeout=5))
                        if index == 0:
                            report(index, wave * 3 + 4, False)
                            assert lab.api('capacity')['gpu']['reserved'] == args.workers
                            lab.api('runs/' + assignment.run_id + '/cancel', {})
                            rejected(lambda: runner.Heartbeat(owner, metadata=auth, timeout=5))
                            report(index, wave * 3 + 5)
                        else:
                            runner.BeginFinalization(owner, metadata=auth, timeout=5)
                            runner.Complete(p.Completion(owner=owner, exit_code=0, final_sequence=0),
                                            metadata=auth, timeout=5)
                    # Terminal results alone cannot release physical device reservations.
                    assert lab.api('capacity')['gpu']['reserved'] == args.workers
                    for index, (assignment, _) in enumerate(assignments):
                        assert not agent.Poll(identities[index], metadata=worker_auth, timeout=5).has_work
                        agent.ReportRuntime(p.RuntimeReport(worker=identities[index],
                                            attempt_id=assignment.attempt_id, stopped=True),
                                            metadata=worker_auth, timeout=5)
                    assert lab.api('capacity')['gpu']['reserved'] == 0
                    histories = {run: lab.api('runs/' + run + '/attempts')['items'] for run in sweep['run_ids']}
                    for history in histories.values():
                        assert len(history) == 1 and history[0]['cleanup_status'] == 'DONE'
                        assert history[0]['gpu_allocations'][0]['released_at']
                    result['waves'].append({'sweep_id': sweep['id'], 'attempts': histories,
                                             'assignment_rpc_seconds': [elapsed for _, elapsed in assignments]})
            result['assignment_rpc_seconds'] = summarize(latencies)
            result['checks'] = [f'exclusive UUID assignment across {args.workers} concurrent workers',
                                 'duplicate UUID advertisements rejected', 'unacknowledged assignment replay',
                                 'duplicate runner claims rejected', 'inventory error preserves reservations',
                                 'cancelled runner is fenced', 'terminal decisions retain reservations',
                                 'cleanup releases devices for the next wave']
            output = pathlib.Path(args.output)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps({key: result[key] for key in ('worker_count', 'gpu_hardware_used',
                                                            'assignment_rpc_seconds', 'checks')}, indent=2))
            channel.close()


if __name__ == '__main__':
    main()
