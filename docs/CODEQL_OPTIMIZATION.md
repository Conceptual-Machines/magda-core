# CodeQL Performance Optimization

## Problem

The CodeQL Security Scan workflow was taking **~67 minutes** to complete, which significantly delayed PR feedback cycles. The breakdown was:
- Build: 44 minutes
- CodeQL Analysis: 19 minutes
- Other steps: 4 minutes

## Optimizations Implemented

### 1. Removed CodeQL from PR Checks (Primary Optimization)

**Change**: CodeQL now only runs on:
- Push to `main` and `develop` branches
- Scheduled weekly scans (Mondays at 2:00 AM UTC)
- Manual workflow dispatch

**Rationale**: 
- Security scans are more appropriate for integrated code rather than every PR iteration
- PRs are already protected by other fast security checks (secret scanning, basic security patterns)
- CodeQL results are most valuable when analyzing the main codebase
- This completely eliminates the 1-hour delay from PR workflows

**Trade-off**: Security issues are detected slightly later (after merge to main), but:
- The weekly schedule ensures regular scanning
- Push to main triggers immediate scan after merge
- PRs still have secret scanning and basic security checks

### 2. Switched to Default Query Set

**Change**: Removed `queries: security-extended` from CodeQL initialization

**Rationale**:
- `security-extended` adds ~30% more analysis time
- Default queries cover the most critical security issues
- Extended queries often produce more false positives

**Impact**: Reduced analysis time by ~6 minutes while maintaining core security coverage

### 3. Added Build Caching with ccache

**Change**: Added ccache support to CodeQL build step

**Rationale**:
- C++ compilation is the most time-consuming part
- ccache can reduce build time by 50-70% on subsequent runs
- Cache is shared across CodeQL runs on the same branch

**Impact**: Expected 20-30 minute reduction in build time on cache hits

### 4. Upgraded to CodeQL Action v4

**Change**: Updated from `github/codeql-action@v3` to `@v4`

**Rationale**:
- v3 is deprecated (December 2026)
- v4 includes performance improvements
- Prevents future breaking changes

## Alternative Approaches (Not Implemented)

### Use Larger GitHub Runners

**Option**: Use `runs-on: ubuntu-latest-8-cores` or self-hosted runners

**Pros**:
- 2-3x faster build times
- Faster analysis with more parallel processing

**Cons**:
- Costs ~4x more for GitHub-hosted runners with 8 cores
- Requires billing setup for private repositories
- Still doesn't address the fundamental issue of blocking PRs

**Recommendation**: Consider if budget allows and if you want CodeQL on every PR

### Switch to Alternative Static Analysis Tools

**Option**: Replace CodeQL with faster alternatives

#### clang-tidy
- **Speed**: Much faster (5-10 minutes for full analysis)
- **Coverage**: Good C++ specific checks, but less comprehensive security analysis
- **Status**: Already used for code quality checks in CI
- **Recommendation**: Already in use; complements rather than replaces CodeQL

#### Semgrep
- **Speed**: Very fast (2-5 minutes)
- **Coverage**: Good security pattern matching, less deep than CodeQL
- **Setup**: Easy to integrate, free tier available
- **Recommendation**: Good complementary tool for quick security checks in PRs

#### SonarQube/SonarCloud
- **Speed**: Moderate (10-20 minutes)
- **Coverage**: Comprehensive quality and security analysis
- **Setup**: Requires account setup and configuration
- **Recommendation**: Good alternative if you want integrated quality/security analysis

#### Coverity (Synopsis)
- **Speed**: Slow (30-60 minutes)
- **Coverage**: Excellent for C++ security and safety
- **Setup**: Commercial tool, requires licensing
- **Recommendation**: Only if organization already has license

### Incremental Analysis

**Option**: Analyze only changed files

**Status**: Not directly supported by GitHub CodeQL Action for C++ (requires full program analysis)

**Alternative**: Use lightweight tools (clang-tidy, Semgrep) on changed files in PR, full CodeQL on main

## Current Security Coverage

### PR-Level Checks (Fast)
- ✅ Secret Scanning (< 1 minute) - Gitleaks
- ✅ Security Patterns (< 1 minute) - Custom grep patterns
- ✅ Code Formatting (< 1 minute) - clang-format
- ✅ Build & Tests (~12 minutes) - Compilation and unit tests

### Post-Merge/Scheduled (Comprehensive)
- ✅ CodeQL Security Scan (~45-60 minutes with caching) - Weekly + on main
- ✅ Periodic Code Analysis - Weekly

## Recommendations

### Immediate (Implemented)
1. ✅ Remove CodeQL from PR checks
2. ✅ Use default query set
3. ✅ Add ccache support
4. ✅ Upgrade to CodeQL v4

### Short-term (Optional)
1. **Add Semgrep for PR security checks** (~2-5 minutes)
   - Fast, good security coverage
   - Catches common vulnerabilities in PRs
   - Complements post-merge CodeQL

2. **Monitor CodeQL results** for 2-4 weeks
   - Verify issues are being caught post-merge
   - Check if weekly schedule is sufficient
   - Adjust schedule if needed (e.g., twice weekly)

### Long-term (If Needed)
1. **Consider self-hosted runners** if CodeQL on PRs is required
   - Dedicated build server with powerful hardware
   - Pre-warmed caches
   - Can reduce CodeQL time to 15-20 minutes

2. **Evaluate SonarCloud** if you want comprehensive analysis with better performance
   - Good balance of speed and coverage
   - Integrated quality and security
   - Better PR feedback with incremental analysis

## Conclusion

The implemented changes reduce the PR feedback loop from **1+ hour to ~12 minutes** by moving CodeQL to post-merge while maintaining comprehensive security coverage. The weekly scheduled scans and post-merge checks ensure the codebase is regularly analyzed without blocking development velocity.

For most projects, this strikes the right balance between security and developer productivity. If you need even faster feedback, consider adding Semgrep for quick security checks in PRs.
