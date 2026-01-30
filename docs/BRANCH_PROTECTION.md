# Branch Protection and Security Architecture

This document describes the comprehensive security measures implemented to protect the MAGDA repository, especially the `main` branch, from malicious attacks.

## Overview

MAGDA implements a multi-layered security approach combining automated scanning, code review requirements, and CI/CD enforcement to ensure code quality and security.

## Branch Protection Strategy

### Main Branch Protection

The `main` branch is protected through a combination of:

1. **GitHub Branch Protection Rules** (configured in repository settings)
   - Require pull request reviews before merging
   - Require status checks to pass before merging
   - Require branches to be up to date before merging
   - Prevent force pushes
   - Prevent branch deletion

2. **Automated Security Checks** (CI/CD workflows)
   - All checks must pass before merge is allowed
   - No bypassing allowed for any contributor

3. **Code Review Requirements**
   - Minimum one approval from maintainer required
   - Reviewers must verify security implications
   - All comments must be resolved

## Security Scanning Workflows

### 1. CodeQL Security Analysis (`.github/workflows/codeql.yml`)

**Purpose**: Automated static analysis for security vulnerabilities in C++ code.

**Triggers**:
- Push to `main` or `develop` branches
- Pull requests targeting `main` or `develop`
- Weekly scheduled scan (every Monday at 2 AM UTC)
- Manual trigger

**What it checks**:
- Buffer overflows and underflows
- Use-after-free vulnerabilities
- Null pointer dereferences
- SQL injection patterns
- Command injection patterns
- Insecure cryptography usage
- Race conditions
- Memory leaks
- Uninitialized variables

**Technology**: GitHub CodeQL with `security-extended` query suite

### 2. Secret Scanning (`.github/workflows/secret-scanning.yml`)

**Purpose**: Prevent accidental commit of secrets, credentials, and sensitive data.

**Triggers**:
- Push to `main` or `develop` branches
- Pull requests targeting `main` or `develop`

**What it detects**:
- API keys and tokens
- AWS access keys
- Private SSH keys
- Database connection strings
- OAuth tokens
- Password patterns
- Certificate files
- JWT tokens

**Technology**: Gitleaks v8.18.1

**Response**: Fails the build immediately if secrets are detected.

### 3. Security Validation (`.github/workflows/ci.yml` - security-checks job)

**Purpose**: Fast security checks integrated into main CI pipeline.

**What it checks**:
- Hardcoded secret patterns (API keys, passwords, AWS keys)
- Unsafe C functions (strcpy, sprintf, gets, scanf)
- Potential command injection (system() calls)
- Accidentally committed binary files
- Dangerous patterns in code

**Response**: Fails build on critical issues, warns on suspicious patterns.

### 4. Branch Protection Enforcement (`.github/workflows/branch-protection.yml`)

**Purpose**: Validate and enforce branch protection policies for PRs to main.

**What it validates**:
- PR targets correct branch
- All required status checks are configured
- Commit message quality
- No force-push indicators
- Breaking changes are documented

**Features**:
- Posts requirements comment on PRs to main
- Validates commit history
- Checks for breaking API changes
- Suggests conventional commit format

## CI/CD Pipeline Security

### Build Security

The CI pipeline includes:

1. **Dependency Management**
   - Dependabot for automated dependency updates
   - Weekly scans for vulnerable dependencies
   - Automated PR creation for security updates

2. **Build Integrity**
   - Reproducible builds with locked dependencies
   - Compiler warnings as errors
   - Static analysis with clang-tidy
   - Code formatting enforcement (clang-format)

3. **Test Security**
   - Unit tests must pass
   - No known test failures allowed
   - Memory sanitizers in debug builds

### Required Status Checks

Before merging to `main`, the following checks **must pass**:

| Check | Workflow | Purpose |
|-------|----------|---------|
| Build and Test (Linux) | `ci.yml` | Code builds and tests pass |
| Code Quality | `ci.yml` | Formatting and style checks |
| Security Validation | `ci.yml` | Fast security pattern checks |
| Secret Scanning | `secret-scanning.yml` | No secrets in code |
| CodeQL Analysis | `codeql.yml` | No security vulnerabilities |
| Branch Protection | `branch-protection.yml` | Policy enforcement |

## Security Best Practices Enforced

### Code Security

1. **Memory Safety**
   - Use C++20 RAII patterns
   - Smart pointers over raw pointers
   - No unsafe C functions
   - Bounds checking on arrays

2. **Input Validation**
   - Validate all external inputs
   - Sanitize file paths
   - Check buffer sizes
   - Validate plugin data

3. **Thread Safety**
   - Lock-free audio processing
   - Proper synchronization primitives
   - No data races in concurrent code

4. **Error Handling**
   - Exceptions for error handling
   - No ignored return values
   - Proper resource cleanup

### Development Security

1. **No Secrets in Code**
   - Use environment variables for sensitive data
   - Configuration files not in repository
   - Secrets managed through secure channels

2. **Dependency Security**
   - Pin dependency versions in submodules
   - Review third-party code
   - Update dependencies regularly
   - Audit for known vulnerabilities

3. **Code Review**
   - All PRs reviewed by maintainer
   - Security implications discussed
   - Test coverage verified
   - Documentation updated

## Incident Response

### If Secrets are Detected

1. **Immediate Actions**:
   - Build fails automatically
   - PR cannot be merged
   - Notification to PR author

2. **Remediation Steps**:
   - Rotate/revoke compromised credentials immediately
   - Remove secrets from code
   - Use environment variables or secure vaults
   - Review git history - may need to rewrite if secret was committed

3. **Prevention**:
   - Add secret to `.gitleaksignore` if false positive
   - Update documentation on proper secret handling
   - Consider pre-commit hooks for local scanning

### If Vulnerabilities are Found

1. **CodeQL Alerts**:
   - Alert created in Security tab
   - Notification to maintainers
   - Must be fixed before merge

2. **Dependency Vulnerabilities**:
   - Dependabot creates PR automatically
   - Review and merge security updates promptly
   - Document breaking changes if any

3. **Manual Reports**:
   - Follow SECURITY.md policy
   - Private security advisory created
   - Fix developed and tested
   - Security update released

## Attack Vectors Mitigated

### 1. Malicious Code Injection

**Protection**:
- CodeQL scans for injection patterns
- All code reviewed before merge
- No direct commits to main
- CI must pass before merge

### 2. Supply Chain Attacks

**Protection**:
- Submodules pinned to specific commits
- Dependabot monitors dependencies
- No automatic dependency updates without review
- Build from source in CI

### 3. Secret Leakage

**Protection**:
- Automated secret scanning
- Build fails on secret detection
- No credentials in repository
- Environment variables for sensitive data

### 4. Backdoor Introduction

**Protection**:
- Mandatory code review
- All changes visible in PR
- CI validates all code changes
- No force-push allowed on main

### 5. Vulnerable Dependencies

**Protection**:
- Dependabot security alerts
- Automated update PRs
- Weekly scheduled scans
- Version pinning

### 6. Binary Injection

**Protection**:
- Verify no binaries committed
- Build from source only
- No pre-compiled artifacts in repo
- `.gitignore` excludes build artifacts

## Monitoring and Alerts

### Security Monitoring

1. **GitHub Security Tab**
   - CodeQL alerts
   - Secret scanning alerts
   - Dependabot alerts
   - Security advisories

2. **Workflow Notifications**
   - Failed security checks notify team
   - Weekly scan summaries
   - Critical alerts immediate

3. **Audit Trail**
   - All PRs logged
   - Reviews tracked
   - Status check history
   - Commit signatures

## Configuration Checklist

To fully enable branch protection, ensure:

### Repository Settings

- [ ] **Branches → Branch protection rules for `main`**:
  - [ ] Require a pull request before merging
  - [ ] Require approvals: 1
  - [ ] Dismiss stale pull request approvals when new commits are pushed
  - [ ] Require review from Code Owners
  - [ ] Require status checks to pass before merging
  - [ ] Require branches to be up to date before merging
  - [ ] Require conversation resolution before merging
  - [ ] Do not allow bypassing the above settings

- [ ] **Required Status Checks** (select all):
  - [ ] `build-and-test-linux`
  - [ ] `code-quality`
  - [ ] `security-checks`
  - [ ] `scan-secrets`
  - [ ] `analyze / Analyze C++ Code`
  - [ ] `enforce-branch-protection`

- [ ] **Security → Code security and analysis**:
  - [ ] Dependency graph: Enabled
  - [ ] Dependabot alerts: Enabled
  - [ ] Dependabot security updates: Enabled
  - [ ] CodeQL analysis: Enabled
  - [ ] Secret scanning: Enabled
  - [ ] Push protection: Enabled (for secrets)

- [ ] **Code security → Secret scanning**:
  - [ ] Push protection: Enabled
  - [ ] Alerts: Enabled

## Continuous Improvement

This security architecture is continuously improved through:

1. **Regular Reviews**
   - Quarterly security policy reviews
   - Update scanning tools versions
   - Review new vulnerability types
   - Improve detection patterns

2. **Community Feedback**
   - Security reports via SECURITY.md
   - Researcher contributions
   - User feedback on security

3. **Industry Standards**
   - Follow OWASP guidelines
   - Adopt CWE best practices
   - Monitor CVE databases
   - Update to latest security tools

## Resources

- [SECURITY.md](../SECURITY.md) - Vulnerability reporting
- [GitHub Security Features](https://docs.github.com/en/code-security)
- [CodeQL Documentation](https://codeql.github.com/docs/)
- [Gitleaks Documentation](https://github.com/gitleaks/gitleaks)
- [OWASP Guidelines](https://owasp.org/)

## Contact

For security questions or concerns:
- Review [SECURITY.md](../SECURITY.md)
- Open a private security advisory
- Contact repository maintainers

---

**Last Updated**: 2026-01-30  
**Version**: 1.0  
**Status**: Active
