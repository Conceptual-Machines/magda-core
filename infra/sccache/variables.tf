variable "region" {
  description = "AWS region for the sccache bucket. Kept close to where CI runs to minimise cache round-trip latency."
  type        = string
  default     = "eu-west-2"
}

variable "github_repo" {
  description = "owner/name of the repo whose GitHub Actions runs may assume the cache role."
  type        = string
  default     = "Conceptual-Machines/magda-core"
}

variable "bucket_prefix" {
  description = "Prefix for the cache bucket name; the account id is appended for global uniqueness."
  type        = string
  default     = "magda-ci-sccache"
}

variable "role_name" {
  description = "Name of the IAM role GitHub Actions assumes via OIDC."
  type        = string
  default     = "magda-ci-sccache"
}

variable "object_expiry_days" {
  description = "Days after which cached objects expire, so the bucket self-prunes and stays fresh/cheap."
  type        = number
  default     = 14
}

# --- Cloudflare R2 ---------------------------------------------------------------

variable "cloudflare_account_id" {
  description = "Cloudflare account that owns the R2 cache bucket. No default: it is account-specific and must be supplied explicitly."
  type        = string
}

variable "r2_bucket_name" {
  description = "Name of the R2 cache bucket. R2 names are account-scoped, so no account id suffix is needed for uniqueness."
  type        = string
  default     = "magda-ci-sccache"
}

variable "r2_location" {
  description = "R2 location hint. weur matches where the S3 bucket lived; enam may cut round-trip latency if GitHub-hosted runners are US-based."
  type        = string
  default     = "weur"
}
