#!/usr/bin/env bash
# Check documentation coverage and quality

set -euo pipefail

echo "📖 Documentation Coverage Check"
echo "================================"
echo ""

# Colors
RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

issues=0

# Check for README
echo "Checking for README..."
if [ -f "README.md" ]; then
  echo -e "${GREEN}✓ README.md exists${NC}"

  # Check README sections
  if ! grep -q "## Installation\|## Setup" README.md; then
    echo -e "${YELLOW}⚠ README missing Installation/Setup section${NC}"
    issues=$((issues + 1))
  fi

  if ! grep -q "## Usage\|## Getting Started" README.md; then
    echo -e "${YELLOW}⚠ README missing Usage section${NC}"
    issues=$((issues + 1))
  fi
else
  echo -e "${RED}✗ README.md not found${NC}"
  issues=$((issues + 1))
fi
echo ""

# Check for undocumented public APIs
echo "Checking for undocumented code..."
if [ -f "package.json" ]; then
  # Check TypeScript/JavaScript
  undocumented=$(find src -name "*.ts" -o -name "*.js" | xargs grep -l "^export " | xargs grep -L "\/\*\*" | wc -l || echo "0")
  if [ "$undocumented" -gt 0 ]; then
    echo -e "${YELLOW}⚠ Found $undocumented files with undocumented exports${NC}"
    issues=$((issues + 1))
  else
    echo -e "${GREEN}✓ Public APIs appear documented${NC}"
  fi
elif [ -f "setup.py" ]; then
  # Check Python
  undocumented=$(find . -name "*.py" -not -path "./venv/*" -not -path "./.venv/*" | xargs grep -l "^def \|^class " | xargs grep -L "\"\"\"" | wc -l || echo "0")
  if [ "$undocumented" -gt 0 ]; then
    echo -e "${YELLOW}⚠ Found $undocumented Python files with undocumented functions/classes${NC}"
    issues=$((issues + 1))
  else
    echo -e "${GREEN}✓ Python code appears documented${NC}"
  fi
fi
echo ""

# Check for API documentation
echo "Checking for API documentation..."
if [ -f "openapi.yaml" ] || [ -f "openapi.json" ] || [ -f "swagger.yaml" ]; then
  echo -e "${GREEN}✓ API specification found${NC}"
elif [ -d "docs/api" ] || [ -d "docs/API" ]; then
  echo -e "${GREEN}✓ API documentation directory exists${NC}"
else
  echo -e "${YELLOW}⚠ No API documentation found${NC}"
  issues=$((issues + 1))
fi
echo ""

# Check for architecture documentation
echo "Checking for architecture documentation..."
if [ -f "docs/architecture.md" ] || [ -f "ARCHITECTURE.md" ] || [ -d "docs/architecture" ]; then
  echo -e "${GREEN}✓ Architecture documentation found${NC}"
else
  echo -e "${YELLOW}⚠ No architecture documentation found${NC}"
  echo "  Consider creating docs/architecture.md"
fi
echo ""

# Check for broken links in markdown
echo "Checking for broken links in documentation..."
broken_links=0
for file in $(find . -name "*.md" -not -path "./node_modules/*" -not -path "./.venv/*"); do
  # Extract links
  links=$(grep -o '\[.*\]([^)]*)' "$file" | sed 's/.*](\(.*\))/\1/' || echo "")
  for link in $links; do
    # Skip external links, anchors, and email
    if [[ $link == http* ]] || [[ $link == \#* ]] || [[ $link == mailto:* ]]; then
      continue
    fi

    # Check if file exists
    link_path=$(dirname "$file")/"$link"
    if [ ! -f "$link_path" ] && [ ! -d "$link_path" ]; then
      echo -e "${RED}✗ Broken link in $file: $link${NC}"
      broken_links=$((broken_links + 1))
    fi
  done
done

if [ $broken_links -eq 0 ]; then
  echo -e "${GREEN}✓ No broken links found${NC}"
else
  echo -e "${RED}✗ Found $broken_links broken link(s)${NC}"
  issues=$((issues + 1))
fi
echo ""

# Summary
echo "================================"
if [ $issues -eq 0 ]; then
  echo -e "${GREEN}✓ Documentation looks good!${NC}"
  exit 0
else
  echo -e "${YELLOW}Found $issues documentation issue(s) to address${NC}"
  exit 1
fi
