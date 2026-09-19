"""Local installation state and Docker operations; no third-party Python packages."""
import json
import os
import pathlib
import re
import secrets
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
TARGETS = ('server', 'agent', 'runner-base', 'fixture', 'cli', 'ui')
SECRET_LENGTHS = {'RUNYARD_OWNER_TOKEN': 24, 'RUNYARD_WORKER_TOKEN': 24,
                  'RUNYARD_SIGNING_KEY': 32, 'RUNYARD_GRAFANA_PASSWORD': 24}


def read_env(path):
    values = {}
    if path.exists():
        for line in path.read_text().splitlines():
            if line.strip() and not line.lstrip().startswith('#'):
                key, separator, value = line.partition('=')
                if not separator:
                    raise RuntimeError(f'Expected KEY=value in {path}')
                values[key.strip()] = value.strip().strip('\"\'')
    return values


def ensure_credentials(path):
    """Preserve existing keys: rotating them would disconnect installed workers."""
    values = read_env(path)
    additions = {key: secrets.token_hex(length) for key, length in SECRET_LENGTHS.items()
                 if key not in values}
    if any(not values[key] for key in SECRET_LENGTHS if key in values):
        raise RuntimeError(f'Empty credential in {path}; restore its value before starting.')
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    with os.fdopen(descriptor, 'a') as output:
        os.fchmod(output.fileno(), 0o600)
        if additions:
            output.write('\n' + ''.join(f'{key}={value}\n' for key, value in additions.items()))
    return {**values, **additions}


def release_images(root, version):
    root = root.rstrip('/')
    if not re.fullmatch(r'[a-z0-9][a-z0-9._:/-]*', root) or '/' not in root:
        raise RuntimeError('Use an image root such as ghcr.io/owner/runyard.')
    if not re.fullmatch(r'v?\d+\.\d+\.\d+(?:-[a-zA-Z0-9.-]+)?', version):
        raise RuntimeError('Use a versioned release tag such as v0.1.0.')
    return {target: f'{root}/{target}:{version}' for target in TARGETS}


class Installation:
    def __init__(self):
        self.state = pathlib.Path(os.environ.get('RUNYARD_INSTALL_STATE', ROOT / '.local/install')).resolve()
        self.env_file = pathlib.Path(os.environ.get('RUNYARD_ENV_FILE', ROOT / '.env')).resolve()
        self.settings_file = self.state / 'images.json'
        self.images = {target: f'runyard-{target}:dev' for target in TARGETS}
        self.prebuilt = self.settings_file.exists()
        if self.prebuilt:
            settings = json.loads(self.settings_file.read_text())
            self.images = release_images(settings['image_root'], settings['version'])
        self.reload_environment()

    def reload_environment(self):
        self.environment = {**os.environ, **read_env(self.env_file)}
        self.project = self.environment.get('RUNYARD_PROJECT_NAME', 'runyard')
        if self.project != 'runyard':
            self.environment.setdefault('RUNYARD_WORKER_PREFIX', self.project + '-local')
        for target, image in self.images.items():
            self.environment['RUNYARD_' + target.upper().replace('-', '_') + '_IMAGE'] = image

    def run(self, *args, capture=False, check=True):
        result = subprocess.run(args, cwd=ROOT, env=self.environment, text=True,
                                stdout=subprocess.PIPE if capture else None,
                                stderr=subprocess.PIPE if capture else None)
        if check and result.returncode:
            # Do not echo environment values or Docker config, which contain keys.
            raise RuntimeError(f'{" ".join(args[:3])} failed (exit {result.returncode}). '
                               'Check Docker is running; use ./runyard logs for service errors.')
        return result

    def compose(self, *args, **options):
        return self.run('docker', 'compose', '--project-name', self.project, '--env-file', str(self.env_file),
                        '-f', 'compose.yaml', '-f', 'deploy/ui.compose.yaml', *args, **options)

    def check_prerequisites(self):
        if not shutil.which('docker'):
            raise RuntimeError('Install Docker Desktop (Mac) or Docker Engine + Compose v2 (Linux).')
        result = self.run('docker', 'info', '--format', '{{.OSType}}', capture=True, check=False)
        if result.returncode:
            raise RuntimeError('Docker is not reachable. Start Docker Desktop or the Docker daemon.')
        if result.stdout.strip() != 'linux':
            raise RuntimeError('Switch Docker to Linux containers.')
        result = self.run('docker', 'compose', 'version', '--short', capture=True, check=False)
        match = re.match(r'v?(\d+)\.(\d+)', result.stdout.strip())
        if result.returncode or not match or tuple(map(int, match.groups())) < (2, 20):
            raise RuntimeError('Docker Compose 2.20+ is required. Update Docker Desktop or Compose.')
        print('Docker and Compose are ready.', file=sys.stderr, flush=True)

    def configure_images(self, root=None, version=None, build=False):
        if bool(root) != bool(version) or (root and build):
            raise RuntimeError('Use --image-root with --version, or --build, separately.')
        if root:
            self.images = release_images(root, version)
            self.prebuilt = True
        elif build:
            self.images = {target: f'runyard-{target}:dev' for target in TARGETS}
            self.prebuilt = False
        self.reload_environment()
        for target, image in self.images.items():
            found = self.run('docker', 'image', 'inspect', image, capture=True, check=False).returncode == 0
            if found and not build:
                continue
            if self.prebuilt:
                print(f'Pulling {image}', flush=True)
                pulled = self.run('docker', 'pull', image, check=False)
                if pulled.returncode:
                    raise RuntimeError(f'Cannot pull {image}. Check the published version and registry access. '
                                       'Source builds are available with ./runyard up --build.')
            else:
                print(f'Building {target}. The first C++ dependency build can take a while; '
                      'subsequent builds use Docker\'s cache.', flush=True)
                if target == 'ui':
                    self.run('docker', 'build', '-f', 'ui/Dockerfile', '-t', image, 'ui')
                else:
                    self.run('docker', 'build', '-f', 'deploy/docker/Dockerfile', '--target', target,
                             '-t', image, '.')
        self.state.mkdir(parents=True, exist_ok=True)
        if root:
            self.settings_file.write_text(json.dumps({'image_root': root, 'version': version}) + '\n')
        elif build:
            self.settings_file.unlink(missing_ok=True)

    def url(self, service='http'):
        default = {'http': '8080', 'ui': '3000'}[service]
        port = self.environment.get(f'RUNYARD_LOCAL_{service.upper()}_PORT', default)
        return f'http://127.0.0.1:{port}'

    def stop(self):
        self.compose('stop', 'agent-1', 'agent-2', 'agent-3')
        # Agent-created runners are outside Compose. Scope by both label and
        # installation network so stopping one installation cannot stop another.
        running = self.run('docker', 'ps', '-q', '--filter', 'label=runyard.attempt',
                           '--filter', 'network=' + self.project, capture=True).stdout.split()
        if running:
            self.run('docker', 'stop', '--time', '10', *running)
        self.compose('stop')
        print('Stopped. Runs, artifacts, credentials, and volumes are preserved.')
