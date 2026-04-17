#!/usr/bin/env python3
"""
MAGDA semi-automated Windows crash analyser.

Workflow:
  1. Parse the .dmp with parse-minidump.py
  2. Optionally download the matching PDB from GitHub releases and symbolize
  3. Feed the report to Claude for root-cause analysis
  4. Optionally create a Linear issue with the findings

Usage:
  analyze-crash.py --dump FILE.dmp
  analyze-crash.py --dump FILE.dmp --version 0.4.5
  analyze-crash.py --dump FILE.dmp --pdb MAGDA.pdb
  analyze-crash.py --dump FILE.dmp --version 0.4.5 --create-issue

Environment variables required for full automation:
  ANTHROPIC_API_KEY   Claude API key
  GITHUB_TOKEN        GitHub PAT with repo read access (for PDB download)
  LINEAR_API_KEY      Linear API key (only needed with --create-issue)
  LINEAR_TEAM_ID      Linear team ID  (only needed with --create-issue)
"""

import argparse
import os
import sys
import subprocess
import tempfile

REPO = "Conceptual-Machines/magda-core"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PARSE_SCRIPT = os.path.join(SCRIPT_DIR, "parse-minidump.py")


# ── PDB download ──────────────────────────────────────────────────────────────

def download_pdb(version, dest_dir):
    """Download MAGDA-{version}-Windows-x86_64.pdb from GitHub releases."""
    try:
        import urllib.request, urllib.error
    except ImportError:
        print("[warn] urllib not available — skipping PDB download")
        return None

    token = os.environ.get("GITHUB_TOKEN")
    pdb_name = f"MAGDA-{version}-Windows-x86_64.pdb"
    url = f"https://github.com/{REPO}/releases/download/v{version}/{pdb_name}"
    dest = os.path.join(dest_dir, pdb_name)

    headers = {"Accept": "application/octet-stream"}
    if token:
        headers["Authorization"] = f"Bearer {token}"

    print(f"Downloading PDB for v{version}...")
    try:
        req = urllib.request.Request(url, headers=headers)
        with urllib.request.urlopen(req, timeout=60) as resp, open(dest, "wb") as f:
            while chunk := resp.read(65536):
                f.write(chunk)
        print(f"  → saved to {dest}")
        return dest
    except urllib.error.HTTPError as e:
        if e.code == 404:
            print(f"  [warn] PDB not found in v{version} release assets — "
                  "was this version built before PDB upload was added?")
        else:
            print(f"  [warn] HTTP {e.code} downloading PDB: {e}")
        return None
    except Exception as e:
        print(f"  [warn] could not download PDB: {e}")
        return None


# ── Parse dump → text report ──────────────────────────────────────────────────

def run_parse(dump_path, pdb_path=None):
    """Run parse-minidump.py and capture its stdout."""
    cmd = [sys.executable, PARSE_SCRIPT, dump_path]
    if pdb_path:
        cmd += ["--pdb", pdb_path]

    result = subprocess.run(cmd, capture_output=True, text=True)
    output = result.stdout
    if result.returncode != 0:
        output += f"\n[stderr]\n{result.stderr}"
    return output


# ── Claude analysis ───────────────────────────────────────────────────────────

SYSTEM_PROMPT = """\
You are an expert C++/JUCE crash analyst for MAGDA, a DAW built on JUCE and Tracktion Engine.
When given a Windows minidump analysis, your job is to:
1. Identify the most likely root cause (null pointer, dangling reference, use-after-free, etc.)
2. Pinpoint the specific function or subsystem responsible
3. Suggest a concrete fix or investigation path
4. Rate your confidence (high/medium/low) given the available symbol information

Be concise. Lead with the most actionable finding. If symbols are absent, focus on the
exception type, register state, and the pattern of stack addresses to narrow down the subsystem.
"""

def analyse_with_claude(report_text):
    """Send the parsed report to Claude and return its analysis."""
    api_key = os.environ.get("ANTHROPIC_API_KEY")
    if not api_key:
        print("[warn] ANTHROPIC_API_KEY not set — skipping Claude analysis")
        return None

    try:
        import anthropic
    except ImportError:
        print("[warn] anthropic package not installed — run: pip install anthropic")
        return None

    client = anthropic.Anthropic(api_key=api_key)
    print("Sending to Claude for analysis...")

    message = client.messages.create(
        model="claude-opus-4-6",
        max_tokens=1024,
        system=SYSTEM_PROMPT,
        messages=[
            {
                "role": "user",
                "content": f"Analyse this MAGDA crash dump:\n\n```\n{report_text}\n```",
            }
        ],
    )
    return message.content[0].text


# ── Linear issue creation ─────────────────────────────────────────────────────

def create_linear_issue(title, body):
    """Create a Linear issue and return its URL."""
    api_key = os.environ.get("LINEAR_API_KEY")
    team_id = os.environ.get("LINEAR_TEAM_ID")
    if not api_key or not team_id:
        print("[warn] LINEAR_API_KEY / LINEAR_TEAM_ID not set — skipping issue creation")
        return None

    try:
        import urllib.request, urllib.error, json
    except ImportError:
        return None

    mutation = """
    mutation IssueCreate($input: IssueCreateInput!) {
      issueCreate(input: $input) {
        success
        issue { identifier url title }
      }
    }
    """
    variables = {
        "input": {
            "teamId": team_id,
            "title": title,
            "description": body,
            "labelIds": [],
        }
    }
    payload = json.dumps({"query": mutation, "variables": variables}).encode()
    req = urllib.request.Request(
        "https://api.linear.app/graphql",
        data=payload,
        headers={
            "Authorization": api_key,
            "Content-Type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            data = json.loads(resp.read())
        issue = data["data"]["issueCreate"]["issue"]
        print(f"Linear issue created: {issue['identifier']} — {issue['url']}")
        return issue["url"]
    except Exception as e:
        print(f"[warn] Linear issue creation failed: {e}")
        return None


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="MAGDA Windows crash analyser")
    ap.add_argument("--dump", required=True, help=".dmp file path")
    ap.add_argument("--version", help="MAGDA version (e.g. 0.4.5) — downloads matching PDB")
    ap.add_argument("--pdb", help="Local path to MAGDA.pdb (skips download)")
    ap.add_argument("--create-issue", action="store_true",
                    help="Create a Linear issue with the analysis")
    ap.add_argument("--no-claude", action="store_true",
                    help="Skip Claude analysis (just parse and print)")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        pdb_path = args.pdb

        # Download PDB if version given and no local PDB
        if args.version and not pdb_path:
            pdb_path = download_pdb(args.version, tmp)

        # Parse the dump
        print(f"\nParsing {args.dump}...\n")
        report = run_parse(args.dump, pdb_path)
        print(report)

        # Claude analysis
        analysis = None
        if not args.no_claude:
            analysis = analyse_with_claude(report)
            if analysis:
                print("\n" + "="*70)
                print("CLAUDE ANALYSIS")
                print("="*70)
                print(analysis)

        # Linear issue
        if args.create_issue and analysis:
            version_tag = f"v{args.version}" if args.version else "unknown version"
            title = f"Windows crash ({version_tag}) — {_extract_exc_code(report)}"
            body = (
                f"## Crash report ({version_tag})\n\n"
                f"```\n{report[:3000]}{'...' if len(report) > 3000 else ''}\n```\n\n"
                f"## Analysis\n\n{analysis}"
            )
            create_linear_issue(title, body)


def _extract_exc_code(report):
    """Pull the exception description from the report for the issue title."""
    for line in report.splitlines():
        if "Code:" in line:
            return line.strip().split("Code:", 1)[1].strip()
    return "ACCESS_VIOLATION"


if __name__ == "__main__":
    main()
