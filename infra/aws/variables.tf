variable "name" {
  type    = string
  default = "runyard-dev"
  validation {
    condition     = can(regex("^[a-z][a-z0-9-]{2,24}$", var.name))
    error_message = "Use 3..25 lowercase letters, digits, or hyphens."
  }
}
variable "region" {
  type    = string
  default = "us-east-1"
}
variable "operator_cidr" {
  description = "Public operator IPv4 CIDR permitted to contact the EKS API."
  type        = string
  validation {
    condition     = can(cidrhost(var.operator_cidr, 0)) && can(regex("/(2[4-9]|3[0-2])$", var.operator_cidr))
    error_message = "Supply a narrow IPv4 CIDR (/24 through /32), never 0.0.0.0/0."
  }
}
variable "operator_role_arn" {
  description = "Existing IAM role granted EKS administrative access."
  type        = string
}
variable "vpc_cidr" {
  type    = string
  default = "10.42.0.0/16"
}
variable "postgres_version" {
  type    = string
  default = "17.6"
}
variable "database_instance_class" {
  type    = string
  default = "db.t4g.micro"
}
variable "kubernetes_version" {
  type    = string
  default = "1.34"
}
variable "node_instance_type" {
  type    = string
  default = "t3.medium"
}
variable "node_count" {
  type    = number
  default = 2
  validation {
    condition     = var.node_count >= 1 && var.node_count <= 4 && floor(var.node_count) == var.node_count
    error_message = "Use 1..4 development nodes."
  }
}
variable "allow_data_destruction" {
  description = "Explicitly permit disposable database/bucket deletion during teardown."
  type        = bool
  default     = false
}
