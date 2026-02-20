#!/bin/bash
# Helper script for building Magda in Docker

set -e

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Building Docker image...${NC}"
docker build -t magda-build .

echo -e "${BLUE}Configuring CMake...${NC}"
docker run --rm -v "$PWD:/workspace" magda-build \
    cmake -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DMAGDA_BUILD_TESTS=ON

echo -e "${BLUE}Building project...${NC}"
docker run --rm -v "$PWD:/workspace" magda-build \
    bash -c "cmake --build build --config Debug -j\$(nproc)"

echo -e "${BLUE}Running tests...${NC}"
docker run --rm -v "$PWD:/workspace" magda-build \
    ./build/tests/magda_tests

echo -e "${GREEN}✅ Build and tests completed successfully!${NC}"
