#!/usr/bin/env bash
# Run test suites with appropriate configuration

set -euo pipefail

TEST_TYPE="${1:-all}"

echo "🧪 Running Tests"
echo "================"
echo ""

# Detect test framework
if [ -f "package.json" ]; then
  if grep -q "jest" package.json; then
    FRAMEWORK="jest"
  elif grep -q "mocha" package.json; then
    FRAMEWORK="mocha"
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

echo "Detected framework: $FRAMEWORK"
echo ""

run_jest_tests() {
  case $TEST_TYPE in
    unit)
      echo "Running unit tests..."
      npm test -- --testPathPattern='.*\.(test|spec)\.(js|ts)$' --testPathIgnorePatterns='integration|e2e'
      ;;
    integration)
      echo "Running integration tests..."
      npm test -- --testPathPattern='integration'
      ;;
    e2e)
      echo "Running e2e tests..."
      npm test -- --testPathPattern='e2e'
      ;;
    all)
      echo "Running all tests..."
      npm test
      ;;
    *)
      echo "Unknown test type: $TEST_TYPE"
      exit 1
      ;;
  esac
}

run_pytest_tests() {
  case $TEST_TYPE in
    unit)
      echo "Running unit tests..."
      pytest tests/unit -v
      ;;
    integration)
      echo "Running integration tests..."
      pytest tests/integration -v
      ;;
    e2e)
      echo "Running e2e tests..."
      pytest tests/e2e -v
      ;;
    all)
      echo "Running all tests..."
      pytest tests -v
      ;;
    *)
      echo "Unknown test type: $TEST_TYPE"
      exit 1
      ;;
  esac
}

run_go_tests() {
  case $TEST_TYPE in
    unit)
      echo "Running unit tests..."
      go test -v -short ./...
      ;;
    integration)
      echo "Running integration tests..."
      go test -v -run Integration ./...
      ;;
    all)
      echo "Running all tests..."
      go test -v ./...
      ;;
    *)
      echo "Unknown test type: $TEST_TYPE"
      exit 1
      ;;
  esac
}

case $FRAMEWORK in
  jest|vitest)
    run_jest_tests
    ;;
  pytest)
    run_pytest_tests
    ;;
  go)
    run_go_tests
    ;;
  *)
    echo "Could not detect test framework"
    echo "Please run tests manually or update this script"
    exit 1
    ;;
esac
