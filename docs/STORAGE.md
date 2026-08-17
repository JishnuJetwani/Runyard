# Artifact storage

The default filesystem adapter stores objects below `RUNYARD_ARTIFACT_ROOT`.
Select S3 with `RUNYARD_STORAGE=s3`, `RUNYARD_S3_BUCKET`, and `AWS_REGION`.
The coordinator uses the AWS SDK default credential chain; workloads never
receive those credentials. `RUNYARD_S3_ENDPOINT` is intended for emulator tests;
plaintext endpoints require `RUNYARD_PROFILE=development`.

Uploads stream to a temporary file. The coordinator checks SHA-256, stores the
complete object, then checks attempt ownership again before saving its metadata.
Object keys include the attempt, path, and content. An interrupted upload can leave
an object without metadata, but cannot publish a partial file. Downloads are
verified before they enter the local cache.

Run `PYTHON=.local/venv/bin/python scripts/test-s3.sh` to test the adapter against
pinned Moto 5.1.22. The round trip, duplicate upload, missing object, invalid source,
and path validation checks have passed locally. This tests application behavior,
not AWS IAM, TLS routing, or actual S3 service access.

Staging files, downloaded objects, and orphaned objects require operator-managed
retention in v1. Do not delete objects referenced by the artifacts table.

The runner checks the canonical working root, output-directory identity, and every
file/archive before upload. Symlinks are rejected even when the workload replaces
the output directory itself. The TLS runner integration test includes that case
and verifies that no external file is published.
