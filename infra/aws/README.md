# AWS development infrastructure

This directory defines two public and two private subnets, one NAT gateway, ECR,
an encrypted private S3 bucket, and private single-AZ RDS PostgreSQL. Database
master credentials are managed by Secrets Manager and are not Terraform inputs.
Database connections require TLS with certificate verification.

Terraform 1.13.3 and pinned AWS 6.10.0/TLS 4.1.0 provider definitions have passed
`terraform init -backend=false`, `terraform fmt -check`, and `terraform validate`
locally. The deployment procedure covers quotas, instance/version availability,
IAM access, and network connectivity.

Copy `terraform.tfvars.example` to a private local variables file and replace both
operator values. Keep state, plans, credentials, and generated outputs outside Git.
A future live deployment must first estimate current costs for EKS, NAT, nodes,
RDS, storage, and logs. One NAT and single-AZ RDS are deliberate development cost
choices; this is not a highly available production layout.

S3 deletion and RDS destruction are protected by default. A deliberate disposable
teardown can set `allow_data_destruction=true`; doing so permits loss of stored
experiment outputs and database history. Preserve anything needed before teardown.
