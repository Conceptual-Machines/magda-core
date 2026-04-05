#!/usr/bin/env bash
set -euo pipefail

PR_NUMBER="${1:?Usage: release-after-ci.sh <pr-number> <version-tag>}"
VERSION_TAG="${2:?Usage: release-after-ci.sh <pr-number> <version-tag>}"

echo "Waiting for PR #${PR_NUMBER} CI to pass..."

# Wait for all checks to pass (polls every 60s, times out after 60min)
if ! gh pr checks "$PR_NUMBER" --watch --fail-fast; then
    echo "CI failed on PR #${PR_NUMBER}. Aborting release."
    exit 1
fi

echo "CI passed. Merging PR #${PR_NUMBER}..."
gh pr merge "$PR_NUMBER" --merge --delete-branch

echo "Pulling latest main..."
git checkout main
git pull origin main

echo "Tagging ${VERSION_TAG} on main..."
git tag "$VERSION_TAG"
git push origin "$VERSION_TAG"

echo "Done. Release ${VERSION_TAG} tagged and pushed."
