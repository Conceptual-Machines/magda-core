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
