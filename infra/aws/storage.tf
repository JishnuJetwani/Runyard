resource "aws_ecr_repository" "images" {
  for_each             = toset(["server", "agent", "runner-base", "fixture", "cli"])
  name                 = "${var.name}/${each.key}"
  image_tag_mutability = "IMMUTABLE"
  image_scanning_configuration { scan_on_push = true }
  encryption_configuration { encryption_type = "AES256" }
}
resource "aws_s3_bucket" "artifacts" {
  bucket        = "${var.name}-artifacts-${data.aws_caller_identity.current.account_id}-${var.region}"
  force_destroy = var.allow_data_destruction
}
resource "aws_s3_bucket_public_access_block" "artifacts" {
  bucket                  = aws_s3_bucket.artifacts.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}
resource "aws_s3_bucket_ownership_controls" "artifacts" {
  bucket = aws_s3_bucket.artifacts.id
  rule { object_ownership = "BucketOwnerEnforced" }
}
resource "aws_s3_bucket_versioning" "artifacts" {
  bucket = aws_s3_bucket.artifacts.id
  versioning_configuration { status = "Enabled" }
}
resource "aws_s3_bucket_server_side_encryption_configuration" "artifacts" {
  bucket = aws_s3_bucket.artifacts.id
  rule {
    apply_server_side_encryption_by_default { sse_algorithm = "AES256" }
  }
}
resource "aws_s3_bucket_policy" "tls" {
  bucket = aws_s3_bucket.artifacts.id
  policy = jsonencode({ Version = "2012-10-17", Statement = [{
    Sid       = "RequireTLS", Effect = "Deny", Principal = "*", Action = "s3:*",
    Resource  = [aws_s3_bucket.artifacts.arn, "${aws_s3_bucket.artifacts.arn}/*"],
    Condition = { Bool = { "aws:SecureTransport" = "false" } }
  }] })
}
resource "aws_db_subnet_group" "main" {
  name       = var.name
  subnet_ids = aws_subnet.private[*].id
}
resource "aws_security_group" "database" {
  name_prefix = "${var.name}-database-"
  vpc_id      = aws_vpc.main.id
  description = "PostgreSQL access from EKS nodes only"
}
resource "aws_db_parameter_group" "main" {
  name_prefix = "${var.name}-"
  family      = "postgres17"
  parameter {
    name  = "rds.force_ssl"
    value = "1"
  }
}
resource "aws_db_instance" "main" {
  identifier                  = var.name
  engine                      = "postgres"
  engine_version              = var.postgres_version
  instance_class              = var.database_instance_class
  allocated_storage           = 20
  max_allocated_storage       = 50
  storage_type                = "gp3"
  storage_encrypted           = true
  db_name                     = "runyard"
  username                    = "runyard_admin"
  manage_master_user_password = true
  db_subnet_group_name        = aws_db_subnet_group.main.name
  vpc_security_group_ids      = [aws_security_group.database.id]
  parameter_group_name        = aws_db_parameter_group.main.name
  publicly_accessible         = false
  multi_az                    = false
  backup_retention_period     = 1
  deletion_protection         = !var.allow_data_destruction
  skip_final_snapshot         = var.allow_data_destruction
  final_snapshot_identifier   = "${var.name}-final"
  auto_minor_version_upgrade  = false
}
