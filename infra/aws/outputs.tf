output "region" { value = var.region }
output "cluster_name" { value = aws_eks_cluster.main.name }
output "coordinator_role_arn" { value = aws_iam_role.coordinator.arn }
output "artifact_bucket" { value = aws_s3_bucket.artifacts.id }
output "database_host" { value = aws_db_instance.main.address }
output "database_secret_arn" { value = aws_db_instance.main.master_user_secret[0].secret_arn }
output "repositories" { value = { for name, repo in aws_ecr_repository.images : name => repo.repository_url } }
