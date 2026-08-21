#!/usr/bin/env python3
"""Measure actual coordinator timestamps and preserve raw observations."""
import argparse
import concurrent.futures
import datetime
import json
import os
import pathlib
import platform
import subprocess
import sys
import time
import urllib.request
import uuid

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tests'))
from execution_contract import request, terminal, wait_for


def percentile(values, fraction):
    values = sorted(values)
    if not values:
        return None
    position = (len(values) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(values) - 1)
    return values[lower] + (values[upper] - values[lower]) * (position - lower)


def summarize(values):
    return {'count': len(values), **{label: percentile(values, fraction) for label, fraction in [('p50', .5), ('p95', .95), ('p99', .99)]}}


def timestamp(value):
    value = value.replace('T', ' ').replace('Z', '+00:00')
    if len(value) >= 3 and value[-3] in '+-' and value[-2:].isdigit():
        value += ':00'
    pattern = '%Y-%m-%d %H:%M:%S' + ('.%f' if '.' in value else '') + '%z'
    return datetime.datetime.strptime(value, pattern).timestamp()


def metrics():
    with urllib.request.urlopen(os.environ.get('RUNYARD_URL', 'http://127.0.0.1:8080') + '/metrics', timeout=10) as response:
        return response.read().decode()


def metric_value(exposition, name):
    for line in exposition.splitlines():
        if line.startswith(name + ' '):
            return float(line.split()[1])
    return None


def deployment_configuration(backend):
    keys = {'RUNYARD_PROFILE', 'RUNYARD_EXECUTION_MODE', 'RUNYARD_MAX_ACTIVE_JOBS',
            'RUNYARD_HEARTBEAT_SECONDS', 'RUNYARD_LEASE_SECONDS', 'RUNYARD_WORKER_SECONDS',
            'RUNYARD_LAUNCH_SECONDS', 'RUNYARD_RETRY_BASE_SECONDS', 'RUNYARD_RETRY_MAX_SECONDS',
            'RUNYARD_TERMINATION_SECONDS', 'RUNYARD_FINALIZATION_SECONDS', 'RUNYARD_RECOVERY_SCAN_MILLIS',
            'RUNYARD_STORAGE'}
    if backend == 'docker':
        container = subprocess.check_output(['docker', 'compose', 'ps', '-q', 'server'], text=True).strip()
        raw = subprocess.check_output(['docker', 'inspect', '--format', '{{json .Config.Env}}', container], text=True)
        values = dict(entry.split('=', 1) for entry in json.loads(raw))
        resources = {'coordinator_limits': 'Docker VM capacity; no separate Compose coordinator limit'}
    else:
        command = ['kubectl', '--context', 'kind-runyard', '-n', 'runyard', 'get']
        values = json.loads(subprocess.check_output(command + ['configmap', 'runyard-config', '-o', 'json'], text=True))['data']
        server = json.loads(subprocess.check_output(command + ['deployment', 'server', '-o', 'json'], text=True))
        resources = {'coordinator_resources': server['spec']['template']['spec']['containers'][0]['resources']}
        version = json.loads(subprocess.check_output(['kubectl', '--context', 'kind-runyard', 'version', '-o', 'json'], text=True))
        resources['kubernetes_version'] = version['serverVersion']['gitVersion']
    # Credentials share the Docker environment; retain only this explicit non-secret allowlist.
    return {'environment_overrides': {key: value for key, value in values.items() if key in keys},
            'unspecified_settings': 'Defaults from the recorded source revision', **resources}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', required=True)
    parser.add_argument('--backend', choices=['docker', 'kubernetes'], required=True)
    parser.add_argument('--count', type=int, default=100)
    parser.add_argument('--output', required=True)
    parser.add_argument('--recovery-evidence', help='execution_contract.py output from the same configuration')
    args = parser.parse_args()
    if not 1 <= args.count <= 1000:
        parser.error('count must be 1..1000')
    spec = {'name': 'benchmark', 'image': args.image, 'command': ['/usr/local/bin/runyard-fixture'],
            'resources': {'cpu_millis': 250, 'memory_mib': 128}, 'parameters': {'steps': 5, 'delay_ms': 20},
            'retry': {'max_attempts': 3}, 'priority': 5}
    before = metrics()
    started = time.time()
    submit_start = time.monotonic()
    sweep = request('/v1/sweeps', {'base': spec, 'grid': {'seed': list(range(args.count))}}, str(uuid.uuid4()))
    submission_seconds = time.monotonic() - submit_start
    ids = sweep['run_ids']
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        def snapshot():
            return list(executor.map(lambda run: request('/v1/runs/' + run), ids))
        runs = wait_for(lambda: (rows if all(terminal(r) for r in rows) else None) if (rows := snapshot()) else None, 900)
    observed_wall = time.time() - started
    if any(r['status'] != 'SUCCEEDED' for r in runs):
        raise AssertionError('benchmark contains unsuccessful runs')
    histories = {r['id']: request('/v1/runs/' + r['id'] + '/attempts')['items'] for r in runs}
    after = metrics()
    queue, dispatch = [], []
    for run in runs:
        first = histories[run['id']][0]
        queue.append(timestamp(first['created_at']) - timestamp(run['created_at']))
        dispatch.append(timestamp(first['started_at']) - timestamp(first['created_at']))
    first_submission = min(timestamp(r['created_at']) for r in runs)
    last_finish = max(timestamp(histories[r['id']][-1]['finished_at']) for r in runs)
    hardware = {'os': platform.system(), 'os_release': platform.release(), 'architecture': platform.machine(), 'logical_cpus': os.cpu_count()}
    if platform.system() == 'Darwin':
        hardware['cpu'] = subprocess.check_output(['sysctl', '-n', 'machdep.cpu.brand_string'], text=True).strip()
        hardware['memory_bytes'] = int(subprocess.check_output(['sysctl', '-n', 'hw.memsize'], text=True))
    docker_info = json.loads(subprocess.check_output(['docker', 'info', '--format', '{{json .}}'], text=True))
    hardware['docker_vm'] = {k: docker_info[k] for k in ('NCPU', 'MemTotal', 'ServerVersion', 'Architecture')}
    container_names = subprocess.check_output(['docker', 'ps', '--format', '{{.Names}}'], text=True).splitlines()
    hardware['other_running_container_count'] = sum(not name.startswith('runyard') for name in container_names)
    cpu_before, cpu_after = [metric_value(value, 'runyard_process_cpu_seconds') for value in (before, after)]
    result = {'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'backend': args.backend,
              'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(), 'hardware': hardware,
              'deployment_configuration': deployment_configuration(args.backend),
              'limitations': ['Shared local host and Docker VM; logical workers are not separate machines.',
                              'Small fixture workload; polling and container startup affect throughput.',
                              'CPU fixture workload with a warm image cache.',
                              'One observed sample, not a statistically isolated capacity study.',
                              'Recovery compares the host fault clock to PostgreSQL timestamps; local clocks are assumed aligned.'],
              'specification': spec, 'count': args.count, 'submission_seconds': submission_seconds,
              'observed_wall_seconds': observed_wall, 'durable_execution_window_seconds': last_finish-first_submission,
              'completed_runs_per_second': args.count/(last_finish-first_submission),
              'queue_delay_seconds': summarize(queue), 'dispatch_latency_seconds': summarize(dispatch),
              'coordinator_cpu_seconds': None if cpu_before is None or cpu_after is None else cpu_after-cpu_before,
              'coordinator_peak_rss_bytes': metric_value(after, 'runyard_process_max_rss_bytes'),
              'workers': request('/v1/workers')['items'], 'sweep_id': sweep['id'], 'runs': runs, 'attempts': histories,
              'prometheus_before': before, 'prometheus_after': after}
    if args.recovery_evidence:
        faults = json.loads(pathlib.Path(args.recovery_evidence).read_text())
        if faults['backend'] != args.backend or any(r['spec']['image'] != args.image for r in faults['runs']):
            raise ValueError('recovery evidence must use the same backend and fixture image')
        result['recovery_seconds'] = [timestamp(faults['attempts'][run_id][1]['started_at']) - faults['fault_at_unix']
                                      for run_id in faults['interrupted_runs']]
        result['recovery_source'] = args.recovery_evidence
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('count', 'completed_runs_per_second', 'queue_delay_seconds', 'dispatch_latency_seconds', 'coordinator_cpu_seconds', 'coordinator_peak_rss_bytes')}, indent=2))


if __name__ == '__main__':
    main()
