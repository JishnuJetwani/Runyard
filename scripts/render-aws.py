#!/usr/bin/env python3
"""Render reviewable installation manifests from Terraform outputs; no AWS calls."""
import argparse
import copy
import json
import pathlib
import re
import subprocess
import yaml

parser = argparse.ArgumentParser()
parser.add_argument('--outputs', required=True)
parser.add_argument('--server-image', required=True)
parser.add_argument('--directory', default='.local/aws')
args = parser.parse_args()
if not re.fullmatch(r'[^\s]+@sha256:[a-f0-9]{64}', args.server_image):
    parser.error('server image must be pinned by SHA-256 digest')
outputs = {key: item['value'] for key, item in json.loads(pathlib.Path(args.outputs).read_text()).items()}
root = pathlib.Path(__file__).resolve().parents[1]
rendered = subprocess.check_output(['kubectl', 'kustomize', str(root / 'deploy/kubernetes/overlays/aws')], text=True)
resources = list(yaml.safe_load_all(rendered))
for resource in resources:
    if resource['kind'] == 'ConfigMap' and resource['metadata']['name'] == 'runyard-config':
        resource['data'].update(AWS_REGION=outputs['region'], RUNYARD_S3_BUCKET=outputs['artifact_bucket'])
    if resource['kind'] == 'ServiceAccount' and resource['metadata']['name'] == 'coordinator':
        resource['metadata']['annotations'] = {'eks.amazonaws.com/role-arn': outputs['coordinator_role_arn']}
    if resource['kind'] == 'Deployment':
        resource['spec']['template']['spec']['containers'][0]['image'] = args.server_image
        template = copy.deepcopy(resource['spec']['template'])
container = template['spec']['containers'][0]
container['args'] = ['migrate', '--directory', '/opt/runyard/migrations']
for key in ('readinessProbe', 'livenessProbe', 'ports'):
    container.pop(key, None)
template['spec']['restartPolicy'] = 'Never'
template['spec']['automountServiceAccountToken'] = False
template['spec']['serviceAccountName'] = 'workload'
job = {'apiVersion': 'batch/v1', 'kind': 'Job', 'metadata': {'name': 'migrate', 'namespace': 'runyard'},
       'spec': {'backoffLimit': 0, 'template': template}}
directory = pathlib.Path(args.directory)
directory.mkdir(parents=True, exist_ok=True)
(directory / 'application.yaml').write_text(yaml.safe_dump_all(resources, sort_keys=False))
(directory / 'migrate.yaml').write_text(yaml.safe_dump(job, sort_keys=False))
print('Rendered application.yaml and migrate.yaml in', directory)
