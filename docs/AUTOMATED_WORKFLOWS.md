# Automated Workflows

This document describes the automated workflows configured for the MAGDA project.

## Overview

The project uses GitHub Actions for continuous integration and periodic automated analysis. The workflows are designed to:

1. **Validate code changes** on every push (CI workflow)
2. **Analyze codebase** periodically for potential issues (analysis workflows)
3. **Skip heavy build/test** jobs when not needed (conditional execution)

## Workflows

### 1. CI Workflow (`.github/workflows/ci.yml`)

**Triggers**: 
- Automatic: On every push to any branch
- Manual: Can be triggered via workflow_dispatch with options

**Jobs**:
- `build-and-test-linux`: Builds C++ code and runs test suite
- `code-quality`: Checks code formatting with clang-format

**Conditional Execution**:
The workflow supports skipping C++ build and test jobs when triggered manually:

```bash
# Trigger workflow manually and skip C++ jobs
gh workflow run ci.yml -f skip-cpp-build=true
```

This is useful when:
- Testing workflow changes
- Making documentation-only changes
- Running workflows that don't require C++ validation

### 2. Periodic Code Analysis (`.github/workflows/periodic-analysis.yml`)

**Triggers**:
- Scheduled: Weekly on Mondays at 9:00 AM UTC
- Manual: Can be triggered on-demand

**What it does**:
- Collects codebase metrics (lines of code, file counts)
- Finds TODO and FIXME comments
- Detects potential code smells:
  - Long functions (>100 lines)
  - Files with many includes (>15)
  - Raw pointer usage (consider smart pointers)
  - Static non-const variables (thread safety concerns)

**Outputs**:
- Analysis report as workflow artifact
- GitHub issue (if FIXMEs > 5) with findings and recommendations

**Running manually**:
```bash
gh workflow run periodic-analysis.yml
```

### 3. Refactoring Opportunities Scanner (`.github/workflows/refactoring-scanner.yml`)

**Triggers**:
- Scheduled: Bi-weekly (1st and 15th) at 10:00 AM UTC
- Manual: Can be triggered on-demand

**What it does**:
- Analyzes code complexity (cyclomatic complexity)
- Finds duplicate code candidates
- Identifies large files (>500 lines)
- Detects tight coupling (files with many internal includes)
- Suggests unused includes cleanup
- Finds magic numbers (should be named constants)
- Identifies god objects (classes with 20+ methods)

**Outputs**:
- Refactoring report as workflow artifact
- GitHub issue (if high-complexity functions > 3) with prioritized recommendations

**Running manually**:
```bash
gh workflow run refactoring-scanner.yml
```

## Benefits

### 1. No C++ Build Overhead for Analysis Workflows

The periodic analysis workflows are designed to be lightweight:
- ✅ No C++ compilation required
- ✅ No test suite execution
- ✅ Fast execution (typically < 2 minutes)
- ✅ Minimal resource usage

This allows frequent automated analysis without the overhead of full CI builds.

### 2. Automated Issue Creation

High-priority findings automatically create GitHub issues with:
- Detailed analysis report
- Actionable recommendations
- Appropriate labels for tracking
- Smart de-duplication (updates existing issues if found)

### 3. Trend Tracking

All reports are saved as workflow artifacts with 30-day retention, allowing you to:
- Track code quality metrics over time
- Measure improvement from refactoring efforts
- Identify growing technical debt

## Customization

### Adjusting Schedules

Edit the `cron` expressions in workflow files to change timing:

```yaml
schedule:
  # Format: minute hour day-of-month month day-of-week
  - cron: '0 9 * * 1'  # Weekly on Mondays at 9 AM UTC
```

Common examples:
- Daily: `0 9 * * *`
- Weekly: `0 9 * * 1` (Monday)
- Monthly: `0 9 1 * *` (1st of month)

### Adjusting Thresholds

Thresholds for issue creation can be modified in the workflow files:

**In `periodic-analysis.yml`**:
```yaml
if: steps.metrics.outputs.fixme_count > 5  # Create issue if more than 5 FIXMEs
```

**In `refactoring-scanner.yml`**:
```yaml
if: steps.complexity.outputs.complex_count > 3  # Create issue if >3 complex functions
```

### Adding New Analysis

To add new periodic analysis:

1. Create a new workflow file in `.github/workflows/`
2. Use existing workflows as templates
3. Ensure `permissions` include what you need:
   ```yaml
   permissions:
     issues: write      # To create issues
     contents: read     # To read code
     pull-requests: write  # To create PRs (if needed)
   ```
4. Use lightweight tools (grep, awk, Python scripts)
5. Avoid heavy C++ builds by not triggering CI

## Best Practices

1. **Keep analysis lightweight**: Use text processing tools instead of full compilation
2. **Set appropriate thresholds**: Balance between noise and useful alerts
3. **Review false positives**: Automated analysis isn't perfect—use human judgment
4. **Close resolved issues**: Mark automated issues as complete when addressed
5. **Adjust frequency**: Start with less frequent runs, increase if helpful

## Disabling Workflows

To temporarily disable a workflow:

1. Go to GitHub → Actions → Select workflow
2. Click "..." menu → "Disable workflow"

Or edit the workflow file and add:
```yaml
on:
  # All triggers commented out or removed
  workflow_dispatch:  # Keep manual trigger only
```

## Monitoring

View workflow runs:
- GitHub web UI: Actions tab
- CLI: `gh run list --workflow=periodic-analysis.yml`

View created issues:
- Filter by labels: `label:automated-analysis` or `label:refactoring`
- Use GitHub Projects to track automated findings

## Troubleshooting

**Issue: Workflow not running on schedule**
- Check that workflows are enabled for the repository
- Verify cron syntax with https://crontab.guru/
- Note: Scheduled workflows may be delayed up to 15 minutes

**Issue: Too many automated issues**
- Increase thresholds in workflow conditions
- Adjust schedule to run less frequently
- Add more filters to reduce false positives

**Issue: Analysis tools failing**
- Check tool installation steps in workflows
- Verify tool compatibility with Ubuntu runner
- Review workflow logs for specific error messages

## Future Enhancements

Potential additions:
- Dependency vulnerability scanning
- Performance regression detection
- API documentation coverage checking
- Commit message linting
- Pull request quality checks
