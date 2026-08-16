# AWS deployment

Use the Terraform definitions in `infra/aws` to create the infrastructure, then
install Runyard separately. This creates billable resources.

1. Check regional costs, quotas, and supported EKS/RDS versions. Copy the example
   variables to a private file and set your operator IAM role and IPv4 CIDR.
   Review a saved Terraform plan before applying it. Keep state encrypted and
   access-controlled.
2. Apply the infrastructure and save `terraform -chdir=infra/aws output -json` to
   `.local/aws/outputs.json`. Use `aws eks update-kubeconfig` with the output
   cluster, region, and operator role to create a dedicated kubeconfig.
   Use that kubeconfig for all commands below.
3. Build and push the server and workload images to the output ECR repositories.
   Log Docker in with `aws ecr get-login-password`. Use immutable tags and record
   the image digests. Run `scripts/render-aws.py --outputs
   .local/aws/outputs.json --server-image ECR_REPOSITORY@sha256:DIGEST`. Rendering
   only writes local manifests.
4. Generate or supply a private CA and server certificate valid for
   `server.runyard.svc` and the port-forward hostname. `scripts/tls-certificates.sh`
   creates a development CA and a 30-day server certificate. Keep the CA key
   private and renew the certificate before expiry. Create the namespace, then
   a `runyard-tls` TLS Secret and a `runyard-ca` ConfigMap containing `ca.crt`.
5. Download the current RDS CA bundle from AWS's trust store and check its
   certificates. Create `runyard-rds-ca` with key `global-bundle.pem`.
   Keep database TLS verification set to `sslmode=verify-full`.
6. Retrieve the RDS master secret from the Terraform output into a local file
   with permissions 0600. Workload Pods must not have Secrets Manager access.
   Run `scripts/aws-secrets.py --outputs .local/aws/outputs.json
   --database-secret .local/aws/database-secret.json` to generate the application
   Secret, then apply it. Setup uses the managed database owner. Before broader
   use, create a restricted runtime role and keep schema ownership with the
   migration role.
7. Review and apply `.local/aws/application.yaml` (server replicas remain zero).
   Apply `.local/aws/migrate.yaml`, wait for the migration Job to complete, inspect
   its logs, then scale `deployment/server` to one replica.
8. Reach the private API with `kubectl -n runyard port-forward service/server
   8080:8080`. Configure the CLI URL as `https://localhost:8080`, owner token, and CA
   path. Submit a pinned fixture and inspect all outputs. Verify that workload Pods
   cannot use node credentials, access the Kubernetes API, or read broad S3 data.
   Exercise restart, cancellation, retries, and S3 failures. Record results and costs.

## Teardown

Stop new work, cancel remaining runs, and wait for cleanup. Export any results
you need, then remove the application namespace and review the destroy plan.
RDS and S3 data are protected by default. Set `allow_data_destruction=true` and
apply it to allow deletion. Empty ECR repositories before deleting them.

Check for remaining NAT gateways, EKS clusters, instances, RDS databases, buckets,
ECR images, snapshots, Secrets Manager secrets, and CloudWatch log groups.
Check billing for resources you chose to keep.

Deployment checks should cover IAM/OIDC, node startup and IMDS restrictions,
regional versions and quotas, ECR pulls, RDS and S3 access, TLS, and recovery timing.

References: [EKS endpoint access](https://docs.aws.amazon.com/eks/latest/userguide/cluster-endpoint.html),
[RDS certificate bundles](https://docs.aws.amazon.com/AmazonRDS/latest/UserGuide/UsingWithRDS.SSL.html).
