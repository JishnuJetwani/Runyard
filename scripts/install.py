#!/usr/bin/env python3
"""Start a local installation and verify the first submission-to-artifact workflow."""
import argparse
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.request

from install_support import ROOT, Installation, ensure_credentials


def request(installation, path, payload=None, key=None, raw=False):
    headers = {'Authorization': 'Bearer ' + installation.environment.get('RUNYARD_OWNER_TOKEN', ''),
               'Content-Type': 'application/json'}
    if key:
        headers['Idempotency-Key'] = key
    req = urllib.request.Request(installation.url() + path, headers=headers,
                                 data=None if payload is None else json.dumps(payload).encode())
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            data = response.read()
            return data if raw else json.loads(data)
    except urllib.error.HTTPError as error:
        raise RuntimeError(f'API returned HTTP {error.code} for {path}. '
                           'Check ./runyard logs and the credentials in your environment file.') from None


def wait_ready(installation):
    deadline = time.monotonic() + 120
    last_error = 'Waiting for the coordinator and workers'
    while time.monotonic() < deadline:
        try:
            request(installation, '/health/ready', raw=True)
            workers = request(installation, '/v1/workers?limit=200')['items']
            prefix = installation.environment.get('RUNYARD_WORKER_PREFIX', 'local')
            expected = {f'{prefix}-{number}' for number in (1, 2, 3)}
            ready = {worker['id'] for worker in workers if worker['available'] and not worker['drained']}
            if expected <= ready:
                with urllib.request.urlopen(installation.url('ui'), timeout=5) as response:
                    if response.status == 200:
                        print('Coordinator, dashboard, and all three workers are ready.', flush=True)
                        return
        except (OSError, RuntimeError) as error:
            last_error = str(error)
        time.sleep(1)
    raise RuntimeError(f'Startup did not become ready within 120 seconds. {last_error}. '
                       'Run ./runyard logs to inspect it; existing data has been retained.')


def fixture_digest(installation):
    image = installation.images['fixture']
    if installation.prebuilt:
        repository = image.rsplit(':', 1)[0]
    else:
        port = installation.environment.get('RUNYARD_LOCAL_REGISTRY_PORT', '5001')
        repository = f'localhost:{port}/runyard-fixture'
        tag = repository + ':dev'
        installation.run('docker', 'tag', image, tag)
        installation.run('docker', 'push', tag)
        image = tag
    result = installation.run('docker', 'image', 'inspect', '--format', '{{json .RepoDigests}}',
                              image, capture=True)
    for digest in json.loads(result.stdout) or []:
        if digest.startswith(repository + '@sha256:'):
            return digest
    raise RuntimeError('The fixture image has no registry digest. Pull or publish the image first.')


def first_experiment(installation):
    digest = fixture_digest(installation)
    spec = {'version': 1, 'name': 'Welcome to Runyard', 'image': digest,
            'command': ['/usr/local/bin/runyard-fixture'],
            'parameters': {'seed': 7, 'steps': 5, 'delay_ms': 100},
            'resources': {'cpu_millis': 250, 'memory_mib': 128},
            'labels': {'purpose': 'first-run'}}
    # Reuse the submission after a lost response or repeated setup. Changing the
    # fixture image intentionally creates a new first-run example.
    key = 'install-' + hashlib.sha256(json.dumps(spec, sort_keys=True).encode()).hexdigest()
    installation.state.mkdir(parents=True, exist_ok=True)
    (installation.state / 'example.json').write_text(json.dumps(spec, indent=2) + '\n')
    run = request(installation, '/v1/runs', spec, key)
    print(f'First experiment: {run["id"]}', flush=True)
    deadline = time.monotonic() + 180
    previous = None
    while time.monotonic() < deadline:
        run = request(installation, '/v1/runs/' + run['id'])
        if run['status'] != previous:
            print('  ' + run['status'], flush=True)
            previous = run['status']
        if run['status'] == 'SUCCEEDED':
            break
        if run['status'] in ('FAILED', 'CANCELLED'):
            raise RuntimeError(f'Example ended {run["status"]}. Inspect {installation.url("ui")}/runs/{run["id"]}; '
                               'use Rerun in the dashboard after fixing the cause.')
        time.sleep(1)
    else:
        raise RuntimeError('Example is still pending. Inspect its run in the dashboard or run ./runyard logs.')
    metrics = request(installation, f'/v1/runs/{run["id"]}/metrics')['items']
    logs = request(installation, f'/v1/runs/{run["id"]}/logs')['items']
    artifacts = request(installation, f'/v1/runs/{run["id"]}/artifacts')['items']
    artifact = next((item for item in artifacts if item['path'] == 'result.json'), None)
    if not metrics or not logs or not artifact:
        raise RuntimeError('Example completed without its expected metrics, logs, and result artifact.')
    data = request(installation, '/v1/artifacts/' + artifact['id'] + '/download', raw=True)
    if len(data) != artifact['size'] or hashlib.sha256(data).hexdigest() != artifact['sha256']:
        raise RuntimeError('Example artifact failed size/checksum verification.')
    if json.loads(data)['seed'] != 7:
        raise RuntimeError('Example output does not match its input parameters.')
    destination = installation.state / 'result.json'
    destination.write_bytes(data)
    evidence = {'run_id': run['id'], 'image': digest, 'artifact_sha256': artifact['sha256'],
                'checks': ['submission', 'execution', 'metrics', 'logs', 'artifact checksum']}
    (installation.state / 'example-result.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(f'Verified metrics, logs, and artifact. Downloaded result: {destination}')
    print(f'Inspect the run: {installation.url("ui")}/runs/{run["id"]}')


def parser():
    app = argparse.ArgumentParser(description='Install and operate a private local Runyard deployment.')
    commands = app.add_subparsers(dest='command', required=True)
    up = commands.add_parser('up', help='start the dashboard, backend, workers, and first experiment')
    up.add_argument('--build', action='store_true', help='rebuild all local images from source')
    up.add_argument('--image-root', help='published registry root, e.g. ghcr.io/owner/runyard')
    up.add_argument('--version', help='published version, e.g. v0.1.0')
    up.add_argument('--skip-example', action='store_true', help='start without submitting the example')
    commands.add_parser('doctor', help='check Docker and Compose prerequisites')
    commands.add_parser('status', help='show installed services and coordinator readiness')
    commands.add_parser('stop', help='stop local services and runners, preserving all data')
    commands.add_parser('key', help='print the owner key for dashboard Connection settings')
    commands.add_parser('example', help='submit or inspect the repeatable first experiment')
    cli = commands.add_parser('cli', help='run the C++ client using this installation')
    cli.add_argument('arguments', nargs=argparse.REMAINDER)
    logs = commands.add_parser('logs', help='show recent service logs')
    logs.add_argument('service', nargs='?')
    logs.add_argument('--follow', '-f', action='store_true')
    return app


def parse_arguments(arguments):
    # Native CLI flags may precede its subcommand. Forward them untouched,
    # including --help, instead of interpreting them as installer options.
    if arguments and arguments[0] == 'cli':
        return argparse.Namespace(command='cli', arguments=arguments[1:])
    return parser().parse_args(arguments)


def main():
    if sys.version_info < (3, 9):
        raise RuntimeError('Python 3.9+ is required.')
    args = parse_arguments(sys.argv[1:])
    installation = Installation()
    if args.command == 'key':
        key = installation.environment.get('RUNYARD_OWNER_TOKEN')
        if not key:
            raise RuntimeError('No owner key found. Run ./runyard up first.')
        print(key)
        return
    installation.check_prerequisites()
    if args.command == 'doctor':
        return
    if args.command == 'up':
        ensure_credentials(installation.env_file)
        installation.configure_images(args.image_root, args.version, args.build)
        installation.compose('config', '--quiet')
        print('Starting Runyard; applying migrations before coordinator startup.', flush=True)
        installation.compose('up', '-d', '--no-build', '--wait', '--wait-timeout', '120',
                             'postgres', 'migrate', 'server', 'agent-1', 'agent-2', 'agent-3', 'registry', 'ui')
        wait_ready(installation)
        print(f'Dashboard: {installation.url("ui")}\n'
              'Use ./runyard key to get the owner key, then paste it into Connection settings.', flush=True)
        if not args.skip_example:
            first_experiment(installation)
    elif args.command == 'example':
        first_experiment(installation)
    elif args.command == 'stop':
        installation.stop()
    elif args.command == 'status':
        installation.compose('ps')
        request(installation, '/health/ready', raw=True)
        print('Coordinator ready. Dashboard: ' + installation.url('ui'))
    elif args.command == 'cli':
        installation.run('docker', 'run', '--rm', '--network', installation.project,
                         '--user', f'{os.getuid()}:{os.getgid()}', '-e', 'RUNYARD_OWNER_TOKEN',
                         '-e', 'RUNYARD_PROFILE=development', '-v', str(ROOT) + ':/workspace',
                         '-w', '/workspace', installation.images['cli'], '--url', 'http://server:8080',
                         *args.arguments)
    elif args.command == 'logs':
        options = ['logs', '--tail', '100']
        if args.follow:
            options.append('--follow')
        if args.service:
            options.append(args.service)
        installation.compose(*options)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\nInterrupted. Services and data are preserved; use ./runyard status or ./runyard stop.', file=sys.stderr)
        sys.exit(130)
    except (RuntimeError, OSError, ValueError) as error:
        print(f'Runyard: {error}', file=sys.stderr)
        sys.exit(1)
