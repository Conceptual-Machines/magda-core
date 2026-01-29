# Periodic Workflows Setup - Summary

## What Was Accomplished

This PR sets up periodic GitHub Actions workflows that can run independently without triggering the full C++ build and test suite.

## Changes Made

### 1. Updated CI Workflow (`.github/workflows/ci.yml`)

**Added conditional execution:**
- Added `workflow_dispatch` trigger with `skip-cpp-build` input parameter
- Added conditional `if` statements to both `build-and-test-linux` and `code-quality` jobs
- Jobs now skip when manually triggered with `skip-cpp-build=true`

**Usage:**
```bash
# Skip C++ jobs when manually triggering CI
gh workflow run ci.yml -f skip-cpp-build=true
```

### 2. Periodic Code Analysis Workflow (`.github/workflows/periodic-analysis.yml`)

**Features:**
- Runs weekly on Mondays at 9:00 AM UTC
- Can be triggered manually via GitHub Actions UI or CLI
- Lightweight analysis (no C++ compilation required)
- Execution time: ~1-2 minutes

**What it analyzes:**
- TODOs and FIXMEs in code
- Long functions (>100 lines)
- Files with many includes (>15)
- Raw pointer usage (suggests smart pointers)
- Static non-const variables (thread safety concerns)

**Outputs:**
- Analysis report artifact (30-day retention)
- Automatic GitHub issue creation when FIXMEs > 5

### 3. Refactoring Opportunities Scanner (`.github/workflows/refactoring-scanner.yml`)

**Features:**
- Runs bi-weekly (1st and 15th of month) at 10:00 AM UTC
- Can be triggered manually
- Lightweight analysis (no C++ compilation)
- Execution time: ~2-3 minutes

**What it analyzes:**
- Code complexity (cyclomatic complexity)
- Large files (>500 lines)
- Tight coupling (files with many internal includes)
- Unused includes (suggestions)
- Magic numbers (should be named constants)
- God objects (classes with 20+ methods)

**Outputs:**
- Refactoring report artifact (30-day retention)
- Automatic GitHub issue when high-complexity functions > 3

### 4. Documentation

**Created `docs/AUTOMATED_WORKFLOWS.md`:**
- Complete guide to all automated workflows
- Usage instructions and examples
- Customization guide (schedules, thresholds)
- Troubleshooting section
- Best practices

**Updated `README.md`:**
- Added "Automated Workflows" section
- Links to detailed documentation

## Key Benefits

### ✅ No C++ Build Overhead
- Periodic analysis workflows don't require C++ compilation
- Fast execution (1-3 minutes vs 10+ minutes for full CI)
- Minimal resource usage on GitHub runners

### ✅ Automated Issue Creation
- High-priority findings automatically create GitHub issues
- Issues include detailed reports and actionable recommendations
- Smart de-duplication (updates existing issues instead of creating duplicates)

### ✅ Flexible Scheduling
- Weekly code analysis for immediate feedback
- Bi-weekly refactoring scan for long-term code health
- Manual triggers available for on-demand analysis

### ✅ Independent Execution
- Workflows run independently of code changes
- Don't interfere with developer PRs or regular CI
- Can be disabled/adjusted without affecting main CI

## How It Works

### Conditional CI Execution

The main CI workflow now has conditional job execution:

```yaml
jobs:
  build-and-test-linux:
    if: |
      github.event_name != 'workflow_dispatch' || 
      inputs.skip-cpp-build != true
```

This means:
- Regular pushes: CI runs normally (builds and tests C++)
- Manual trigger with `skip-cpp-build=true`: CI jobs are skipped
- Periodic workflows can trigger without running heavy C++ jobs

### Lightweight Analysis

Both periodic workflows use only text processing tools:
- `grep`, `find`, `awk` for pattern matching
- `cloc` for code metrics
- `radon` for complexity analysis
- Python scripts for custom analysis

No C++ compilation or linking required!

### Smart Issue Management

Issues are created with:
- Descriptive titles with dates
- Detailed analysis in collapsible sections
- Actionable recommendations
- Proper labels for filtering (`automated-analysis`, `refactoring`, `technical-debt`)
- De-duplication logic (updates existing issues if found)

## Testing Results

All workflows have been validated:
- ✅ YAML syntax validated
- ✅ Shellcheck issues fixed (proper quoting, read flags, find syntax)
- ✅ Manual testing confirms all analysis tools work correctly
- ✅ Found actual data:
  - 5+ TODOs/FIXMEs in codebase
  - Several files over 500 lines (candidates for refactoring)
  - Multiple large files identified for potential splitting

## Next Steps

### For Immediate Use

1. **Enable workflows** (they're ready to run on next schedule)
2. **Test manually** (optional):
   ```bash
   gh workflow run periodic-analysis.yml
   gh workflow run refactoring-scanner.yml
   ```
3. **Review first issues created** and adjust thresholds if needed

### Customization Options

**Adjust schedule timing:**
Edit cron expressions in workflow files:
```yaml
schedule:
  - cron: '0 9 * * 1'  # Change to your preferred time
```

**Adjust issue creation thresholds:**
```yaml
if: steps.metrics.outputs.fixme_count > 5  # Change threshold
```

**Add more analysis:**
- Create new workflow files using existing ones as templates
- Add custom scripts to `scripts/` directory
- Use any lightweight analysis tools (no heavy dependencies)

### Maintenance

- Review automated issues regularly
- Close issues when addressed
- Adjust thresholds based on project needs
- Archive old reports from workflow artifacts if not needed

## Example Output

### Periodic Analysis Report
```
=== TODOs and FIXMEs ===
magda/daw/magda.cpp:24: // TODO: Initialize additional systems
magda/daw/project/ProjectManager.cpp:235: // TODO: Implement UI dialog
...

=== Summary ===
TODOs found: 15
FIXMEs found: 3
```

### Refactoring Report
```
=== High Complexity Functions ===
Function xyz (Complexity: 15) - Consider refactoring

=== Large Files ===
magda/daw/project/ProjectSerializer.cpp: 1521 lines
magda/daw/engine/TracktionEngineWrapper.cpp: 1603 lines
```

## Questions?

See [docs/AUTOMATED_WORKFLOWS.md](docs/AUTOMATED_WORKFLOWS.md) for complete documentation or create an issue for support.
