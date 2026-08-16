# AWS development infrastructure

This directory defines two public and two private subnets, one NAT gateway, ECR,
an encrypted private S3 bucket, and private single-AZ RDS PostgreSQL. Database
master credentials are managed by Secrets Manager and are not Terraform inputs.
Database connections require TLS with certificate verification.

Terraform 1.13.3 and pinned AWS 6.10.0/TLS 4.1.0 provider definitions have passed
`terraform init -backend=false`, `terraform fmt -check`, and `terraform validate`
locally. The deployment procedure covers quotas, instance/version availability,
IAM access, and network connectivity.

Copy `terraform.tfvars.example` to a private variables file and replace both
operator values. Keep state, plans, credentials, and generated output outside Git.
Before deploying, estimate costs for EKS, NAT, nodes, RDS, storage, and logs.
One NAT gateway and single-AZ RDS keep development costs lower but do not provide
a highly available setup.

S3 and RDS data are protected from deletion by default. Set
`allow_data_destruction=true` to allow teardown. Save any results and history you
need first.

EKS uses private endpoint access plus a public endpoint restricted to the operator
CIDR. The configured IAM role gets cluster access; the creating identity does not
automatically get admin access. On-demand CPU nodes run in private subnets and
require IMDSv2 with a hop limit of one. Only the coordinator service account can
assume the artifact role through IRSA. Workload Pods have no IAM role annotation
or Kubernetes credentials. Check these boundaries during deployment.
Runyard is for trusted workloads.

The node default is x86-64 `t3.medium`; use an x86-64-compatible instance type when
changing its size. Application images support arm64, but this node group uses an
x86-64 AMI.

References: [EKS service-account IAM](https://docs.aws.amazon.com/eks/latest/userguide/iam-roles-for-service-accounts.html),
[EKS identity guidance](https://docs.aws.amazon.com/eks/latest/best-practices/identity-and-access-management.html).
