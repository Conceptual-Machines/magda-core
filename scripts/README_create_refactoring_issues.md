# GitHub Issue Creation Script

## Overview

This script creates GitHub issues for refactoring opportunities detected by the refactoring scanner workflow. It includes robust rate limiting and error handling to avoid hitting GitHub's API limits.

## Features

- **Rate Limiting**: Configurable delay between API requests (default: 2.0s)
- **Retry Logic**: Exponential backoff for transient errors (default: 3 retries)
- **Error Handling**: Gracefully handles failures and continues processing
- **Duplicate Detection**: Updates existing issues instead of creating duplicates
- **Statistics Tracking**: Reports success/failure/skip counts
- **Dry Run Mode**: Test without actually creating issues

## Usage

### Basic Usage

```bash
python3 scripts/create_refactoring_issues.py \
  --summary ./refactoring-results-summary.txt \
  --results-dir ./results
```

### With Custom Parameters

```bash
python3 scripts/create_refactoring_issues.py \
  --summary ./refactoring-results-summary.txt \
  --results-dir ./results \
  --delay 3.0 \
  --max-retries 5
```

### Dry Run (Testing)

```bash
python3 scripts/create_refactoring_issues.py \
  --summary ./refactoring-results-summary.txt \
  --results-dir ./results \
  --dry-run
```

## Arguments

- `--summary`: Path to the refactoring summary file (default: `./refactoring-results-summary.txt`)
- `--results-dir`: Directory containing detailed report files (default: `./results`)
- `--delay`: Delay in seconds between API requests (default: `2.0`)
- `--max-retries`: Maximum number of retries for failed requests (default: `3`)
- `--dry-run`: Test mode - parse files but don't create issues

## Environment Variables

The script requires the following environment variables:

- `GITHUB_TOKEN`: GitHub personal access token or Actions token
- `GITHUB_REPOSITORY`: Repository in `owner/repo` format

These are automatically set in GitHub Actions workflows.

## Rate Limiting Strategy

1. **Base Delay**: Wait 2-3 seconds between each API request
2. **Exponential Backoff**: On rate limit errors, retry with delays:
   - 1st retry: 2 seconds
   - 2nd retry: 4 seconds
   - 3rd retry: 8 seconds
   - 4th retry: 16 seconds
   - 5th retry: 32 seconds
3. **Persistent Rate Limit**: If rate limit persists after all retries, wait 30 seconds before continuing

## Error Handling

- **Missing Report Files**: Skips the file and logs a warning
- **API Errors**: Retries with backoff, logs error if all retries fail
- **Rate Limits**: Implements exponential backoff and longer waits
- **Continues on Failure**: One file's failure doesn't stop processing others

## Output

The script provides detailed progress logs:

```
📖 Reading summary from: ./refactoring-results-summary.txt
Found 15 files with refactoring opportunities

Processing file 1/15...
📄 Processing: magda/daw/ui/MainComponent.cpp
✅ Created new issue #123 for magda/daw/ui/MainComponent.cpp
⏳ Waiting 2.5s before next request...

Processing file 2/15...
📄 Processing: magda/agents/AudioAgent.hpp
✅ Updated existing issue #124 for magda/agents/AudioAgent.hpp
⏳ Waiting 2.5s before next request...

...

============================================================
📊 SUMMARY
============================================================
✅ Created: 10
🔄 Updated: 3
⚠️  Skipped: 1
❌ Failed: 1
📝 Total: 15
```

## Exit Codes

- `0`: Success (all files processed, or only skips)
- `1`: Failure (one or more files failed to process)

## Testing Locally

1. Create test summary and report files:

```bash
mkdir -p /tmp/test-results

cat > /tmp/test-summary.txt << 'EOF'
⚠️  test/file1.cpp: High complexity detected
⚠️  test/file2.hpp: Large file (600 lines)
EOF

cat > /tmp/test-results/refactoring-report-test-file1-cpp.txt << 'EOF'
=== Analysis Report for: test/file1.cpp ===
Test report content
EOF
```

2. Run in dry-run mode:

```bash
export GITHUB_TOKEN=your_token
export GITHUB_REPOSITORY=owner/repo

python3 scripts/create_refactoring_issues.py \
  --summary /tmp/test-summary.txt \
  --results-dir /tmp/test-results \
  --dry-run
```

## Dependencies

- Python 3.7+
- `requests` library

Install dependencies:

```bash
pip install requests
```

## Integration with GitHub Actions

The script is called from the workflow:

```yaml
- name: Post Results on GitHub Issues
  if: steps.analyze_files.outputs.complex_count > 3
  env:
    GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
  run: |
    python3 scripts/create_refactoring_issues.py \
      --summary ./refactoring-results-summary.txt \
      --results-dir ./results \
      --delay 2.5 \
      --max-retries 5
```

## Troubleshooting

### "GITHUB_TOKEN environment variable not set"

Make sure `GITHUB_TOKEN` is set. In GitHub Actions, this is automatically available as `${{ secrets.GITHUB_TOKEN }}`.

### "GITHUB_REPOSITORY environment variable not set"

Make sure `GITHUB_REPOSITORY` is set. In GitHub Actions, this is automatically available.

### Rate limit errors persist

- Increase the `--delay` parameter (e.g., `--delay 5.0`)
- Increase `--max-retries` (e.g., `--max-retries 10`)
- Run the workflow less frequently
- Process fewer files by adjusting the complexity threshold in the workflow

### Script exits with code 1

Check the error summary at the end of the output. The script will list all failed files and their error messages.
