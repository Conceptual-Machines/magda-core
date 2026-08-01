output "r2_bucket_name" {
  description = "R2 bucket for sccache objects (set as the SCCACHE_BUCKET secret in CI)."
  value       = cloudflare_r2_bucket.sccache.name
}

output "r2_endpoint" {
  description = "S3-compatible endpoint sccache talks to (SCCACHE_ENDPOINT is derived from the R2_ACCOUNT_ID secret in CI)."
  value       = "https://${var.cloudflare_account_id}.r2.cloudflarestorage.com"
}

# --- Pending decommission with the S3 bucket -------------------------------------

output "legacy_s3_bucket_name" {
  description = "Superseded S3 cache bucket; drop with the aws_s3_bucket block once R2 is verified."
  value       = aws_s3_bucket.sccache.id
}

output "legacy_role_arn" {
  description = "Superseded OIDC role; drop with the aws_iam_role block once R2 is verified."
  value       = aws_iam_role.sccache.arn
}
