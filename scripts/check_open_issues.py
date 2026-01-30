#!/usr/bin/env python3
"""
Pre-filter open GitHub issues by scanning the codebase for evidence of implementation.

For each non-automated open issue, searches for keywords from the issue title
in source files. Reports issues as LIKELY DONE, PARTIAL, or NO EVIDENCE.

Usage:
    python3 scripts/check_open_issues.py [--verbose] [--label LABEL] [--status STATUS]

Options:
    --verbose, -v       Show detailed file matches
    --label, -l LABEL   Filter issues by label (e.g. "epic:fx-chain", "enhancement")
                        Can be specified multiple times for AND filtering
    --status, -s STATUS Only show issues with this status: done, partial, maybe, none
                        Can be specified multiple times

Examples:
    python3 scripts/check_open_issues.py --label epic:fx-chain
    python3 scripts/check_open_issues.py --label enhancement --status done --status partial
    python3 scripts/check_open_issues.py -l epic:session-arrangement -v
"""

import argparse
import subprocess
import json
import re
from pathlib import Path


def get_open_issues():
    """Fetch non-automated open issues from GitHub."""
    result = subprocess.run(
        [
            "gh", "issue", "list",
            "--state", "open",
            "--search", "-label:automated",
            "--limit", "200",
            "--json", "number,title,labels,body",
        ],
        capture_output=True, text=True, check=True,
    )
    return json.loads(result.stdout)


def extract_keywords(title: str, body: str) -> list[str]:
    """Extract search keywords from issue title and body."""
    # Remove common filler words
    stop_words = {
        "implement", "implementation", "add", "create", "build", "make",
        "system", "support", "the", "for", "and", "with", "from", "into",
        "based", "options", "new", "core", "data", "types", "management",
        "integration", "abstract", "abstraction", "base", "interface",
    }

    # Get words from title
    words = re.findall(r'[A-Za-z][a-z]+(?:[A-Z][a-z]+)*', title)

    # Also extract CamelCase/PascalCase identifiers from body (class names, etc.)
    if body:
        identifiers = re.findall(r'\b([A-Z][a-zA-Z]{3,}(?::[A-Z][a-zA-Z]+)*)\b', body)
        words.extend(identifiers)

    keywords = []
    for w in words:
        low = w.lower()
        if low not in stop_words and len(low) > 2:
            keywords.append(w)

    return list(dict.fromkeys(keywords))  # dedupe, preserve order


def search_codebase(keyword: str, src_dirs: list[str]) -> list[str]:
    """Search for a keyword in source files, return matching files."""
    matches = set()
    for src_dir in src_dirs:
        if not Path(src_dir).exists():
            continue
        result = subprocess.run(
            [
                "grep", "-rl", "--include=*.cpp", "--include=*.hpp",
                "--include=*.h", "-i", keyword, src_dir,
            ],
            capture_output=True, text=True,
        )
        if result.returncode == 0:
            for line in result.stdout.strip().split("\n"):
                if line:
                    matches.add(line)
    return sorted(matches)


def check_class_exists(class_name: str, src_dirs: list[str]) -> list[str]:
    """Check if a class/struct definition exists."""
    matches = set()
    for src_dir in src_dirs:
        if not Path(src_dir).exists():
            continue
        result = subprocess.run(
            [
                "grep", "-rl", "--include=*.cpp", "--include=*.hpp",
                "--include=*.h", f"class {class_name}", src_dir,
            ],
            capture_output=True, text=True,
        )
        if result.returncode == 0:
            for line in result.stdout.strip().split("\n"):
                if line:
                    matches.add(line)
        # Also check struct
        result = subprocess.run(
            [
                "grep", "-rl", "--include=*.cpp", "--include=*.hpp",
                "--include=*.h", f"struct {class_name}", src_dir,
            ],
            capture_output=True, text=True,
        )
        if result.returncode == 0:
            for line in result.stdout.strip().split("\n"):
                if line:
                    matches.add(line)
    return sorted(matches)


def analyze_issue(issue: dict, src_dirs: list[str], verbose: bool) -> dict:
    """Analyze a single issue for implementation evidence."""
    title = issue["title"]
    body = issue.get("body", "") or ""
    number = issue["number"]

    keywords = extract_keywords(title, body)

    # Extract explicit class names from body (e.g., `ClassName` in backticks)
    class_names = re.findall(r'`([A-Z][a-zA-Z]+)`', body)
    # Also try to derive class names from title
    # e.g., "Auto-save system" → "AutoSave", "Theme system" → "Theme"
    title_parts = re.findall(r'[A-Z][a-z]+|[a-z]+', title)
    potential_class = "".join(p.capitalize() for p in title_parts if p.lower() not in {
        "implement", "add", "create", "system", "and", "for", "the",
        "with", "from", "based", "options", "abstract", "abstraction",
    })
    if len(potential_class) > 4:
        class_names.append(potential_class)

    all_hits = {}  # keyword -> [files]
    class_hits = {}  # class_name -> [files]

    for kw in keywords[:8]:  # limit to top 8 keywords
        files = search_codebase(kw, src_dirs)
        if files:
            all_hits[kw] = files

    for cn in class_names[:5]:
        files = check_class_exists(cn, src_dirs)
        if files:
            class_hits[cn] = files

    # Score the evidence
    total_keyword_files = len(set(f for files in all_hits.values() for f in files))
    total_class_files = len(set(f for files in class_hits.values() for f in files))
    keywords_matched = len(all_hits)
    classes_found = len(class_hits)

    if classes_found >= 2 or (classes_found >= 1 and keywords_matched >= 3):
        status = "LIKELY DONE"
    elif classes_found >= 1 or keywords_matched >= 3 or total_keyword_files >= 5:
        status = "PARTIAL"
    elif keywords_matched >= 2:
        status = "MAYBE"
    else:
        status = "NO EVIDENCE"

    result = {
        "number": number,
        "title": title,
        "status": status,
        "keywords_searched": keywords[:8],
        "keywords_matched": keywords_matched,
        "classes_found": classes_found,
        "total_files": total_keyword_files + total_class_files,
        "class_hits": class_hits,
        "keyword_hits": {k: len(v) for k, v in all_hits.items()},
    }

    if verbose:
        result["all_files"] = all_hits
        result["class_files"] = class_hits

    return result


def parse_args():
    parser = argparse.ArgumentParser(description="Check open issues for implementation evidence")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show detailed matches")
    parser.add_argument("--label", "-l", action="append", dest="labels", default=[],
                        help="Filter by label (can repeat for AND)")
    parser.add_argument("--status", "-s", action="append", dest="statuses", default=[],
                        choices=["done", "partial", "maybe", "none"],
                        help="Only show issues with this status (can repeat)")
    return parser.parse_args()


STATUS_MAP = {
    "done": "LIKELY DONE",
    "partial": "PARTIAL",
    "maybe": "MAYBE",
    "none": "NO EVIDENCE",
}


def main():
    args = parse_args()
    verbose = args.verbose
    filter_labels = [l.lower() for l in args.labels]
    filter_statuses = [STATUS_MAP[s] for s in args.statuses] if args.statuses else None

    src_dirs = ["magda/", "tests/"]
    print("Fetching open issues...")
    issues = get_open_issues()

    if filter_labels:
        issues = [
            i for i in issues
            if all(
                any(fl == label["name"].lower() for label in i.get("labels", []))
                for fl in filter_labels
            )
        ]
        print(f"Filtered to {len(issues)} issues matching label(s): {', '.join(args.labels)}")
    else:
        print(f"Found {len(issues)} non-automated open issues")
    print()

    results = {"LIKELY DONE": [], "PARTIAL": [], "MAYBE": [], "NO EVIDENCE": []}

    for issue in issues:
        r = analyze_issue(issue, src_dirs, verbose)
        results[r["status"]].append(r)
        # Progress indicator
        marker = {"LIKELY DONE": "✓", "PARTIAL": "~", "MAYBE": "?", "NO EVIDENCE": " "}
        print(f"  [{marker[r['status']]}] #{r['number']:>3}  {r['title'][:60]}")

    print("\n" + "=" * 72)
    print("RESULTS SUMMARY")
    print("=" * 72)

    for status in ["LIKELY DONE", "PARTIAL", "MAYBE", "NO EVIDENCE"]:
        group = results[status]
        if not group:
            continue
        if filter_statuses and status not in filter_statuses:
            continue
        print(f"\n--- {status} ({len(group)} issues) ---")
        for r in group:
            line = f"  #{r['number']:>3}  {r['title'][:55]}"
            detail_parts = []
            if r["classes_found"]:
                classes = ", ".join(r["class_hits"].keys())
                detail_parts.append(f"classes: {classes}")
            if r["keyword_hits"]:
                top_kw = sorted(r["keyword_hits"].items(), key=lambda x: -x[1])[:3]
                kw_str = ", ".join(f"{k}({v})" for k, v in top_kw)
                detail_parts.append(f"kw: {kw_str}")
            if detail_parts:
                line += f"  [{'; '.join(detail_parts)}]"
            print(line)

    likely = len(results["LIKELY DONE"])
    partial = len(results["PARTIAL"])
    total = len(issues)
    print(f"\n{likely} likely done, {partial} partial, out of {total} total open issues")


if __name__ == "__main__":
    main()
