#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python_bin=${PYTHON:-python3}
docker compose -f deploy/moto.compose.yaml up -d
trap 'docker compose -f deploy/moto.compose.yaml down' EXIT
export AWS_ACCESS_KEY_ID=testing AWS_SECRET_ACCESS_KEY=testing AWS_REGION=us-east-1
export RUNYARD_TEST_S3_ENDPOINT=http://127.0.0.1:55001
"$python_bin" - <<'PY'
import boto3, os, time
client = boto3.client('s3', endpoint_url=os.environ['RUNYARD_TEST_S3_ENDPOINT'])
for attempt in range(30):
    try:
        client.create_bucket(Bucket='runyard-test')
        break
    except Exception:
        if attempt == 29:
            raise
        time.sleep(1)
PY
ctest --test-dir "${RUNYARD_BUILD_DIR:-build/dev}" -R '^S3\.' --output-on-failure
