#!/usr/bin/env python3
"""Measure Docker dispatch and recovery using durable timestamps."""
import argparse
import concurrent.futures
import contextlib
import datetime
import json
import os
import pathlib
import platform
import secrets
import subprocess
import tempfile
import time
import urllib.request
import uuid

from run import summarize, timestamp

ROOT = pathlib.Path(__file__).resolve().parents[1]
POSTGRES = 'postgres:17.9-bookworm@sha256:47f917f7409eacd22fc5dfb1dee634e1b55cf0c01d1a7eb701be2227a03e0641'


def command(*args):
    return subprocess.check_output(args, text=True).strip()


def wait_for(predicate, seconds=180):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(.25)
    raise TimeoutError('benchmark deadline exceeded')


class Lab:
    """Own only the uniquely named containers/network/volumes created for this run."""
    def __init__(self, count, http_port, grpc_port):
        self.count = count
        self.name = 'runyard-scale-' + uuid.uuid4().hex[:8]
        self.url = f'http://127.0.0.1:{http_port}'
        self.grpc_port = grpc_port
        self.owner = secrets.token_hex(24)
        self.worker_token = secrets.token_hex(24)
        self.temp = None
        self.compose_file = None
        self.workers = [f'{self.name}-w{index:02}' for index in range(count)]
        self.services = dict(zip(self.workers, [f'agent-{index:02}' for index in range(count)]))

    def compose(self, *args):
        return command('docker', 'compose', '-p', self.name, '-f', str(self.compose_file), *args)

    def api(self, path, payload=None):
        request = urllib.request.Request(self.url + '/v1/' + path, headers={
            'Authorization': 'Bearer ' + self.owner,
            'Content-Type': 'application/json', 'Idempotency-Key': str(uuid.uuid4()),
        }, data=None if payload is None else json.dumps(payload).encode())
        with urllib.request.urlopen(request, timeout=15) as response:
            return json.load(response)

    def metrics(self):
        with urllib.request.urlopen(self.url + '/metrics', timeout=10) as response:
            return response.read().decode()

    def database_clock(self):
        before = time.time()
        database = float(self.compose('exec', '-T', 'postgres', 'psql', '-U', 'runyard', '-tAc',
                                     'SELECT extract(epoch FROM clock_timestamp())'))
        after = time.time()
        return {'host_before': before, 'database': database, 'host_after': after,
                'estimated_db_minus_host_seconds': database - (before + after) / 2,
                'uncertainty_seconds': (after - before) / 2}

    def __enter__(self):
        ROOT.joinpath('.local').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix=self.name, dir=ROOT / '.local')
        self.compose_file = pathlib.Path(self.temp.name) / 'compose.json'
        env = {
            'RUNYARD_PROFILE': 'development', 'RUNYARD_EXECUTION_MODE': 'docker',
            'RUNYARD_DATABASE_URL': 'host=postgres user=runyard password=' + secrets.token_hex(20)
                                    + ' dbname=runyard connect_timeout=3',
            'RUNYARD_OWNER_TOKEN': self.owner, 'RUNYARD_WORKER_TOKEN': self.worker_token,
            'RUNYARD_SIGNING_KEY': secrets.token_hex(32), 'RUNYARD_BIND': '0.0.0.0',
            'RUNYARD_ARTIFACT_ROOT': '/work/artifacts',
        }
        password = env['RUNYARD_DATABASE_URL'].split('password=')[1].split()[0]
        common = {'image': 'runyard-server:dev', 'pull_policy': 'never', 'environment': env,
                  'networks': ['lab']}
        services = {
            'postgres': {'image': POSTGRES, 'environment': {
                'POSTGRES_USER': 'runyard', 'POSTGRES_PASSWORD': password, 'POSTGRES_DB': 'runyard'},
                'healthcheck': {'test': ['CMD-SHELL', 'pg_isready -U runyard'], 'interval': '1s',
                                'timeout': '2s', 'retries': 30},
                'volumes': ['database:/var/lib/postgresql/data'], 'networks': ['lab']},
            'migrate': {**common, 'command': ['migrate', '--directory', '/opt/runyard/migrations'],
                        'depends_on': {'postgres': {'condition': 'service_healthy'}}},
            'server': {**common, 'ports': [self.url.removeprefix('http://') + ':8080',
                                           f'127.0.0.1:{self.grpc_port}:9090'],
                       'depends_on': {'migrate': {'condition': 'service_completed_successfully'}}},
        }
        for worker, service in self.services.items():
            services[service] = {
                'image': 'runyard-agent:dev', 'pull_policy': 'never', 'networks': ['lab'],
                'volumes': ['/var/run/docker.sock:/var/run/docker.sock'],
                'depends_on': {'server': {'condition': 'service_started'}},
                'environment': {
                    'RUNYARD_PROFILE': 'development', 'RUNYARD_COORDINATOR': 'server:9090',
                    'RUNYARD_WORKER_TOKEN': self.worker_token, 'RUNYARD_WORKER_ID': worker,
                    'RUNYARD_CPU_MILLIS': '250', 'RUNYARD_MEMORY_MIB': '256',
                    'RUNYARD_DOCKER_NETWORK': self.name, 'RUNYARD_GPU_MODE': 'disabled',
                },
            }
        self.compose_file.write_text(json.dumps({'services': services, 'volumes': {'database': {}},
                                                 'networks': {'lab': {'name': self.name}}}))
        self.compose_file.chmod(0o600)
        try:
            self.compose('up', '-d', '--wait', '--wait-timeout', '90')
            wait_for(lambda: len(self.api('workers?limit=200')['items']) == self.count)
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *_):
        # Agent-created attempts are not Compose services. Remove only attempts
        # carrying one of this lab's worker identities, including stopped ones.
        with contextlib.suppress(Exception):
            self.compose('stop', *self.services.values())
        for worker in self.workers:
            containers = command('docker', 'ps', '-aq', '--filter', 'label=runyard.worker=' + worker).splitlines()
            if containers:
                command('docker', 'rm', '-f', *containers)
        with contextlib.suppress(Exception):
            self.compose('down', '--volumes', '--timeout', '5')
        if self.temp:
            self.temp.cleanup()


def fixture(image, name, **parameters):
    return {'version': 1, 'name': name, 'image': image,
            'command': ['/usr/local/bin/runyard-fixture'], 'parameters': parameters,
            'resources': {'cpu_millis': 250, 'memory_mib': 128, 'gpu_count': 0},
            'retry': {'max_attempts': 3}, 'timeout_seconds': 180, 'priority': 5}


def collect(lab, ids):
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        runs = list(executor.map(lambda run: lab.api('runs/' + run), ids))
        attempts = list(executor.map(lambda run: lab.api('runs/' + run + '/attempts')['items'], ids))
    return runs, dict(zip(ids, attempts))


def completed(lab, ids):
    def snapshot():
        runs, attempts = collect(lab, ids)
        if all(run['status'] in ('SUCCEEDED', 'FAILED', 'CANCELLED') for run in runs):
            assert all(run['status'] == 'SUCCEEDED' for run in runs), 'unsuccessful benchmark run'
            return runs, attempts
    return wait_for(snapshot, 600)


def measure_round(lab, spec, count):
    before = lab.metrics()
    submitted = time.monotonic()
    sweep = lab.api('sweeps', {'base': spec, 'grid': {'seed': list(range(count))}})
    submission = time.monotonic() - submitted
    runs, attempts = completed(lab, sweep['run_ids'])
    delays = {'queue': [], 'dispatch': [], 'submission_to_claim': []}
    used = set()
    intervals = []
    for run in runs:
        history = attempts[run['id']]
        assert len(history) == 1, 'steady-state run unexpectedly retried'
        attempt = history[0]
        created, assigned, claimed, finished = map(timestamp, [run['created_at'], attempt['created_at'],
                                                              attempt['started_at'], attempt['finished_at']])
        delays['queue'].append(assigned - created)
        delays['dispatch'].append(claimed - assigned)
        delays['submission_to_claim'].append(claimed - created)
        used.add(attempt['worker_id'])
        intervals.extend([(claimed, 1), (finished, -1)])
    current = peak = 0
    for _, change in sorted(intervals):
        current += change
        peak = max(peak, current)
    return {'count': count, 'submission_seconds': submission, 'unique_workers': sorted(used),
            'peak_overlapping_attempts': peak, 'seconds': {key: summarize(value) for key, value in delays.items()},
            'raw_seconds': delays, 'runs': runs, 'attempts': attempts,
            'prometheus_before': before, 'prometheus_after': lab.metrics()}


def agent_only_crash(lab, image):
    run = lab.api('runs', fixture(image, 'agent-crash', steps=120, delay_ms=250))
    active = wait_for(lambda: (row if row['status'] == 'RUNNING' else None)
                     if (row := lab.api('runs/' + run['id'])) else None)
    attempt = lab.api('runs/' + run['id'] + '/attempts')['items'][0]
    service = lab.services[attempt['worker_id']]
    started = time.time()
    lab.compose('kill', '-s', 'SIGKILL', service)
    try:
        runs, attempts = completed(lab, [active['id']])
        assert len(attempts[active['id']]) == 1, 'agent loss retried a healthy runner'
        assert attempts[active['id']][0]['id'] == attempt['id']
        return {'fault_at_unix': started, 'outcome': 'original attempt completed without restarting',
                'run': runs[0], 'attempts': attempts[active['id']]}
    finally:
        lab.compose('start', service)


def lost_workers(lab, image, batch):
    # Long-enough work ensures every worker has an active attempt before fault
    # injection. The replacement reruns the same immutable finite workload.
    sweep = lab.api('sweeps', {'base': fixture(image, f'worker-loss-{batch}', steps=160, delay_ms=250),
                              'grid': {'seed': list(range(lab.count))}})
    ids = sweep['run_ids']
    def all_started():
        runs, attempts = collect(lab, ids)
        return (runs, attempts) if all(run['status'] == 'RUNNING' for run in runs) else None
    runs, attempts = wait_for(all_started, 120)
    assert len({history[0]['worker_id'] for history in attempts.values()}) == lab.count
    victims = sorted((history[0] for history in attempts.values()), key=lambda row: row['worker_id'])[:3]
    agent_containers = [lab.compose('ps', '-q', lab.services[a['worker_id']]) for a in victims]
    fault_before = time.time()
    command('docker', 'kill', *agent_containers, *('runyard-' + a['id'] for a in victims))
    fault_after = time.time()
    try:
        finished, histories = completed(lab, ids)
        observations = []
        for original in victims:
            history = histories[original['run_id']]
            assert len(history) == 2
            assert history[0]['status'] == 'FAILED' and history[0]['reason'] == 'LEASE_EXPIRED'
            assert history[1]['status'] == 'SUCCEEDED'
            assert history[1]['worker_id'] not in {a['worker_id'] for a in victims}
            claimed = timestamp(history[1]['started_at'])
            observations.append({'run_id': original['run_id'], 'original_worker': original['worker_id'],
                                 'replacement_worker': history[1]['worker_id'],
                                 'seconds_from_fault_command_start': claimed - fault_before,
                                 'seconds_from_fault_command_end': claimed - fault_after})
        return {'fault': 'SIGKILL three agents and their assigned runner containers',
                'fault_before_unix': fault_before, 'fault_after_unix': fault_after,
                'observations': observations, 'runs': finished, 'attempts': histories}
    finally:
        lab.compose('start', *(lab.services[a['worker_id']] for a in victims))
        wait_for(lambda: all(w['available'] for w in lab.api('workers?limit=200')['items']))


def hardware():
    docker = json.loads(command('docker', 'info', '--format', '{{json .}}'))
    return {'os': platform.platform(), 'architecture': platform.machine(), 'logical_cpus': os.cpu_count(),
            'cpu': command('sysctl', '-n', 'machdep.cpu.brand_string') if platform.system() == 'Darwin' else platform.processor(),
            'docker_vm': {key: docker[key] for key in ('NCPU', 'MemTotal', 'Architecture', 'ServerVersion')},
            'preexisting_container_count': len(command('docker', 'ps', '--format', '{{.ID}}').splitlines())}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', required=True)
    parser.add_argument('--workers', type=int, default=12)
    parser.add_argument('--count', type=int, default=120)
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--fault-batches', type=int, default=2)
    parser.add_argument('--http-port', type=int, default=18081)
    parser.add_argument('--grpc-port', type=int, default=19091)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if not 4 <= args.workers <= 24 or not args.workers <= args.count <= 1000 or not 1 <= args.rounds <= 10 or not 0 <= args.fault_batches <= 5:
        parser.error('workers 4..24; count workers..1000; rounds 1..10; fault-batches 0..5')
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    result = {'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'revision': command('git', 'rev-parse', 'HEAD'), 'hardware': hardware(),
              'images': {name: command('docker', 'image', 'inspect', '--format', '{{.Id}}', name)
                         for name in ('runyard-server:dev', 'runyard-agent:dev', args.image)},
              'configuration': {'logical_workers': args.workers, 'worker_cpu_millis': 250,
                                'worker_memory_mib': 256, 'lease_seconds': 30, 'heartbeat_seconds': 5,
                                'retry_base_seconds': 5, 'recovery_scan_seconds': 1,
                                'timing_overrides': False}, 'rounds': [], 'worker_loss': []}
    def save():
        output.write_text(json.dumps(result, indent=2) + '\n')
    save()
    with Lab(args.workers, args.http_port, args.grpc_port) as lab:
        result['clock_before'] = lab.database_clock()
        result['workers'] = lab.api('workers?limit=200')['items']
        print(f'Isolated deployment ready: {args.workers} logical CPU workers', flush=True)
        warmup = measure_round(lab, fixture(args.image, 'warmup', steps=5, delay_ms=20), args.workers)
        result['warmup'] = warmup
        save()
        for index in range(args.rounds):
            measurement = measure_round(lab, fixture(args.image, f'dispatch-{index + 1}', steps=5, delay_ms=20), args.count)
            result['rounds'].append(measurement)
            save()
            print(json.dumps({'round': index + 1, 'workers': len(measurement['unique_workers']),
                              'seconds': measurement['seconds']}), flush=True)
        result['agent_only_crash'] = agent_only_crash(lab, args.image)
        save()
        print('Agent-only crash: healthy runner finished its original attempt', flush=True)
        for batch in range(args.fault_batches):
            observation = lost_workers(lab, args.image, batch + 1)
            result['worker_loss'].append(observation)
            save()
            print(json.dumps({'worker_loss_batch': batch + 1, 'observations': observation['observations']}), flush=True)
        result['clock_after'] = lab.database_clock()
        dispatch = [value for round_ in result['rounds'] for value in round_['raw_seconds']['dispatch']]
        recovery = [item['seconds_from_fault_command_start'] for fault in result['worker_loss'] for item in fault['observations']]
        result['summary'] = {'dispatch_seconds': summarize(dispatch), 'recovery_seconds': summarize(recovery),
                             'gpu_hardware_used': False, 'physical_hosts': 1}
        result['finished_at_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        save()
        print(json.dumps(result['summary'], indent=2), flush=True)


if __name__ == '__main__':
    main()
