#!/usr/bin/env python3
"""Build an untracked Kubernetes Secret from local credential files, without printing values."""
import argparse
import json
import os
import pathlib
import urllib.parse

parser = argparse.ArgumentParser()
parser.add_argument('--outputs', required=True)
parser.add_argument('--database-secret', required=True)
parser.add_argument('--credentials', default='.env')
parser.add_argument('--output', default='.local/aws/secrets.json')
args = parser.parse_args()
outputs = {key: item['value'] for key, item in json.loads(pathlib.Path(args.outputs).read_text()).items()}
secret = json.loads(pathlib.Path(args.database_secret).read_text())
if 'SecretString' in secret:
    secret = json.loads(secret['SecretString'])
values = dict(line.split('=', 1) for line in pathlib.Path(args.credentials).read_text().splitlines() if '=' in line and not line.startswith('#'))
values = {key: values[key] for key in ('RUNYARD_OWNER_TOKEN', 'RUNYARD_WORKER_TOKEN', 'RUNYARD_SIGNING_KEY')}
quote = urllib.parse.quote
values['RUNYARD_DATABASE_URL'] = ('postgresql://' + quote(secret['username'], safe='') + ':' + quote(secret['password'], safe='') + '@' + outputs['database_host'] +
                                 ':5432/runyard?sslmode=verify-full&sslrootcert=/etc/runyard/rds/global-bundle.pem&connect_timeout=3')
path = pathlib.Path(args.output)
path.parent.mkdir(parents=True, exist_ok=True)
with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), 'w') as file:
    os.chmod(path, 0o600)
    json.dump({'apiVersion': 'v1', 'kind': 'Secret', 'metadata': {'name': 'runyard-secrets', 'namespace': 'runyard'}, 'stringData': values}, file)
print('Wrote private credential file:', path)
