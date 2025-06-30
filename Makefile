# Magda Makefile - Wrapper around CMake

BUILD_DIR = build
CMAKE_FLAGS = -DCMAKE_BUILD_TYPE=Release

.PHONY: all build clean debug release test examples install help

all: build

# Build in release mode
build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) ..
	@cd $(BUILD_DIR) && make -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Debug build
debug:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Debug ..
	@cd $(BUILD_DIR) && make -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Release build
release:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Release ..
	@cd $(BUILD_DIR) && make -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Build with JUCE adapter
juce:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DMAGDA_BUILD_JUCE_ADAPTER=ON $(CMAKE_FLAGS) ..
	@cd $(BUILD_DIR) && make -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Build and run tests
test:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DMAGDA_BUILD_TESTS=ON $(CMAKE_FLAGS) ..
	@cd $(BUILD_DIR) && make -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
	@cd $(BUILD_DIR) && ctest --output-on-failure

# Run tests (assumes already built)
test-only:
	@cd $(BUILD_DIR) && ctest --output-on-failure

# Build examples
examples:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DMAGDA_BUILD_EXAMPLES=ON $(CMAKE_FLAGS) ..
	@cd $(BUILD_DIR) && make -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Install
install: build
	@cd $(BUILD_DIR) && make install

# Clean build directory
clean:
	@rm -rf $(BUILD_DIR)
	@echo "Build directory cleaned"

# Configure only
configure:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake $(CMAKE_FLAGS) ..

# Help
help:
	@echo "Magda Build System"
	@echo "=================="
	@echo "Available targets:"
	@echo "  build     - Build in release mode (default)"
	@echo "  debug     - Build in debug mode"
	@echo "  release   - Build in release mode"
	@echo "  juce      - Build with JUCE adapter enabled"
	@echo "  test      - Build and run tests"
	@echo "  test-only - Run tests (assumes already built)"
	@echo "  examples  - Build examples"
	@echo "  install   - Install the library"
	@echo "  clean     - Clean build directory"
	@echo "  configure - Configure CMake only"
	@echo "  help      - Show this help" 