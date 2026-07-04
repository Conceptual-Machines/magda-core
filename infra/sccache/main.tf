provider "aws" {
  region = var.region
}

data "aws_caller_identity" "current" {}

# The GitHub Actions OIDC provider is account-global and already exists in this
# account, so reference it rather than manage a second copy.
data "aws_iam_openid_connect_provider" "github" {
  url = "https://token.actions.githubusercontent.com"
}

locals {
  bucket_name = "${var.bucket_prefix}-${data.aws_caller_identity.current.account_id}"
}

# --- S3 bucket holding the sccache compile-cache objects ------------------------

resource "aws_s3_bucket" "sccache" {
  bucket = local.bucket_name

  tags = {
    Project = "magda"
    Purpose = "ci-sccache"
  }
}

resource "aws_s3_bucket_public_access_block" "sccache" {
  bucket                  = aws_s3_bucket.sccache.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

# Compile-cache objects are disposable; expire them so the bucket self-prunes
# and does not accumulate cost or stale entries.
resource "aws_s3_bucket_lifecycle_configuration" "sccache" {
  bucket = aws_s3_bucket.sccache.id

  rule {
    id     = "expire-sccache-objects"
    status = "Enabled"

    filter {} # whole bucket

    expiration {
      days = var.object_expiry_days
    }
  }
}

# --- IAM role assumed by GitHub Actions via OIDC --------------------------------

data "aws_iam_policy_document" "assume" {
  statement {
    effect  = "Allow"
    actions = ["sts:AssumeRoleWithWebIdentity"]

    principals {
      type        = "Federated"
      identifiers = [data.aws_iam_openid_connect_provider.github.arn]
    }

    # Standard audience for AWS federation.
    condition {
      test     = "StringEquals"
      variable = "token.actions.githubusercontent.com:aud"
      values   = ["sts.amazonaws.com"]
    }

    # Scope to this repo only (any branch/tag/PR). CI runs on all branches, so
    # the subject is left wildcarded past the repo path.
    condition {
      test     = "StringLike"
      variable = "token.actions.githubusercontent.com:sub"
      values   = ["repo:${var.github_repo}:*"]
    }
  }
}

resource "aws_iam_role" "sccache" {
  name               = var.role_name
  description        = "GitHub Actions OIDC role for the CI sccache S3 cache (${var.github_repo})."
  assume_role_policy = data.aws_iam_policy_document.assume.json
}

# Least privilege: read/write cache objects and list the one bucket, nothing else.
data "aws_iam_policy_document" "sccache" {
  statement {
    sid       = "SccacheObjectReadWrite"
    effect    = "Allow"
    actions   = ["s3:GetObject", "s3:PutObject"]
    resources = ["${aws_s3_bucket.sccache.arn}/*"]
  }

  statement {
    sid       = "SccacheListBucket"
    effect    = "Allow"
    actions   = ["s3:ListBucket"]
    resources = [aws_s3_bucket.sccache.arn]
  }
}

resource "aws_iam_role_policy" "sccache" {
  name   = "sccache-s3-access"
  role   = aws_iam_role.sccache.id
  policy = data.aws_iam_policy_document.sccache.json
}
