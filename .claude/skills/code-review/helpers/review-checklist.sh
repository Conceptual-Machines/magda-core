#!/usr/bin/env bash
# Generate a code review checklist based on changed files

set -euo pipefail

FILES="${@:-.}"

echo "🔍 Code Review Checklist"
echo "========================"
echo ""

# Detect file types
has_js_ts=$(find $FILES -type f \( -name "*.js" -o -name "*.ts" -o -name "*.jsx" -o -name "*.tsx" \) 2>/dev/null | wc -l)
has_python=$(find $FILES -type f -name "*.py" 2>/dev/null | wc -l)
has_sql=$(find $FILES -type f -name "*.sql" 2>/dev/null | wc -l)
has_tests=$(find $FILES -type f \( -name "*.test.*" -o -name "*.spec.*" -o -path "*/tests/*" \) 2>/dev/null | wc -l)

echo "## Security"
echo "- [ ] No hardcoded secrets or API keys"
echo "- [ ] Input validation for untrusted data"
echo "- [ ] Output encoding for user-generated content"
echo "- [ ] Proper authentication and authorization checks"

if [ "$has_sql" -gt 0 ]; then
  echo "- [ ] SQL injection prevention (parameterized queries)"
fi

if [ "$has_js_ts" -gt 0 ]; then
  echo "- [ ] XSS prevention (proper escaping)"
fi

echo ""
echo "## Correctness"
echo "- [ ] Logic matches requirements"
echo "- [ ] Edge cases handled"
echo "- [ ] Error handling appropriate"
echo "- [ ] No off-by-one errors"
echo "- [ ] Null/undefined checks where needed"

echo ""
echo "## Code Quality"
echo "- [ ] Clear, descriptive naming"
echo "- [ ] Functions are focused and single-purpose"
echo "- [ ] Appropriate level of complexity"
echo "- [ ] No code duplication"
echo "- [ ] Follows project conventions"

if [ "$has_tests" -gt 0 ] || [ "$has_js_ts" -gt 0 ] || [ "$has_python" -gt 0 ]; then
  echo ""
  echo "## Testing"
  echo "- [ ] Critical paths have tests"
  echo "- [ ] Edge cases are tested"
  echo "- [ ] Error conditions are tested"
  echo "- [ ] Tests are clear and maintainable"
fi

echo ""
echo "## Documentation"
echo "- [ ] README updated if needed"
echo "- [ ] API changes documented"
echo "- [ ] Complex logic has explanatory comments"
echo "- [ ] Public interfaces documented"

echo ""
echo "## Performance"
echo "- [ ] No obvious performance issues"
echo "- [ ] Database queries are efficient"
echo "- [ ] No unnecessary computations in loops"
echo "- [ ] Appropriate caching if needed"

echo ""
echo "## Maintainability"
echo "- [ ] Code is easy to understand"
echo "- [ ] Changes are focused and minimal"
echo "- [ ] No unnecessary abstraction"
echo "- [ ] Technical debt addressed or documented"
