#!/usr/bin/env python3
"""Verify installation with cached images, a fresh database, and scoped cleanup."""
import argparse
import json
import os
import pathlib
import socket
import sys
import tempfile
import time
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from install import request
from install_support import Installation


def available_ports(count):
    sockets = [socket.socket() for _ in range(count)]
    try:
        for connection in sockets:
            connection.bind(('127.0.0.1', 0))
        return [connection.getsockname()[1] for connection in sockets]
    finally:
        for connection in sockets:
            connection.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='.local/install-smoke.json')
    args = parser.parse_args()
    name = 'runyard-install-' + uuid.uuid4().hex[:8]
    with tempfile.TemporaryDirectory(prefix=name) as tmp:
        state = pathlib.Path(tmp)
        env_file = state / 'env'
        ports = dict(zip(('HTTP', 'GRPC', 'UI', 'REGISTRY'), available_ports(4)))
        env_file.write_text(f'RUNYARD_PROJECT_NAME={name}\n' + ''.join(
            f'RUNYARD_LOCAL_{key}_PORT={value}\n' for key, value in ports.items()))
        env_file.chmod(0o600)
        os.environ.update(RUNYARD_INSTALL_STATE=str(state), RUNYARD_ENV_FILE=str(env_file))
        installation = Installation()
        for image in installation.images.values():
            installation.run('docker', 'image', 'inspect', image, capture=True)
        def launcher(*arguments):
            installation.run(str(ROOT / 'runyard'), *arguments)
        try:
            launcher('up')
            installation.reload_environment()
            first = json.loads((state / 'example-result.json').read_text())
            credentials = env_file.read_text()
            launcher('up')
            second = json.loads((state / 'example-result.json').read_text())
            assert first == second, 'repeated setup duplicated the first experiment'
            assert credentials == env_file.read_text(), 'repeated setup rotated credentials'
            assert len(request(installation, '/v1/runs?limit=100')['items']) == 1
            launcher('cli', 'runs', 'get', first['run_id'])
            # Stopping the installation must also stop active agent-created runners.
            spec = json.loads((state / 'example.json').read_text())
            spec.update(name='installer-stop-check', parameters={'mode': 'hang'})
            active = request(installation, '/v1/runs', spec, 'installer-stop-check')
            for _ in range(60):
                if request(installation, '/v1/runs/' + active['id'])['status'] == 'RUNNING':
                    break
                time.sleep(1)
            else:
                raise AssertionError('stop-check runner did not start')
            launcher('stop')
            running = installation.run('docker', 'ps', '-q', '--filter', 'label=runyard.attempt',
                                       '--filter', 'network=' + name, capture=True).stdout.strip()
            assert not running, 'stop left an experiment running'
            launcher('up', '--skip-example')
            launcher('status')
            request(installation, '/v1/runs/' + active['id'] + '/cancel', {})
            assert request(installation, '/v1/runs/' + first['run_id'])['status'] == 'SUCCEEDED'
            artifact = request(installation, '/v1/runs/' + first['run_id'] + '/artifacts')['items'][0]
            assert request(installation, '/v1/artifacts/' + artifact['id'] + '/download', raw=True)
            result = {'project': name, 'example': first, 'checks': [
                'fresh database startup', 'dashboard readiness', 'three registered workers',
                'repeat startup preserves credentials and submission', 'C++ client inspection',
                'stop terminates active local runners', 'restart retains history and artifacts']}
            output = pathlib.Path(args.output)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(json.dumps(result, indent=2) + '\n')
            print('Installation acceptance passed.', flush=True)
        finally:
            installation.reload_environment()
            installation.compose('stop', 'agent-1', 'agent-2', 'agent-3', check=False)
            containers = installation.run('docker', 'ps', '-aq', '--filter', 'label=runyard.attempt',
                                          '--filter', 'network=' + name, capture=True).stdout.split()
            if containers:
                installation.run('docker', 'rm', '-f', *containers)
            installation.compose('down', '--volumes', '--timeout', '5')


if __name__ == '__main__':
    main()
