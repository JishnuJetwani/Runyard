terraform {
  required_version = ">= 1.13.3, < 2.0.0"
  required_providers {
    aws = { source = "hashicorp/aws", version = "6.10.0" }
    tls = { source = "hashicorp/tls", version = "4.1.0" }
  }
}
provider "aws" {
  region = var.region
  default_tags { tags = { Project = "Runyard", Environment = "development" } }
}
data "aws_availability_zones" "available" { state = "available" }
data "aws_caller_identity" "current" {}
data "aws_partition" "current" {}
