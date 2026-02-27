#!/usr/bin/env bash
# Generate test coverage report

set -euo pipefail

echo "📊 Generating Coverage Report"
echo "=============================="
echo ""

# Detect test framework
if [ -f "package.json" ]; then
  if grep -q "jest" package.json; then
    FRAMEWORK="jest"
  elif grep -q "vitest" package.json; then
    FRAMEWORK="vitest"
  else
    FRAMEWORK="unknown"
  fi
elif [ -f "pytest.ini" ] || [ -f "setup.py" ]; then
  FRAMEWORK="pytest"
elif [ -f "go.mod" ]; then
  FRAMEWORK="go"
else
  FRAMEWORK="unknown"
fi

case $FRAMEWORK in
  jest)
    echo "Running Jest with coverage..."
    npm test -- --coverage --coverageReporters=text --coverageReporters=html
    echo ""
    echo "Coverage report generated in: coverage/"
    echo "Open coverage/index.html to view detailed report"
    ;;

  vitest)
    echo "Running Vitest with coverage..."
    npx vitest run --coverage
    echo ""
    echo "Coverage report generated in: coverage/"
    ;;

  pytest)
    echo "Running pytest with coverage..."
    pytest --cov=. --cov-report=html --cov-report=term
    echo ""
    echo "Coverage report generated in: htmlcov/"
    echo "Open htmlcov/index.html to view detailed report"
    ;;

  go)
    echo "Running Go tests with coverage..."
    go test -v -coverprofile=coverage.out ./...
    go tool cover -html=coverage.out -o coverage.html
    echo ""
    echo "Coverage report generated: coverage.html"
    echo "Coverage summary:"
    go tool cover -func=coverage.out | tail -1
    ;;

  *)
    echo "Could not detect test framework"
    echo "Supported frameworks: Jest, Vitest, pytest, Go"
    exit 1
    ;;
esac
