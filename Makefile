# Magica DAW - Simple Build System
# This Makefile provides a simple interface to build the Magica DAW project

# Build directories
BUILD_DIR = cmake-build-debug
BUILD_DIR_RELEASE = cmake-build-release

# Default target
.PHONY: all
all: debug

# Debug build
.PHONY: debug
debug:
	@echo "🔨 Building Magica DAW (Debug)..."
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Debug ..
	cd $(BUILD_DIR) && make -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Release build
.PHONY: release
release:
	@echo "🚀 Building Magica DAW (Release)..."
	@mkdir -p $(BUILD_DIR_RELEASE)
	cd $(BUILD_DIR_RELEASE) && cmake -DCMAKE_BUILD_TYPE=Release ..
	cd $(BUILD_DIR_RELEASE) && make -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Run the application
.PHONY: run
run: debug
	@echo "🎵 Running Magica DAW..."
	./$(BUILD_DIR)/daw/magica_daw_app

# Run tests
.PHONY: test
test: debug
	@echo "🧪 Running tests..."
	cd $(BUILD_DIR) && make test

# Clean build artifacts
.PHONY: clean
clean:
	@echo "🧹 Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(BUILD_DIR_RELEASE)
	rm -rf build/

# Clean and rebuild
.PHONY: rebuild
rebuild: clean debug

# Show help
.PHONY: help
help:
	@echo "🎵 Magica DAW - Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all, debug     - Build debug version (default)"
	@echo "  release        - Build release version"
	@echo "  run            - Build and run the application"
	@echo "  test           - Build and run tests"
	@echo "  clean          - Remove build artifacts"
	@echo "  rebuild        - Clean and rebuild"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Build directories:"
	@echo "  Debug:   $(BUILD_DIR)"
	@echo "  Release: $(BUILD_DIR_RELEASE)" 