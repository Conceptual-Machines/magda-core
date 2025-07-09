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
	@echo "Building Magica DAW (Debug)..."
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -DCMAKE_BUILD_TYPE=Debug ..
	cd $(BUILD_DIR) && make -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Release build
.PHONY: release
release:
	@echo "Building Magica DAW (Release)..."
	@mkdir -p $(BUILD_DIR_RELEASE)
	cd $(BUILD_DIR_RELEASE) && cmake -DCMAKE_BUILD_TYPE=Release ..
	cd $(BUILD_DIR_RELEASE) && make -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Run the application
.PHONY: run
run: debug
	@echo "Running Magica DAW..."
	"./$(BUILD_DIR)/daw/magica_daw_app_artefacts/Debug/Magica DAW.app/Contents/MacOS/Magica DAW"

# Open the application (macOS)
.PHONY: open
open:
	@echo "Opening Magica DAW..."
	open "$(BUILD_DIR)/daw/magica_daw_app_artefacts/Debug/Magica DAW.app"

# Build and open the application
.PHONY: build-open
build-open: debug open

# Run tests
.PHONY: test
test: debug
	@echo "Running tests..."
	cd $(BUILD_DIR) && ctest --output-on-failure
	@echo "Running JUCE tests directly..."
	@if [ -f "$(BUILD_DIR)/tests/magica_juce_tests_artefacts/Debug/magica_juce_tests" ]; then \
		"$(BUILD_DIR)/tests/magica_juce_tests_artefacts/Debug/magica_juce_tests"; \
	else \
		echo "JUCE tests not found, skipping..."; \
	fi
	@echo "Running Catch2 tests directly (if available)..."
	@if [ -f "$(BUILD_DIR)/tests/magica_tests" ]; then \
		"$(BUILD_DIR)/tests/magica_tests"; \
	else \
		echo "Catch2 tests not found, skipping..."; \
	fi

# Run only JUCE tests
.PHONY: test-juce
test-juce: debug
	@echo "Running JUCE tests..."
	@if [ -f "$(BUILD_DIR)/tests/magica_juce_tests_artefacts/Debug/magica_juce_tests" ]; then \
		"$(BUILD_DIR)/tests/magica_juce_tests_artefacts/Debug/magica_juce_tests"; \
	else \
		echo "JUCE tests not found. Build first with 'make debug'"; \
	fi

# Clean build artifacts
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(BUILD_DIR_RELEASE)
	rm -rf build/

# Clean and rebuild
.PHONY: rebuild
rebuild: clean debug



# Show help
.PHONY: help
help:
	@echo "Magica DAW - Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all, debug     - Build debug version (default)"
	@echo "  release        - Build release version"
	@echo "  run            - Build and run the application"
	@echo "  open           - Open the application (macOS)"
	@echo "  build-open     - Build and open the application"
	@echo "  test           - Build and run all tests (CTest + JUCE + Catch2)"
	@echo "  test-juce      - Build and run JUCE tests only"
	@echo "  clean          - Remove build artifacts"
	@echo "  rebuild        - Clean and rebuild"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Build directories:"
	@echo "  Debug:   $(BUILD_DIR)"
	@echo "  Release: $(BUILD_DIR_RELEASE)" 