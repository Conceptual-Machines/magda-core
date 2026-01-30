#!/usr/bin/env python3
"""
Create GitHub issues for refactoring opportunities.

This script reads the refactoring analysis summary and creates individual
GitHub issues for each file with refactoring opportunities. It includes:
- Rate limiting and throttling to avoid hitting GitHub API limits
- Retry logic with exponential backoff for transient errors
- Graceful error handling to continue processing even if some issues fail
- Duplicate detection to update existing issues instead of creating new ones
"""

import os
import sys
import time
import re
import argparse
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass
import requests


@dataclass
class IssueStats:
    """Track statistics about issue creation."""
    total: int = 0
    created: int = 0
    updated: int = 0
    skipped: int = 0
    failed: int = 0
    errors: List[Tuple[str, str]] = None
    
    def __post_init__(self):
        if self.errors is None:
            self.errors = []


class GitHubAPIError(Exception):
    """Custom exception for GitHub API errors."""
    pass


class RateLimitError(GitHubAPIError):
    """Exception for rate limit errors."""
    pass


class GitHubIssueCreator:
    """Handle GitHub issue creation with rate limiting and retries."""
    
    def __init__(self, token: str, owner: str, repo: str, 
                 delay_seconds: float = 2.0, max_retries: int = 3):
        """
        Initialize the issue creator.
        
        Args:
            token: GitHub personal access token
            owner: Repository owner
            repo: Repository name
            delay_seconds: Delay between API requests (default: 2.0)
            max_retries: Maximum number of retries for failed requests (default: 3)
        """
        self.token = token
        self.owner = owner
        self.repo = repo
        self.delay_seconds = delay_seconds
        self.max_retries = max_retries
        self.base_url = "https://api.github.com"
        self.session = requests.Session()
        self.session.headers.update({
            "Authorization": f"token {token}",
            "Accept": "application/vnd.github.v3+json",
            "User-Agent": f"{owner}/{repo} refactoring-scanner"
        })
        self.stats = IssueStats()
    
    def _sleep(self, seconds: Optional[float] = None):
        """Sleep for the specified duration."""
        delay = seconds if seconds is not None else self.delay_seconds
        if delay > 0:
            print(f"⏳ Waiting {delay:.1f}s before next request...")
            time.sleep(delay)
    
    def _retry_with_backoff(self, func, *args, **kwargs):
        """
        Retry a function with exponential backoff.
        
        Args:
            func: Function to retry
            *args: Positional arguments for the function
            **kwargs: Keyword arguments for the function
            
        Returns:
            The return value of the function
            
        Raises:
            GitHubAPIError: If all retries are exhausted
        """
        for attempt in range(1, self.max_retries + 1):
            try:
                return func(*args, **kwargs)
            except RateLimitError as e:
                if attempt < self.max_retries:
                    # Exponential backoff: 2s, 4s, 8s, etc.
                    delay = self.delay_seconds * (2 ** attempt)
                    print(f"⏳ Rate limit hit, retrying in {delay:.1f}s "
                          f"(attempt {attempt}/{self.max_retries})...")
                    time.sleep(delay)
                else:
                    raise GitHubAPIError(f"Rate limit persists after {self.max_retries} attempts") from e
            except requests.exceptions.RequestException as e:
                if attempt < self.max_retries:
                    delay = self.delay_seconds * (2 ** (attempt - 1))
                    print(f"⚠️  Request failed, retrying in {delay:.1f}s "
                          f"(attempt {attempt}/{self.max_retries}): {str(e)}")
                    time.sleep(delay)
                else:
                    raise GitHubAPIError(f"Request failed after {self.max_retries} attempts") from e
    
    def _make_request(self, method: str, url: str, **kwargs) -> requests.Response:
        """
        Make an API request with error handling.
        
        Args:
            method: HTTP method (GET, POST, etc.)
            url: URL to request
            **kwargs: Additional arguments for requests
            
        Returns:
            Response object
            
        Raises:
            RateLimitError: If rate limit is hit
            GitHubAPIError: For other API errors
        """
        try:
            response = self.session.request(method, url, **kwargs)
            
            # Check for rate limit errors
            if response.status_code == 429 or response.status_code == 403:
                error_msg = response.json().get('message', '') if response.headers.get('content-type', '').startswith('application/json') else response.text
                if 'rate limit' in error_msg.lower() or 'secondary rate limit' in error_msg.lower():
                    raise RateLimitError(f"Rate limit exceeded: {error_msg}")
            
            # Raise for other HTTP errors
            response.raise_for_status()
            return response
            
        except requests.exceptions.HTTPError as e:
            # Check if it's a rate limit error in the exception
            if e.response.status_code in [403, 429]:
                raise RateLimitError(f"Rate limit error: {str(e)}") from e
            raise GitHubAPIError(f"HTTP error: {str(e)}") from e
        except requests.exceptions.RequestException as e:
            raise GitHubAPIError(f"Request error: {str(e)}") from e
    
    def search_existing_issue(self, file_path: str) -> Optional[int]:
        """
        Search for an existing issue for the given file.
        
        Args:
            file_path: Path to the file
            
        Returns:
            Issue number if found, None otherwise
        """
        # Search for issues with the file path in the title
        query = f'repo:{self.owner}/{self.repo} is:issue is:open label:refactoring label:automated in:title "{file_path}"'
        url = f"{self.base_url}/search/issues"
        
        try:
            response = self._retry_with_backoff(
                self._make_request,
                "GET",
                url,
                params={"q": query}
            )
            
            data = response.json()
            items = data.get('items', [])
            
            # Filter for exact title match
            issue_title = f"[Refactoring] {file_path}"
            for item in items:
                if item.get('title') == issue_title:
                    return item.get('number')
            
            return None
            
        except Exception as e:
            print(f"⚠️  Search failed for {file_path}: {str(e)}")
            return None
    
    def create_issue(self, title: str, body: str, labels: List[str]) -> int:
        """
        Create a new GitHub issue.
        
        Args:
            title: Issue title
            body: Issue body
            labels: List of labels
            
        Returns:
            Issue number
            
        Raises:
            GitHubAPIError: If issue creation fails
        """
        url = f"{self.base_url}/repos/{self.owner}/{self.repo}/issues"
        data = {
            "title": title,
            "body": body,
            "labels": labels
        }
        
        response = self._retry_with_backoff(
            self._make_request,
            "POST",
            url,
            json=data
        )
        
        return response.json().get('number')
    
    def add_comment(self, issue_number: int, body: str):
        """
        Add a comment to an existing issue.
        
        Args:
            issue_number: Issue number
            body: Comment body
            
        Raises:
            GitHubAPIError: If comment creation fails
        """
        url = f"{self.base_url}/repos/{self.owner}/{self.repo}/issues/{issue_number}/comments"
        data = {"body": body}
        
        self._retry_with_backoff(
            self._make_request,
            "POST",
            url,
            json=data
        )
    
    def process_file(self, file_path: str, issue_description: str, 
                     report_content: str) -> bool:
        """
        Process a single file and create or update its issue.
        
        Args:
            file_path: Path to the file
            issue_description: Description of the issue
            report_content: Full report content
            
        Returns:
            True if successful, False otherwise
        """
        print(f"\n📄 Processing: {file_path}")
        
        try:
            # Create issue title and body
            issue_title = f"[Refactoring] {file_path}"
            issue_body = f"""## Refactoring Opportunity: {file_path}

This issue was automatically created by the refactoring scanner workflow.

### Issue Detected
{issue_description}

### Analysis Date
{time.strftime('%Y-%m-%d')}

### Detailed Analysis

```
{report_content}
```

### Recommended Actions

Based on the issue type:
- **High Complexity**: Break down complex functions (cyclomatic complexity > 10) into smaller, more focused functions
- **Large File**: Consider splitting file over 500 lines into smaller, focused modules
- **High Coupling**: Review and reduce files with many internal dependencies (>10 includes)
- **Magic Numbers**: Replace literal numbers with named constants
- **God Object**: Consider splitting classes with 20+ methods into smaller, more focused classes

### Next Steps

1. Review the detailed analysis above
2. Plan refactoring approach
3. Create a branch for the refactoring work
4. Submit a PR with the improvements
5. Close this issue when complete

**Note**: This is an automated issue. Feel free to add comments, assign it, or close it if not applicable.
"""
            
            # Check if issue already exists
            existing_issue = self.search_existing_issue(file_path)
            
            if existing_issue:
                # Update existing issue with a comment
                comment_body = f"""## Updated Analysis - {time.strftime('%Y-%m-%d')}

{issue_description}

<details>
<summary>Click to expand updated report</summary>

```
{report_content}
```

</details>"""
                
                self.add_comment(existing_issue, comment_body)
                print(f"✅ Updated existing issue #{existing_issue} for {file_path}")
                self.stats.updated += 1
            else:
                # Create new issue
                issue_number = self.create_issue(
                    issue_title,
                    issue_body,
                    ['refactoring', 'automated', 'technical-debt']
                )
                print(f"✅ Created new issue #{issue_number} for {file_path}")
                self.stats.created += 1
            
            return True
            
        except Exception as e:
            print(f"❌ Failed to process {file_path}: {str(e)}")
            self.stats.failed += 1
            self.stats.errors.append((file_path, str(e)))
            
            # If it's a persistent rate limit, wait longer
            if isinstance(e, (RateLimitError, GitHubAPIError)) and 'rate limit' in str(e).lower():
                print(f"⏳ Rate limit persists, waiting 30s before continuing...")
                time.sleep(30)
            
            return False
    
    def print_summary(self):
        """Print a summary of the processing results."""
        print("\n" + "=" * 60)
        print("📊 SUMMARY")
        print("=" * 60)
        print(f"✅ Created: {self.stats.created}")
        print(f"🔄 Updated: {self.stats.updated}")
        print(f"⚠️  Skipped: {self.stats.skipped}")
        print(f"❌ Failed: {self.stats.failed}")
        print(f"📝 Total: {self.stats.total}")
        
        if self.stats.errors:
            print(f"\n❌ Errors encountered ({len(self.stats.errors)}):")
            for idx, (file_path, error) in enumerate(self.stats.errors, 1):
                print(f"  {idx}. {file_path}")
                print(f"     Error: {error}")


def parse_summary_file(summary_path: str) -> List[Tuple[str, str]]:
    """
    Parse the refactoring summary file to extract files with issues.
    
    Args:
        summary_path: Path to the summary file
        
    Returns:
        List of tuples (file_path, issue_description)
    """
    with open(summary_path, 'r') as f:
        content = f.read()
    
    # Extract lines starting with ⚠️
    warning_pattern = re.compile(r'⚠️\s+([^:]+):\s*(.+)')
    warnings = []
    
    for line in content.split('\n'):
        match = warning_pattern.match(line)
        if match:
            file_path = match.group(1).strip()
            description = match.group(2).strip()
            warnings.append((file_path, description))
    
    return warnings


def read_report_file(file_path: str, results_dir: str = "./results") -> Optional[str]:
    """
    Read the detailed report file for a given source file.
    
    Args:
        file_path: Path to the source file
        results_dir: Directory containing the report files
        
    Returns:
        Report content or None if file not found
    """
    # Sanitize filename (must match the shell script sanitization)
    safe_filename = file_path.replace('/', '-').replace('.', '-')
    report_path = os.path.join(results_dir, f"refactoring-report-{safe_filename}.txt")
    
    try:
        with open(report_path, 'r') as f:
            return f.read()
    except FileNotFoundError:
        print(f"⚠️  Report file not found: {report_path}")
        return None


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description="Create GitHub issues for refactoring opportunities"
    )
    parser.add_argument(
        '--summary',
        default='./refactoring-results-summary.txt',
        help='Path to the refactoring summary file'
    )
    parser.add_argument(
        '--results-dir',
        default='./results',
        help='Directory containing the detailed report files'
    )
    parser.add_argument(
        '--delay',
        type=float,
        default=2.0,
        help='Delay in seconds between API requests (default: 2.0)'
    )
    parser.add_argument(
        '--max-retries',
        type=int,
        default=3,
        help='Maximum number of retries for failed requests (default: 3)'
    )
    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='Dry run mode - do not actually create issues'
    )
    
    args = parser.parse_args()
    
    # Get GitHub credentials from environment
    token = os.environ.get('GITHUB_TOKEN')
    if not token:
        print("❌ Error: GITHUB_TOKEN environment variable not set")
        sys.exit(1)
    
    repo_full = os.environ.get('GITHUB_REPOSITORY')
    if not repo_full:
        print("❌ Error: GITHUB_REPOSITORY environment variable not set")
        sys.exit(1)
    
    try:
        owner, repo = repo_full.split('/')
    except ValueError:
        print(f"❌ Error: Invalid GITHUB_REPOSITORY format: {repo_full}")
        sys.exit(1)
    
    # Parse the summary file
    print(f"📖 Reading summary from: {args.summary}")
    warnings = parse_summary_file(args.summary)
    
    if not warnings:
        print("✅ No files with refactoring opportunities found")
        return
    
    print(f"Found {len(warnings)} files with refactoring opportunities")
    
    if args.dry_run:
        print("\n🔍 DRY RUN MODE - No issues will be created\n")
        for file_path, description in warnings:
            print(f"  - {file_path}: {description}")
        return
    
    # Create the issue creator
    creator = GitHubIssueCreator(
        token=token,
        owner=owner,
        repo=repo,
        delay_seconds=args.delay,
        max_retries=args.max_retries
    )
    
    creator.stats.total = len(warnings)
    
    # Process each file
    for idx, (file_path, description) in enumerate(warnings, 1):
        print(f"\nProcessing file {idx}/{len(warnings)}...")
        
        # Read the detailed report
        report_content = read_report_file(file_path, args.results_dir)
        if not report_content:
            print(f"⚠️  Skipping {file_path} - no report file found")
            creator.stats.skipped += 1
            continue
        
        # Process the file
        success = creator.process_file(file_path, description, report_content)
        
        # Throttle between files (except for the last one)
        if success and idx < len(warnings):
            creator._sleep()
    
    # Print summary
    creator.print_summary()
    
    # Exit with error code if there were failures
    if creator.stats.failed > 0:
        sys.exit(1)


if __name__ == '__main__':
    main()
