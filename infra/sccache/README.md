# CI sccache S3 cache

Terraform for the shared compile cache the Linux CI jobs use via
[sccache](https://github.com/mozilla/sccache) with an S3 backend. This replaces
the `actions/cache`-backed ccache, which was branch-scoped and capped at the
10 GB per-repo cache budget, so Linux runs were almost always cold.

## What it creates

- An S3 bucket (`magda-ci-sccache-<account-id>`) with public access blocked and
  a 14-day object-expiry lifecycle rule.
- An IAM role assumed by GitHub Actions via OIDC, trust-scoped to
  `repo:Conceptual-Machines/magda-core:*`, with least-privilege
  `GetObject`/`PutObject`/`ListBucket` on that one bucket.

The account-global GitHub OIDC provider already exists and is referenced, not
managed here.

## Usage

```sh
cd infra/sccache
terraform init
terraform plan      # review the trust policy before applying
terraform apply
```

State is local (`terraform.tfstate`, gitignored). The resources are static and
rarely change; if this grows, migrate to an S3 state backend.

## Wiring into CI

After `apply`, feed the outputs to the Linux workflow jobs:

- `SCCACHE_BUCKET` = `bucket_name`
- `SCCACHE_REGION` = `region`
- `configure-aws-credentials` `role-to-assume` = `role_arn`

The job needs `permissions: id-token: write` for OIDC, and builds with
`-DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache`.
