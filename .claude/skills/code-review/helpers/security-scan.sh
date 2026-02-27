#!/usr/bin/env bash
# Basic security scan for common vulnerabilities

set -euo pipefail

FILES="${@:-.}"

echo "🔒 Security Scan"
echo "================"
echo ""

# Colors
RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

issues_found=0

# Check for hardcoded secrets
echo "Checking for hardcoded secrets..."
if grep -r -i -n -E "(password|api[_-]?key|secret|token|private[_-]?key)\s*=\s*['\"][^'\"]{8,}" $FILES 2>/dev/null; then
  echo -e "${RED}⚠ WARNING: Possible hardcoded secrets found${NC}"
  issues_found=$((issues_found + 1))
else
  echo -e "${GREEN}✓ No obvious hardcoded secrets${NC}"
fi
echo ""

# Check for SQL injection vulnerabilities
echo "Checking for SQL injection risks..."
if grep -r -n "execute.*+\|query.*+\|sql.*+.*%" $FILES 2>/dev/null | grep -v "test\|spec"; then
  echo -e "${RED}⚠ WARNING: Possible SQL injection vulnerability (string concatenation in queries)${NC}"
  issues_found=$((issues_found + 1))
else
  echo -e "${GREEN}✓ No obvious SQL injection risks${NC}"
fi
echo ""

# Check for command injection vulnerabilities
echo "Checking for command injection risks..."
if grep -r -n "exec\|eval\|system\|shell" $FILES 2>/dev/null | grep -v "test\|spec\|node_modules"; then
  echo -e "${YELLOW}⚠ NOTICE: Found exec/eval/system calls - verify input is sanitized${NC}"
fi
echo ""

# Check for debug code
echo "Checking for debug code..."
if grep -r -n "console\.log\|debugger\|print(" $FILES 2>/dev/null | grep -v "test\|spec\|node_modules" | head -5; then
  echo -e "${YELLOW}⚠ NOTICE: Debug code found (may expose sensitive information)${NC}"
fi
echo ""

# Check for TODO/FIXME with security
echo "Checking for security-related TODOs..."
if grep -r -n -i "TODO.*security\|FIXME.*security\|XXX.*security" $FILES 2>/dev/null; then
  echo -e "${YELLOW}⚠ NOTICE: Security-related TODOs found${NC}"
fi
echo ""

# Summary
echo "========================"
if [ $issues_found -eq 0 ]; then
  echo -e "${GREEN}✓ No critical security issues found${NC}"
  echo "Note: This is a basic scan. Manual review is still required."
else
  echo -e "${RED}⚠ Found $issues_found potential security issue(s)${NC}"
  echo "Please review and address before merging."
fi
