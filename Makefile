# MAGDA DAW - Simple Build System
# This Makefile provides a simple interface to build the MAGDA DAW project

# Build directories
BUILD_DIR = cmake-build-debug
BUILD_DIR_RELEASE = cmake-build-release

# Default target
.PHONY: all
all: debug

# Setup project (initialize submodules)
.PHONY: setup
setup:
	@echo "🔧 Setting up MAGDA DAW project..."
	@if [ ! -d ".git" ]; then \
		echo "❌ Error: Not a git repository"; \
		exit 1; \
	fi
	@echo "📦 Initializing git submodules..."
	@git submodule update --init --recursive
	@echo "✅ Project setup complete!"

# Debug build
.PHONY: debug
debug:
	@echo "🔨 Building MAGDA DAW (Debug)..."
	@mkdir -p $(BUILD_DIR)
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "📝 Configuring project..."; \
		cd $(BUILD_DIR) && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..; \
	fi
	cd $(BUILD_DIR) && ninja

# Reconfigure build (force CMake to run)
.PHONY: configure
configure:
	@echo "📝 Reconfiguring MAGDA DAW (Debug)..."
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..

# Release build
.PHONY: release
release:
	@echo "🚀 Building MAGDA DAW (Release)..."
	@mkdir -p $(BUILD_DIR_RELEASE)
	cd $(BUILD_DIR_RELEASE) && cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
	cd $(BUILD_DIR_RELEASE) && ninja

# Run the application
.PHONY: run
run: debug
	@echo "🎵 Running MAGDA DAW..."
	open "$(BUILD_DIR)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.app"

# Run the application from console (shows debug output)
.PHONY: run-console
run-console: debug
	@echo "🎵 Running MAGDA DAW (console mode)..."
	"$(BUILD_DIR)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.app/Contents/MacOS/MAGDA"

# Run with profiling enabled
.PHONY: run-profile
run-profile: debug
	@echo "📊 Running MAGDA DAW with profiling enabled..."
	@echo "Performance data will be saved to:"
	@echo "  ~/Library/Application Support/MAGDA/Benchmarks/"
	@echo ""
	MAGDA_ENABLE_PROFILING=1 "$(BUILD_DIR)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.app/Contents/MacOS/MAGDA"

# Build tests
.PHONY: test-build
test-build:
	@echo "🔨 Building tests..."
	@mkdir -p $(BUILD_DIR)
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "📝 Configuring project with tests enabled..."; \
		cd $(BUILD_DIR) && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..; \
	fi
	cd $(BUILD_DIR) && ninja magda_tests

# Run all tests
.PHONY: test
test: test-build
	@echo "🧪 Running all tests..."
	cd $(BUILD_DIR) && ./tests/magda_tests

# Run tests using CTest
.PHONY: test-ctest
test-ctest: test-build
	@echo "🧪 Running tests via CTest..."
	cd $(BUILD_DIR) && ctest --output-on-failure

# Run tests with verbose output
.PHONY: test-verbose
test-verbose: test-build
	@echo "🧪 Running tests (verbose)..."
	cd $(BUILD_DIR) && ./tests/magda_tests -s

# Run plugin window manager tests only
.PHONY: test-window
test-window: test-build
	@echo "🪟 Running plugin window tests..."
	cd $(BUILD_DIR) && ./tests/magda_tests "[ui][plugin][window]"

# Run shutdown sequence tests only
.PHONY: test-shutdown
test-shutdown: test-build
	@echo "🔚 Running shutdown tests..."
	cd $(BUILD_DIR) && ./tests/magda_tests "[ui][shutdown]"

# Run thread safety tests only
.PHONY: test-threading
test-threading: test-build
	@echo "🧵 Running thread safety tests..."
	cd $(BUILD_DIR) && ./tests/magda_tests "[threading]"

# List all available tests
.PHONY: test-list
test-list: test-build
	@echo "📋 Available tests:"
	cd $(BUILD_DIR) && ./tests/magda_tests --list-tests

# Clean build artifacts
.PHONY: clean
clean:
	@echo "🧹 Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(BUILD_DIR_RELEASE)
	rm -rf build/

# Clean and rebuild
.PHONY: rebuild
rebuild: clean debug

# Format code
.PHONY: format
format:
	@echo "🎨 Formatting code..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find . -name "*.cpp" -o -name "*.hpp" -o -name "*.h" | grep -E "(daw|agents|tests)" | xargs clang-format -i; \
		echo "✅ Code formatting complete"; \
	else \
		echo "❌ clang-format not found. Please install it first."; \
	fi

# Lint code with clang-tidy (analyze all source files)
.PHONY: lint
lint:
	@echo "🔍 Running clang-tidy on all source files..."
	@if [ ! -f $(BUILD_DIR)/compile_commands.json ]; then \
		echo "❌ compile_commands.json not found. Run 'make debug' first."; \
		exit 1; \
	fi
	@if [ ! -f /opt/homebrew/opt/llvm/bin/clang-tidy ]; then \
		echo "❌ clang-tidy not found at /opt/homebrew/opt/llvm/bin/clang-tidy"; \
		echo "Install with: brew install llvm"; \
		exit 1; \
	fi
	@echo "📋 Analyzing magda/daw sources..."
	@find magda/daw -name "*.cpp" -type f -exec \
		/opt/homebrew/opt/llvm/bin/clang-tidy \
		{} \
		--config-file=.clang-tidy \
		--format-style=file \
		-p=$(BUILD_DIR) \
		--quiet \;
	@echo "✅ Code analysis complete"

# Lint recently modified files only
.PHONY: lint-changed
lint-changed:
	@echo "🔍 Running clang-tidy on modified files..."
	@if [ ! -f $(BUILD_DIR)/compile_commands.json ]; then \
		echo "❌ compile_commands.json not found. Run 'make debug' first."; \
		exit 1; \
	fi
	@CHANGED_FILES=$$(git diff --name-only --diff-filter=d HEAD | grep '\.cpp$$' || true); \
	if [ -z "$$CHANGED_FILES" ]; then \
		echo "No modified .cpp files found"; \
	else \
		echo "Analyzing: $$CHANGED_FILES"; \
		for file in $$CHANGED_FILES; do \
			/opt/homebrew/opt/llvm/bin/clang-tidy \
				$$file \
				--config-file=.clang-tidy \
				--format-style=file \
				-p=$(BUILD_DIR) \
				--quiet; \
		done; \
	fi
	@echo "✅ Analysis complete"

# Lint with automatic fixes (use with caution)
.PHONY: lint-fix
lint-fix:
	@echo "🔧 Running clang-tidy with automatic fixes..."
	@if [ ! -f $(BUILD_DIR)/compile_commands.json ]; then \
		echo "❌ compile_commands.json not found. Run 'make debug' first."; \
		exit 1; \
	fi
	@echo "⚠️  This will modify your source files!"
	@read -p "Continue? [y/N] " -n 1 -r; \
	echo; \
	if [[ $$REPLY =~ ^[Yy]$$ ]]; then \
		find magda/daw -name "*.cpp" -type f -exec \
			/opt/homebrew/opt/llvm/bin/clang-tidy \
			{} \
			--config-file=.clang-tidy \
			--format-style=file \
			-p=$(BUILD_DIR) \
			--fix \
			--fix-errors \;; \
		echo "✅ Fixes applied"; \
	else \
		echo "❌ Cancelled"; \
	fi

# Lint specific file
.PHONY: lint-file
lint-file:
	@if [ -z "$(FILE)" ]; then \
		echo "❌ Usage: make lint-file FILE=path/to/file.cpp"; \
		exit 1; \
	fi
	@echo "🔍 Analyzing $(FILE)..."
	@/opt/homebrew/opt/llvm/bin/clang-tidy \
		$(FILE) \
		--config-file=.clang-tidy \
		--format-style=file \
		-p=$(BUILD_DIR)
	@echo "✅ Analysis complete"

# Show help
.PHONY: help
help:
	@echo "🎵 MAGDA DAW - Build System"
	@echo ""
	@echo "Build targets:"
	@echo "  all, debug     - Build debug version (default)"
	@echo "  release        - Build release version"
	@echo "  configure      - Reconfigure CMake"
	@echo "  clean          - Remove build artifacts"
	@echo "  rebuild        - Clean and rebuild"
	@echo ""
	@echo "Run targets:"
	@echo "  run            - Build and run the application"
	@echo "  run-console    - Run with console output visible"
	@echo "  run-profile    - Run with performance profiling enabled"
	@echo ""
	@echo "Test targets:"
	@echo "  test-build     - Build tests only"
	@echo "  test           - Build and run all tests"
	@echo "  test-ctest     - Run tests via CTest"
	@echo "  test-verbose   - Run tests with verbose output"
	@echo "  test-window    - Run plugin window tests only"
	@echo "  test-shutdown  - Run shutdown sequence tests only"
	@echo "  test-threading - Run thread safety tests only"
	@echo "  test-list      - List all available tests"
	@echo ""
	@echo "Code Quality targets:"
	@echo "  format         - Format code with clang-format"
	@echo "  lint           - Analyze all source files with clang-tidy"
	@echo "  lint-changed   - Analyze only modified files"
	@echo "  lint-fix       - Apply automatic fixes (use with caution)"
	@echo "  lint-file      - Analyze specific file (usage: make lint-file FILE=path/to/file.cpp)"
	@echo ""
	@echo "Other targets:"
	@echo "  setup          - Initialize git submodules"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Build directories:"
	@echo "  Debug:   $(BUILD_DIR)"
	@echo "  Release: $(BUILD_DIR_RELEASE)"
