data "tls_certificate" "oidc" {
  url = aws_eks_cluster.main.identity[0].oidc[0].issuer
}
resource "aws_iam_openid_connect_provider" "cluster" {
  url             = aws_eks_cluster.main.identity[0].oidc[0].issuer
  client_id_list  = ["sts.amazonaws.com"]
  thumbprint_list = [data.tls_certificate.oidc.certificates[length(data.tls_certificate.oidc.certificates) - 1].sha1_fingerprint]
}
locals {
  oidc_issuer = replace(aws_eks_cluster.main.identity[0].oidc[0].issuer, "https://", "")
}
resource "aws_iam_role" "coordinator" {
  name = "${var.name}-coordinator"
  assume_role_policy = jsonencode({ Version = "2012-10-17", Statement = [{
    Effect = "Allow", Principal = { Federated = aws_iam_openid_connect_provider.cluster.arn },
    Action = "sts:AssumeRoleWithWebIdentity",
    Condition = { StringEquals = {
      "${local.oidc_issuer}:sub" = "system:serviceaccount:runyard:coordinator",
      "${local.oidc_issuer}:aud" = "sts.amazonaws.com"
    } }
  }] })
}
resource "aws_iam_role_policy" "artifacts" {
  name = "artifact-objects"
  role = aws_iam_role.coordinator.id
  policy = jsonencode({ Version = "2012-10-17", Statement = [{
    Effect = "Allow", Action = ["s3:GetObject", "s3:PutObject"], Resource = "${aws_s3_bucket.artifacts.arn}/*"
  }] })
}
