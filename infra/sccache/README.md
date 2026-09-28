# CI sccache cache

Terraform for the shared compile cache the Linux CI jobs use via
[sccache](https://github.com/mozilla/sccache). This replaces the
`actions/cache`-backed ccache, which was branch-scoped and capped at the 10 GB
per-repo cache budget, so Linux runs were almost always cold.

The backend is Cloudflare R2, reached through sccache's S3-compatible client.

## Why R2 and not S3

The cache started on S3 in eu-west-2. Both consuming jobs (`ci.yml`
`build-and-test-linux` and `juce-tests-linux.yml`) run on `ubuntu-latest`, i.e.
GitHub-hosted runners outside AWS, so every cache hit was billed egress at
$0.09/GB. A healthy cache made that worse, not better:

- ~87% hit rate, ~1267 objects pulled per job, ~1.2 GB per job
- ~356 GB in the first month, trending to ~800 GB/month
- storage was only $4.34/mo of it; the rest was transfer

Expiring objects sooner does not help, because egress is driven by hits on the
objects the current tree needs, not by how much history sits in the bucket.
Deleting those just converts hits into misses. R2 removes the per-GB charge
instead of rationing the cache: at this volume the reads and writes sit inside
R2's free operation tiers, so the bill is storage only.

## What it creates

- An R2 bucket (`magda-ci-sccache`) with a 14-day object-expiry lifecycle rule.
- The superseded S3 bucket and GitHub OIDC role, still present but marked
  pending decommission (see below).

## One-time Cloudflare setup

The bucket is Terraform-managed, but the credentials are not: R2 API tokens
cannot be created through the S3-compatible API, and R2 has no OIDC federation,
so CI authenticates with a scoped token rather than an assumed role.

1. In the Cloudflare dashboard, note the **account ID** (R2 > Overview).
2. Create an API token for Terraform with **Workers R2 Storage: Edit**, and
   export it as `CLOUDFLARE_API_TOKEN`.
3. Apply (see Usage) to create the bucket.
4. In **R2 > Manage API Tokens**, create an S3-compatible token scoped to
   *Object Read & Write* on `magda-ci-sccache` only. This yields an
   **Access Key ID** and a **Secret Access Key**, shown once.

## Usage

```sh
cd infra/sccache
export CLOUDFLARE_API_TOKEN=...
terraform init          # picks up the cloudflare provider
terraform plan -var cloudflare_account_id=...
terraform apply -var cloudflare_account_id=...
```

State is local (`terraform.tfstate`, gitignored). The resources are static and
rarely change; if this grows, migrate to a remote state backend.

## Also used by Windows CI

`build-and-test-windows` uses the same bucket and the same secrets: sccache
under key prefix `windows` (Linux uses `linux`), and vcpkg's S3-compatible
binary cache under the `vcpkg/` prefix for its libxml2 archives. No Terraform
change is needed for either; it is the same bucket and token, just more
prefixes in it. The 14-day object expiry applies there too.

## Wiring into CI

Set these repository secrets:

| Secret                 | Value                                            |
| ---------------------- | ------------------------------------------------ |
| `R2_BUCKET`            | `magda-ci-sccache` (the `r2_bucket_name` output) |
| `R2_ACCOUNT_ID`        | Cloudflare account ID                            |
| `R2_ACCESS_KEY_ID`     | from the R2 S3-compatible token                  |
| `R2_SECRET_ACCESS_KEY` | from the R2 S3-compatible token                  |

`R2_BUCKET` rather than reusing `SCCACHE_BUCKET`: repository secrets are shared
by every branch, and `SCCACHE_BUCKET` still points at the S3 bucket that CI on
`dev` and `main` uses until this lands. Repointing it would break their cache
mid-flight. `SCCACHE_BUCKET` retires with the S3 decommission below.

The jobs need no `id-token` permission any more. sccache is pointed at R2 with
`SCCACHE_ENDPOINT=https://<account-id>.r2.cloudflarestorage.com` and
`SCCACHE_REGION=auto` (R2 has no regions), and builds with
`-DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache`.

Runs without those secrets (Dependabot) skip the setup step and build cold
rather than failing.

## Verifying the migration

The first Linux run after the secrets are set is the real test; none of this is
checkable locally. Look at the `Show sccache statistics` step:

- The first run is expected to be nearly all misses, since R2 starts empty.
- The run after it should show hits climbing back toward ~87%.

Until hits recover, leave the S3 bucket in place.

## Decommissioning S3

Once a Linux run reports hits against R2, remove from `main.tf` the
`aws_s3_bucket`, `aws_s3_bucket_public_access_block`,
`aws_s3_bucket_lifecycle_configuration`, the whole OIDC role section and the
`aws` provider, drop the `legacy_*` outputs, then `terraform apply` to destroy
them. Delete the now-unused `AWS_SCCACHE_ROLE_ARN` and `SCCACHE_BUCKET`
repository secrets at the same time.

Note that `cloudflare_r2_bucket_lifecycle` cannot be destroyed through
Terraform; the provider warns about this on create. Removing the rule means
deleting it in the dashboard.
