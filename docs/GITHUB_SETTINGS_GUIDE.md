# GitHub Repository Settings Configuration Guide

This guide provides step-by-step instructions for repository administrators to configure GitHub settings to fully enable the branch protection and security measures.

## Prerequisites

- You must have **Admin** access to the repository
- The security workflows must be merged to the main branch
- At least one successful CI run should have completed

## Step 1: Configure Branch Protection Rules

1. Navigate to your repository on GitHub
2. Go to **Settings** → **Branches**
3. Click **Add rule** (or edit the existing rule for `main`)

### Branch Protection Settings

Configure the following settings for the `main` branch:

#### Branch name pattern
```
main
```

#### Protect matching branches

**Require a pull request before merging** ✅
- ☑️ Require approvals: **1** (or more)
- ☑️ Dismiss stale pull request approvals when new commits are pushed
- ☑️ Require review from Code Owners (if you have a CODEOWNERS file)

**Require status checks to pass before merging** ✅
- ☑️ Require branches to be up to date before merging

**Required status checks** (select all that apply after first successful CI run):
- ☑️ `build-and-test-linux` (from ci.yml)
- ☑️ `code-quality` (from ci.yml)
- ☑️ `security-checks` (from ci.yml)
- ☑️ `scan-secrets` (from secret-scanning.yml)
- ☑️ `analyze / Analyze C++ Code` (from codeql.yml)
- ☑️ `enforce-branch-protection` (from branch-protection.yml)

**Require conversation resolution before merging** ✅
- Ensures all review comments are addressed

**Require signed commits** ⚠️ (Optional but Recommended)
- Verifies commit authenticity

**Require linear history** ⚠️ (Optional)
- Prevents merge commits (requires rebase)

**Do not allow bypassing the above settings** ✅
- ☑️ **CRITICAL**: Ensure this is checked
- Even administrators must follow the rules

**Allow force pushes** ❌
- Keep this DISABLED to prevent history rewriting

**Allow deletions** ❌
- Keep this DISABLED to prevent accidental branch deletion

4. Click **Create** or **Save changes**

## Step 2: Enable Security Features

### Enable Dependabot

1. Go to **Settings** → **Code security and analysis**
2. Enable the following:

**Dependency graph**
- ☑️ Enable

**Dependabot alerts**
- ☑️ Enable
- Automatically receive alerts about vulnerable dependencies

**Dependabot security updates**
- ☑️ Enable
- Automatically creates PRs to update vulnerable dependencies

**Grouped security updates**
- ☑️ Enable (if available)
- Groups related security updates into single PRs

### Enable CodeQL Analysis

**CodeQL analysis**
- This is automatically configured via `.github/workflows/codeql.yml`
- Verify it appears under **Settings** → **Code security and analysis** → **Code scanning**
- After first run, you'll see results under **Security** → **Code scanning**

### Enable Secret Scanning

**Secret scanning**
- ☑️ Enable secret scanning
- GitHub will scan for known secret patterns

**Push protection**
- ☑️ Enable push protection (GitHub Pro/Enterprise)
- Prevents accidental push of secrets

If using GitHub Free, the secret scanning workflow (`.github/workflows/secret-scanning.yml`) provides similar protection via CI.

### Enable Private Vulnerability Reporting

1. Go to **Settings** → **Code security and analysis**
2. Scroll to **Private vulnerability reporting**
3. ☑️ Enable
4. This allows security researchers to privately report vulnerabilities

## Step 3: Configure Notifications

### Security Alerts

1. Go to **Settings** → **Notifications**
2. Configure how you want to receive security alerts:
   - ☑️ Email notifications for Dependabot alerts
   - ☑️ Email notifications for code scanning alerts
   - ☑️ Email notifications for secret scanning alerts

### Watch Repository

1. Click **Watch** at the top of the repository
2. Select **Custom** and enable:
   - ☑️ Security alerts
   - ☑️ All activity (if you want all notifications)

## Step 4: Set Up Code Owners (Optional but Recommended)

Create a `.github/CODEOWNERS` file to specify who must review certain parts of the codebase:

```
# Default owner for everything
* @owner-username

# Security-sensitive files
/.github/workflows/* @owner-username @security-team
/SECURITY.md @owner-username @security-team
/.github/dependabot.yml @owner-username

# Core C++ code
/magda/daw/*.cpp @cpp-maintainers
/magda/agents/*.cpp @cpp-maintainers
```

## Step 5: Test the Configuration

### Test Branch Protection

1. Create a test branch:
   ```bash
   git checkout -b test/branch-protection
   echo "test" > test-file.txt
   git add test-file.txt
   git commit -m "test: verify branch protection"
   git push origin test/branch-protection
   ```

2. Create a Pull Request targeting `main`
3. Verify that:
   - ☑️ CI workflows run automatically
   - ☑️ All required status checks appear
   - ☑️ "Merge" button is disabled until checks pass
   - ☑️ At least one approval is required

4. Try to push directly to main (should fail):
   ```bash
   git checkout main
   echo "test" > test-direct.txt
   git add test-direct.txt
   git commit -m "test: try direct push"
   git push origin main
   ```
   
   Expected result: ❌ Push rejected
   ```
   ! [remote rejected] main -> main (protected branch hook declined)
   ```

### Test Secret Scanning

1. Create a test branch with a fake secret:
   ```bash
   git checkout -b test/secret-detection
   echo 'aws_key = "AKIAIOSFODNN7EXAMPLE"' > test-secret.cpp
   git add test-secret.cpp
   git commit -m "test: secret detection"
   git push origin test/secret-detection
   ```

2. Create a Pull Request
3. Verify that:
   - ☑️ Secret scanning workflow fails
   - ☑️ PR cannot be merged
   - ☑️ Security alert is generated (if GitHub secret scanning is enabled)

4. Clean up:
   ```bash
   git checkout main
   git branch -D test/secret-detection
   git push origin --delete test/secret-detection
   ```

## Step 6: Document Access Control

### Repository Roles

Ensure team members have appropriate access levels:

1. Go to **Settings** → **Collaborators and teams**
2. Configure access:
   - **Read**: Can view and clone
   - **Triage**: Can manage issues and PRs without code access
   - **Write**: Can push to non-protected branches
   - **Maintain**: Can manage repository without access to sensitive settings
   - **Admin**: Full access including settings

### Recommended Setup

- **Core Maintainers**: Admin access
- **Regular Contributors**: Write access
- **Code Reviewers**: Write access (for reviewing and merging)
- **External Contributors**: Fork and PR workflow (no direct access)

## Step 7: Create a Security Response Team

1. Consider creating a **@security-team** GitHub team
2. Add trusted maintainers to this team
3. Grant them access to security advisories
4. Reference them in CODEOWNERS for security-sensitive files

## Verification Checklist

Use this checklist to verify everything is configured correctly:

### Branch Protection
- [ ] Main branch cannot be deleted
- [ ] Main branch cannot receive direct pushes
- [ ] Force pushes to main are disabled
- [ ] Pull requests require at least 1 approval
- [ ] All required status checks must pass
- [ ] Branches must be up to date before merging
- [ ] Review comments must be resolved before merging
- [ ] Administrators cannot bypass rules

### Security Features
- [ ] Dependabot alerts enabled
- [ ] Dependabot security updates enabled
- [ ] CodeQL analysis workflow running
- [ ] Secret scanning workflow running
- [ ] Private vulnerability reporting enabled
- [ ] Security notifications configured

### Workflows
- [ ] CI workflow runs on all PRs
- [ ] Security checks run on all PRs to main
- [ ] CodeQL scans run weekly
- [ ] Secret scanning runs on PRs
- [ ] All workflows have appropriate permissions

### Documentation
- [ ] SECURITY.md is present and up to date
- [ ] BRANCH_PROTECTION.md is present
- [ ] README.md links to security documentation
- [ ] CODEOWNERS file configured (optional)

## Troubleshooting

### Status Checks Not Appearing

**Problem**: Required status checks don't appear in the list.

**Solution**:
1. Ensure workflows have run at least once successfully
2. Check workflow files are in `.github/workflows/` on main branch
3. Wait a few minutes for GitHub to detect new workflows
4. Try triggering workflows manually via Actions tab

### Cannot Enable Push Protection

**Problem**: Push protection option is not available.

**Solution**:
- Push protection requires GitHub Pro, Team, or Enterprise
- Use the secret scanning workflow (already included) as an alternative
- It provides similar protection via CI checks

### Checks Failing on Every PR

**Problem**: Required checks always fail.

**Solution**:
1. Check the Actions tab for error details
2. Verify dependencies are installed correctly
3. Ensure CMake configuration is correct
4. Check if tests are passing locally
5. Review workflow logs for specific errors

### Team Members Cannot Merge

**Problem**: PRs with approval still cannot be merged.

**Solution**:
1. Verify all required status checks have passed
2. Check that branch is up to date with main
3. Ensure all review comments are resolved
4. Verify the approving reviewer has sufficient permissions

## Maintenance

### Regular Reviews

- **Weekly**: Review Dependabot PRs and security alerts
- **Monthly**: Review access control and team permissions
- **Quarterly**: Review and update security policies
- **Annually**: Full security audit and policy review

### Updating Workflows

When updating security workflows:

1. Test changes in a feature branch first
2. Verify workflows pass before merging
3. Update required status checks if workflow names change
4. Document changes in PR description
5. Notify team of any new requirements

### Handling Security Alerts

When a security alert is generated:

1. **Immediate**: Review the alert details
2. **Within 24h**: Assess severity and impact
3. **Within 1 week**: Create fix or mitigation plan
4. **Within 2 weeks**: Deploy fix for high/critical issues
5. **Document**: Update SECURITY.md if needed

## Support

If you encounter issues configuring these settings:

1. Check GitHub's documentation: https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches
2. Review workflow logs in the Actions tab
3. Check the repository's Security tab for alerts
4. Open an issue in the repository for team discussion
5. Contact GitHub Support for GitHub-specific issues

## Resources

- [GitHub Branch Protection Documentation](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches)
- [GitHub Code Security Documentation](https://docs.github.com/en/code-security)
- [CodeQL Documentation](https://codeql.github.com/docs/)
- [Dependabot Documentation](https://docs.github.com/en/code-security/dependabot)
- [OWASP Secure Coding Practices](https://owasp.org/www-project-secure-coding-practices-quick-reference-guide/)

---

**Last Updated**: 2026-01-30  
**Applies To**: MAGDA Core Repository  
**Maintainer**: Repository Administrators
