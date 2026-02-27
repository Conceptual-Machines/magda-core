#!/usr/bin/env bash
# Generate API documentation from code

set -euo pipefail

OUTPUT_DIR="${1:-docs/api}"

echo "📚 Generating API Documentation"
echo "================================"
echo ""

# Detect project type
if [ -f "package.json" ]; then
  if grep -q "swagger\|openapi" package.json; then
    echo "Detected OpenAPI/Swagger configuration"

    if command -v swagger-cli &> /dev/null; then
      echo "Generating OpenAPI docs..."
      swagger-cli bundle openapi.yaml -o "$OUTPUT_DIR/openapi.json"
    fi

    if command -v redoc-cli &> /dev/null; then
      echo "Generating ReDoc HTML..."
      redoc-cli bundle openapi.yaml -o "$OUTPUT_DIR/index.html"
    fi
  fi

  if grep -q "typedoc" package.json; then
    echo "Generating TypeDoc documentation..."
    npx typedoc --out "$OUTPUT_DIR" src/
  fi

  if grep -q "jsdoc" package.json; then
    echo "Generating JSDoc documentation..."
    npx jsdoc -c jsdoc.json -d "$OUTPUT_DIR"
  fi

elif [ -f "setup.py" ] || [ -f "pyproject.toml" ]; then
  echo "Detected Python project"

  if command -v sphinx-build &> /dev/null; then
    echo "Generating Sphinx documentation..."
    sphinx-build -b html docs/ "$OUTPUT_DIR"
  elif command -v pdoc &> /dev/null; then
    echo "Generating pdoc documentation..."
    pdoc --html --output-dir "$OUTPUT_DIR" .
  fi

elif [ -f "go.mod" ]; then
  echo "Detected Go project"
  echo "Generating godoc documentation..."
  godoc -http=:6060 &
  GODOC_PID=$!
  echo "godoc server running at http://localhost:6060"
  echo "Press Ctrl+C to stop"
  wait $GODOC_PID

else
  echo "Could not detect project type"
  echo "Supported: Node.js (OpenAPI, TypeDoc, JSDoc), Python (Sphinx, pdoc), Go (godoc)"
  exit 1
fi

echo ""
echo "✓ Documentation generated in: $OUTPUT_DIR"
