output "bucket_name" {
  description = "S3 bucket for sccache objects (set as SCCACHE_BUCKET in CI)."
  value       = aws_s3_bucket.sccache.id
}

output "region" {
  description = "Bucket region (set as SCCACHE_REGION in CI)."
  value       = var.region
}

output "role_arn" {
  description = "IAM role ARN GitHub Actions assumes via OIDC (role-to-assume in configure-aws-credentials)."
  value       = aws_iam_role.sccache.arn
}
