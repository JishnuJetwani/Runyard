"""Offline cloud manifest/credential rendering checks; never contacts AWS."""
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import urllib.parse
import yaml

with tempfile.TemporaryDirectory(prefix='runyard-deployment-') as tmp:
    root = pathlib.Path(tmp)
    outputs = {key: {'value': value} for key, value in {'region': 'us-east-1', 'artifact_bucket': 'test-bucket',
                'database_host': 'db.example.invalid', 'coordinator_role_arn': 'arn:aws:iam::123456789012:role/coordinator'}.items()}
    (root / 'outputs.json').write_text(json.dumps(outputs))
    (root / 'secret.json').write_text(json.dumps({'username': 'runtime', 'password': "spaces ' /:@? &"}))
    (root / 'credentials').write_text('RUNYARD_OWNER_TOKEN=owner\nRUNYARD_WORKER_TOKEN=worker\nRUNYARD_SIGNING_KEY=signing\n')
    subprocess.run([sys.executable, 'scripts/render-aws.py', '--outputs', str(root/'outputs.json'),
                    '--server-image', 'server@sha256:'+'a'*64, '--directory', tmp], check=True)
    resources = list(yaml.safe_load_all((root/'application.yaml').read_text()))
    server = next(r for r in resources if r['kind'] == 'Deployment')
    assert server['spec']['replicas'] == 0
    assert server['spec']['template']['spec']['containers'][0]['image'].endswith('a'*64)
    assert yaml.safe_load((root/'migrate.yaml').read_text())['spec']['template']['spec']['restartPolicy'] == 'Never'
    subprocess.run([sys.executable, 'scripts/aws-secrets.py', '--outputs', str(root/'outputs.json'), '--database-secret',
                    str(root/'secret.json'), '--credentials', str(root/'credentials'), '--output', str(root/'secrets.json')], check=True)
    assert stat.S_IMODE((root/'secrets.json').stat().st_mode) == 0o600
    uri = json.loads((root/'secrets.json').read_text())['stringData']['RUNYARD_DATABASE_URL']
    assert urllib.parse.unquote(urllib.parse.urlsplit(uri).password) == "spaces ' /:@? &"
    assert 'sslmode=verify-full' in uri
print('Cloud rendering passed; no AWS API calls were made')
