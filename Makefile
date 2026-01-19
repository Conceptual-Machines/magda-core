# Magica DAW - Simple Build System
# This Makefile provides a simple interface to build the Magica DAW project

# Build directories
BUILD_DIR = cmake-build-debug
BUILD_DIR_RELEASE = cmake-build-release

# Default target
.PHONY: all
all: debug

# Setup project (initialize submodules)
.PHONY: setup
setup:
	@echo "🔧 Setting up Magica DAW project..."
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
	@echo "🔨 Building Magica DAW (Debug)..."
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
	cd $(BUILD_DIR) && ninja

# Release build
.PHONY: release
release:
	@echo "🚀 Building Magica DAW (Release)..."
	@mkdir -p $(BUILD_DIR_RELEASE)
	cd $(BUILD_DIR_RELEASE) && cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
	cd $(BUILD_DIR_RELEASE) && ninja

# Run the application
.PHONY: run
run: debug
	@echo "🎵 Running Magica DAW..."
	open "$(BUILD_DIR)/magica/daw/magica_daw_app_artefacts/Debug/Magica DAW.app"

# Run the application from console (shows debug output)
.PHONY: run-console
run-console: debug
	@echo "🎵 Running Magica DAW (console mode)..."
	"$(BUILD_DIR)/magica/daw/magica_daw_app_artefacts/Debug/Magica DAW.app/Contents/MacOS/Magica DAW"

# Run tests
.PHONY: test
test: debug
	@echo "🧪 Running tests..."
	cd $(BUILD_DIR) && ninja test

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

# Lint code
.PHONY: lint
lint:
	@echo "🔍 Linting code..."
	@if command -v clang-tidy >/dev/null 2>&1; then \
		find . -name "*.cpp" | grep -E "(daw|agents|tests)" | xargs clang-tidy; \
		echo "✅ Code linting complete"; \
	else \
		echo "❌ clang-tidy not found. Please install it first."; \
	fi

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
	@echo "  setup          - Initialize git submodules"
	@echo "  clean          - Remove build artifacts"
	@echo "  rebuild        - Clean and rebuild"
	@echo "  format         - Format code with clang-format"
	@echo "  lint           - Lint code with clang-tidy"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Build directories:"
	@echo "  Debug:   $(BUILD_DIR)"
	@echo "  Release: $(BUILD_DIR_RELEASE)"
