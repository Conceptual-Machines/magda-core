# MAGDA DAW - Simple Build System
# This Makefile provides a simple interface to build the MAGDA DAW project

# Build directories
BUILD_DIR = cmake-build-debug
BUILD_DIR_DEBUG_CPU = cmake-build-debug-cpu
BUILD_DIR_DEBUG_WEBGPU = cmake-build-debug-webgpu
BUILD_DIR_RELEASE = cmake-build-release
BUILD_DIR_ASAN = cmake-build-asan
BUILD_DIR_TSAN = cmake-build-tsan
DAWN_PREFIX ?= /private/tmp/dawn-webgpu-poc/install-macos11
FETCHCONTENT_SOURCE_ARGS = -DFETCHCONTENT_SOURCE_DIR_LUA=$(CURDIR)/cmake-build-debug/_deps/lua-src \
	-DFETCHCONTENT_SOURCE_DIR_SQLITE3=$(CURDIR)/cmake-build-debug/_deps/sqlite3-src \
	-DFETCHCONTENT_SOURCE_DIR_ONNXRUNTIME=$(CURDIR)/cmake-build-debug/_deps/onnxruntime-src \
	-DFETCHCONTENT_SOURCE_DIR_CATCH2=$(CURDIR)/cmake-build-debug/_deps/catch2-src
CACHE_ROOT = $(CURDIR)/.cache
BUILD_ENV = CCACHE_DIR=$(CACHE_ROOT)/ccache TMPDIR=$(CACHE_ROOT)/tmp XDG_CACHE_HOME=$(CACHE_ROOT)/xdg
TEST_ENV = $(BUILD_ENV) HOME=$(CACHE_ROOT)/home CFFIXED_USER_HOME=$(CACHE_ROOT)/home
LOG_DIR = logs
CPU_RUN_LOG = $(LOG_DIR)/run-console-cpu.log
WEBGPU_RUN_LOG = $(LOG_DIR)/run-console-webgpu.log
WEBGPU_SURFACE_RUN_LOG = $(LOG_DIR)/run-console-webgpu-surface.log

# Platform-specific binary layout.
# macOS: JUCE wraps the exe in a .app bundle (MAGDA.app/Contents/MacOS/MAGDA)
#        and we launch bundles with `open`.
# Linux: plain exe at the artefacts root, launched directly.
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    APP_BINARY_DEBUG   := $(BUILD_DIR)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.app/Contents/MacOS/MAGDA
    APP_BINARY_DEBUG_CPU := $(BUILD_DIR_DEBUG_CPU)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.app/Contents/MacOS/MAGDA
    APP_BINARY_DEBUG_WEBGPU := $(BUILD_DIR_DEBUG_WEBGPU)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.app/Contents/MacOS/MAGDA
    APP_BINARY_ASAN    := $(BUILD_DIR_ASAN)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.app/Contents/MacOS/MAGDA
    APP_BINARY_TSAN    := $(BUILD_DIR_TSAN)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.app/Contents/MacOS/MAGDA
    APP_BUNDLE_DEBUG   := $(BUILD_DIR)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.app
    APP_BUNDLE_RELEASE := $(BUILD_DIR_RELEASE)/magda/daw/magda_daw_app_artefacts/Release/MAGDA.app
    CLI_BINARY_DEBUG   := $(BUILD_DIR)/magda/daw/magda_cli_artefacts/Debug/magda_cli
    LAUNCH             := open
else
    APP_BINARY_DEBUG   := $(BUILD_DIR)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA
    APP_BINARY_DEBUG_CPU := $(BUILD_DIR_DEBUG_CPU)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA
    APP_BINARY_DEBUG_WEBGPU := $(BUILD_DIR_DEBUG_WEBGPU)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA
    APP_BINARY_ASAN    := $(BUILD_DIR_ASAN)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA
    APP_BINARY_TSAN    := $(BUILD_DIR_TSAN)/magda/daw/magda_daw_app_artefacts/Debug/MAGDA
    APP_BUNDLE_DEBUG   := $(APP_BINARY_DEBUG)
    APP_BUNDLE_RELEASE := $(BUILD_DIR_RELEASE)/magda/daw/magda_daw_app_artefacts/Release/MAGDA
    CLI_BINARY_DEBUG   := $(BUILD_DIR)/magda/daw/magda_cli_artefacts/Debug/magda_cli
    LAUNCH             :=
endif

# clang-tidy lookup. Homebrew's LLVM is newer than the Apple toolchain's, so
# prefer it when it is installed; otherwise take whatever is on PATH (the
# distro package on Linux). Override with `make lint CLANG_TIDY=/path/to/it`.
CLANG_TIDY ?= $(shell if [ -x /opt/homebrew/opt/llvm/bin/clang-tidy ]; then \
		echo /opt/homebrew/opt/llvm/bin/clang-tidy; \
	else \
		command -v clang-tidy 2>/dev/null; \
	fi)

# The compile database is written by Apple's /usr/bin/c++, which finds the macOS
# SDK through xcrun and so records no -isysroot. Homebrew's clang-tidy has no
# such fallback, and without the SDK it cannot find <vector> - which means the
# translation unit stops parsing at the first standard header and the run
# reports whatever it happened to see before that, rather than nothing. Empty on
# Linux, where the compiler and the analyser share a sysroot.
TIDY_SYSROOT := $(shell if [ "$$(uname)" = "Darwin" ]; then \
		SDK=$$(xcrun --show-sdk-path 2>/dev/null); \
		[ -n "$$SDK" ] && echo "--extra-arg=-isysroot --extra-arg=$$SDK"; \
	fi)

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
	@mkdir -p $(BUILD_DIR) $(CACHE_ROOT)/ccache $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "📝 Configuring project..."; \
		cd $(BUILD_DIR) && $(BUILD_ENV) cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..; \
	fi
	cd $(BUILD_DIR) && $(BUILD_ENV) ninja

.PHONY: debug-cpu
debug-cpu:
	@echo "🔨 Building MAGDA DAW (Debug, analyzer CPU baseline)..."
	@mkdir -p $(BUILD_DIR_DEBUG_CPU) $(CACHE_ROOT)/ccache $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR_DEBUG_CPU) && $(BUILD_ENV) cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=OFF \
		$(FETCHCONTENT_SOURCE_ARGS) \
		-DMAGDA_ENABLE_DAWN_ANALYZER_POC=OFF ..
	cd $(BUILD_DIR_DEBUG_CPU) && $(BUILD_ENV) ninja magda_daw_app

.PHONY: debug-webgpu
debug-webgpu:
	@echo "🔨 Building MAGDA DAW (Debug, Dawn/WebGPU analyzer POC)..."
	@mkdir -p $(BUILD_DIR_DEBUG_WEBGPU) $(CACHE_ROOT)/ccache $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR_DEBUG_WEBGPU) && $(BUILD_ENV) cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=OFF \
		$(FETCHCONTENT_SOURCE_ARGS) \
		-DMAGDA_ENABLE_DAWN_ANALYZER_POC=ON \
		-DCMAKE_PREFIX_PATH=$(DAWN_PREFIX) \
		-DDawn_DIR=$(DAWN_PREFIX)/lib/cmake/Dawn ..
	cd $(BUILD_DIR_DEBUG_WEBGPU) && $(BUILD_ENV) ninja magda_daw_app

# Reconfigure build (force CMake to run)
.PHONY: configure
configure:
	@echo "📝 Reconfiguring MAGDA DAW (Debug)..."
	@mkdir -p $(BUILD_DIR) $(CACHE_ROOT)/ccache $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR) && $(BUILD_ENV) cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..

# Release build
.PHONY: release
release:
	@echo "🚀 Building MAGDA DAW (Release)..."
	@mkdir -p $(BUILD_DIR_RELEASE) $(CACHE_ROOT)/ccache $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR_RELEASE) && $(BUILD_ENV) cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
	cd $(BUILD_DIR_RELEASE) && $(BUILD_ENV) ninja

# Run the release application
.PHONY: run-release
run-release: release
	@echo "🚀 Running MAGDA DAW (Release)..."
	$(LAUNCH) "$(APP_BUNDLE_RELEASE)"

# ASAN (AddressSanitizer) build
.PHONY: asan
asan:
	@echo "🔬 Building MAGDA DAW (Debug + AddressSanitizer)..."
	@mkdir -p $(BUILD_DIR_ASAN) $(CACHE_ROOT)/ccache $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	@if [ ! -f $(BUILD_DIR_ASAN)/CMakeCache.txt ]; then \
		echo "📝 Configuring project with ASAN..."; \
		cd $(BUILD_DIR_ASAN) && $(BUILD_ENV) cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
			-DCMAKE_CXX_FLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=address" \
			-DCMAKE_C_FLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=address" \
			-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" \
			-DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address" \
			-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..; \
	fi
	cd $(BUILD_DIR_ASAN) && $(BUILD_ENV) ninja

# Run with ASAN
.PHONY: run-asan
run-asan: asan
	@echo "🔬 Running MAGDA DAW with AddressSanitizer..."
	"$(APP_BINARY_ASAN)"

# TSAN (ThreadSanitizer) build — catches data races that ASAN cannot see.
.PHONY: tsan
tsan:
	@echo "🧵 Building MAGDA DAW (Debug + ThreadSanitizer)..."
	@mkdir -p $(BUILD_DIR_TSAN) $(CACHE_ROOT)/ccache $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	@if [ ! -f $(BUILD_DIR_TSAN)/CMakeCache.txt ]; then \
		echo "📝 Configuring project with TSAN..."; \
		cd $(BUILD_DIR_TSAN) && $(BUILD_ENV) cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
			-DCMAKE_CXX_FLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=thread" \
			-DCMAKE_C_FLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=thread" \
			-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
			-DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread" \
			-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..; \
	fi
	cd $(BUILD_DIR_TSAN) && $(BUILD_ENV) ninja

# Run with TSAN
.PHONY: run-tsan
run-tsan: tsan
	@echo "🧵 Running MAGDA DAW with ThreadSanitizer..."
	"$(APP_BINARY_TSAN)"

# Run the application
.PHONY: run
run: debug
	@echo "🎵 Running MAGDA DAW..."
	$(LAUNCH) "$(APP_BUNDLE_DEBUG)"

# Run the application from console (shows debug output)
.PHONY: run-console
run-console: debug
	@echo "🎵 Running MAGDA DAW (console mode)..."
	"$(APP_BINARY_DEBUG)"

.PHONY: cli
cli:
	@echo "🔨 Building magda-cli (Debug)..."
	@mkdir -p $(BUILD_DIR) $(CACHE_ROOT)/ccache $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "📝 Configuring project..."; \
		cd $(BUILD_DIR) && $(BUILD_ENV) cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..; \
	fi
	cd $(BUILD_DIR) && $(BUILD_ENV) ninja magda_cli

.PHONY: cli-boot
cli-boot: cli
	@echo "🎛️  Booting magda-cli headless..."
	"$(CLI_BINARY_DEBUG)" boot

.PHONY: run-console-cpu
run-console-cpu: debug-cpu
	@echo "🎵 Running MAGDA DAW (Debug console, analyzer CPU baseline)..."
	"$(APP_BINARY_DEBUG_CPU)"

.PHONY: run-console-webgpu
run-console-webgpu: debug-webgpu
	@echo "🎵 Running MAGDA DAW (Debug console, Dawn/WebGPU analyzer POC)..."
	"$(APP_BINARY_DEBUG_WEBGPU)"

.PHONY: run-console-webgpu-surface
run-console-webgpu-surface: debug-webgpu
	@echo "🎵 Running MAGDA DAW (Debug console, Dawn/WebGPU native surface POC)..."
	MAGDA_WEBGPU_NATIVE_SURFACE=1 "$(APP_BINARY_DEBUG_WEBGPU)"

.PHONY: run-console-cpu-log
run-console-cpu-log: debug-cpu
	@mkdir -p $(LOG_DIR)
	@echo "🎵 Running MAGDA DAW (Debug console, analyzer CPU baseline)..."
	@echo "📝 Writing app output to $(CPU_RUN_LOG)"
	"$(APP_BINARY_DEBUG_CPU)" 2>&1 | tee "$(CPU_RUN_LOG)"

.PHONY: run-console-webgpu-log
run-console-webgpu-log: debug-webgpu
	@mkdir -p $(LOG_DIR)
	@echo "🎵 Running MAGDA DAW (Debug console, Dawn/WebGPU analyzer POC)..."
	@echo "📝 Writing app output to $(WEBGPU_RUN_LOG)"
	"$(APP_BINARY_DEBUG_WEBGPU)" 2>&1 | tee "$(WEBGPU_RUN_LOG)"

.PHONY: run-console-webgpu-surface-log
run-console-webgpu-surface-log: debug-webgpu
	@mkdir -p $(LOG_DIR)
	@echo "🎵 Running MAGDA DAW (Debug console, Dawn/WebGPU native surface POC)..."
	@echo "📝 Writing app output to $(WEBGPU_SURFACE_RUN_LOG)"
	MAGDA_WEBGPU_NATIVE_SURFACE=1 "$(APP_BINARY_DEBUG_WEBGPU)" 2>&1 | tee "$(WEBGPU_SURFACE_RUN_LOG)"

# Run with profiling enabled
.PHONY: run-profile
run-profile: debug
	@echo "📊 Running MAGDA DAW with profiling enabled..."
	@echo "Performance data will be saved to:"
	@echo "  ~/Library/Application Support/MAGDA/Benchmarks/"
	@echo ""
	MAGDA_ENABLE_PROFILING=1 "$(APP_BINARY_DEBUG)"

# Build tests
.PHONY: test-build
test-build:
	@echo "🔨 Building tests..."
	@mkdir -p $(BUILD_DIR) $(CACHE_ROOT)/ccache $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "📝 Configuring project with tests enabled..."; \
		cd $(BUILD_DIR) && $(BUILD_ENV) cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..; \
	fi
	cd $(BUILD_DIR) && $(BUILD_ENV) ninja magda_tests

.PHONY: test-juce-build
test-juce-build:
	@echo "🔨 Building JUCE tests..."
	@mkdir -p $(BUILD_DIR) $(CACHE_ROOT)/ccache $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "📝 Configuring project with tests enabled..."; \
		cd $(BUILD_DIR) && $(BUILD_ENV) cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..; \
	fi
	cd $(BUILD_DIR) && $(BUILD_ENV) ninja magda_juce_tests

.PHONY: test-juce
test-juce: test-juce-build
	@echo "🧪 Running JUCE tests..."
	@mkdir -p $(CACHE_ROOT)/home $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	@JUCE_TEST_BIN=$$(find $(BUILD_DIR) -name magda_juce_tests -type f | head -1); \
	if [ -z "$$JUCE_TEST_BIN" ]; then \
		echo "❌ magda_juce_tests executable not found"; \
		exit 1; \
	fi; \
	$(TEST_ENV) "$$JUCE_TEST_BIN" $(if $(JUCE_TEST),"$(JUCE_TEST)",)

# Run all tests
.PHONY: test
test: test-build
	@echo "🧪 Running all tests..."
	@mkdir -p $(CACHE_ROOT)/home $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR) && $(TEST_ENV) ./tests/magda_tests

# Build and run the Catch2 tests under ThreadSanitizer. The native engine's
# parallel executor is lock-free, so "it passed" from an ordinary build says
# nothing about the orderings it did not happen to take. TEST=<filter> narrows
# it, e.g. make test-tsan TEST="[parallel]".
.PHONY: test-tsan-build
test-tsan-build:
	@echo "🧵 Building tests (Debug + ThreadSanitizer)..."
	@mkdir -p $(BUILD_DIR_TSAN) $(CACHE_ROOT)/ccache $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	@if [ ! -f $(BUILD_DIR_TSAN)/CMakeCache.txt ]; then \
		echo "📝 Configuring project with TSAN..."; \
		cd $(BUILD_DIR_TSAN) && $(BUILD_ENV) cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
			-DCMAKE_CXX_FLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=thread" \
			-DCMAKE_C_FLAGS="-g -O1 -fno-omit-frame-pointer -fsanitize=thread" \
			-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
			-DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread" \
			-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..; \
	fi
	cd $(BUILD_DIR_TSAN) && $(BUILD_ENV) ninja magda_tests

.PHONY: test-tsan
test-tsan: test-tsan-build
	@echo "🧵 Running tests with ThreadSanitizer..."
	@mkdir -p $(CACHE_ROOT)/home $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR_TSAN) && $(TEST_ENV) ./tests/magda_tests $(if $(TEST),"$(TEST)",)

# Run tests using CTest
.PHONY: test-ctest
test-ctest: test-build
	@echo "🧪 Running tests via CTest..."
	@mkdir -p $(CACHE_ROOT)/home $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR) && $(TEST_ENV) ctest --output-on-failure

# Run tests with verbose output
.PHONY: test-verbose
test-verbose: test-build
	@echo "🧪 Running tests (verbose)..."
	@mkdir -p $(CACHE_ROOT)/home $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR) && $(TEST_ENV) ./tests/magda_tests -s

# Run plugin window manager tests only
.PHONY: test-window
test-window: test-build
	@echo "🪟 Running plugin window tests..."
	@mkdir -p $(CACHE_ROOT)/home $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR) && $(TEST_ENV) ./tests/magda_tests "[ui][plugin][window]"

# Run shutdown sequence tests only
.PHONY: test-shutdown
test-shutdown: test-build
	@echo "🔚 Running shutdown tests..."
	@mkdir -p $(CACHE_ROOT)/home $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR) && $(TEST_ENV) ./tests/magda_tests "[ui][shutdown]"

# Run thread safety tests only
.PHONY: test-threading
test-threading: test-build
	@echo "🧵 Running thread safety tests..."
	@mkdir -p $(CACHE_ROOT)/home $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR) && $(TEST_ENV) ./tests/magda_tests "[threading]"

# List all available tests
.PHONY: test-list
test-list: test-build
	@echo "📋 Available tests:"
	@mkdir -p $(CACHE_ROOT)/home $(CACHE_ROOT)/tmp $(CACHE_ROOT)/xdg
	cd $(BUILD_DIR) && $(TEST_ENV) ./tests/magda_tests --list-tests

# Drive a running MAGDA over every remote transport (#2059).
#
# Not part of `make test`: it needs a MAGDA that is actually running with the
# remote API switched on, which is the whole point — it checks the installed
# artefact rather than the tree. ARGS passes flags through, e.g.
# `make test-transport ARGS="--osc --write"`.
.PHONY: test-transport
test-transport:
	@echo "🔌 Checking the remote transports against a running MAGDA..."
	@if [ -x .venv/bin/python ]; then \
		.venv/bin/python tools/transport_check $(ARGS); \
	else \
		python3 tools/transport_check $(ARGS); \
	fi

# The transport harness against a stub, so it can be checked with no MAGDA and
# no network. This one is safe to run anywhere.
.PHONY: test-transport-selftest
test-transport-selftest:
	@echo "🔌 Checking the transport harness itself..."
	python3 tools/transport_check/selftest.py

# Prove each check can fail: break the stub 23 ways and confirm the check meant
# to catch each one does. Slow — it runs the whole harness per mutation. Uses
# .venv when it exists, which adds the official-SDK mutations.
.PHONY: test-transport-mutations
test-transport-mutations:
	@echo "🔌 Breaking the stub to prove the checks bite..."
	@if [ -x .venv/bin/python ]; then \
		.venv/bin/python tools/transport_check/mutation_test.py; \
	else \
		python3 tools/transport_check/mutation_test.py; \
	fi

# Clean build artifacts
.PHONY: clean
clean:
	@echo "🧹 Cleaning build artifacts..."
	rm -rf $(BUILD_DIR) $(BUILD_DIR_RELEASE) $(BUILD_DIR_ASAN)
	rm -rf build/

# Clean and rebuild
.PHONY: rebuild
rebuild: clean debug

# Format code
.PHONY: format
# Formatting goes through pre-commit, which pins the clang-format version in
# .pre-commit-config.yaml. That pin is the point. Running whatever clang-format
# happens to be on PATH means every developer formats to their own version, the
# commit hook puts the file back on its way in, and the pair churn forever
# against each other. One pinned binary, one set of files, used by this target,
# by the hook and by CI.
#
# One behaviour worth knowing: --all-files means all *tracked* files, so a new
# file that has not been git added yet is skipped here where the old find would
# have formatted it. The commit hook catches it the moment it is staged, so
# nothing reaches a commit unformatted; "make format did nothing to my new file"
# is this, and not a bug.
format:
	@echo "🎨 Formatting code..."
	@if ! command -v pre-commit >/dev/null 2>&1; then \
		echo "❌ pre-commit not found. Install it with: pip install pre-commit"; \
		exit 1; \
	fi
	@# Twice on purpose. The first pass rewrites files and exits non-zero for
	@# saying so, which is why its status cannot be trusted; the second has
	@# nothing left to rewrite, so anything but success there is a real failure -
	@# a broken hook environment, a download that did not arrive, a config that
	@# does not parse. Swallowing the first status alone would report success for
	@# all of those while formatting nothing.
	@pre-commit run clang-format --all-files >/dev/null 2>&1 || true
	@if pre-commit run clang-format --all-files; then \
		echo "✅ Code formatting complete"; \
	else \
		echo "❌ Formatting failed for a reason other than rewriting files"; \
		exit 1; \
	fi

# Lint code with clang-tidy (analyze all source files)
.PHONY: lint
lint:
	@echo "🔍 Running clang-tidy on all source files..."
	@if [ ! -f $(BUILD_DIR)/compile_commands.json ]; then \
		echo "❌ compile_commands.json not found. Run 'make debug' first."; \
		exit 1; \
	fi
	@if [ -z "$(CLANG_TIDY)" ]; then \
		echo "❌ clang-tidy not found on PATH or at /opt/homebrew/opt/llvm/bin"; \
		echo "Install with: brew install llvm   (macOS)"; \
		echo "              apt install clang-tidy   (Debian/Ubuntu)"; \
		exit 1; \
	fi
	@echo "📋 Analyzing magda/daw sources..."
	@find magda/daw -name "*.cpp" -type f -exec \
		$(CLANG_TIDY) \
		{} \
		--config-file=.clang-tidy \
		--format-style=file \
		-p=$(BUILD_DIR) $(TIDY_SYSROOT) \
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
	@if [ -z "$(CLANG_TIDY)" ]; then \
		echo "❌ clang-tidy not found on PATH or at /opt/homebrew/opt/llvm/bin"; \
		exit 1; \
	fi
	@CHANGED_FILES=$$(git diff --name-only --diff-filter=d HEAD | grep '\.cpp$$' || true); \
	if [ -z "$$CHANGED_FILES" ]; then \
		echo "No modified .cpp files found"; \
	else \
		echo "Analyzing: $$CHANGED_FILES"; \
		for file in $$CHANGED_FILES; do \
			$(CLANG_TIDY) \
				$$file \
				--config-file=.clang-tidy \
				--format-style=file \
				-p=$(BUILD_DIR) $(TIDY_SYSROOT) \
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
	@if [ -z "$(CLANG_TIDY)" ]; then \
		echo "❌ clang-tidy not found on PATH or at /opt/homebrew/opt/llvm/bin"; \
		exit 1; \
	fi
	@echo "⚠️  This will modify your source files!"
	@printf "Continue? [y/N] "; \
	read REPLY; \
	case "$$REPLY" in \
		[Yy]*) \
			find magda/daw -name "*.cpp" -type f -exec \
				$(CLANG_TIDY) \
				{} \
				--config-file=.clang-tidy \
				--format-style=file \
				-p=$(BUILD_DIR) $(TIDY_SYSROOT) \
				--fix \
				--fix-errors \;; \
			echo "✅ Fixes applied" ;; \
		*) \
			echo "❌ Cancelled" ;; \
	esac

# Lint specific file
.PHONY: lint-file
lint-file:
	@if [ -z "$(FILE)" ]; then \
		echo "❌ Usage: make lint-file FILE=path/to/file.cpp"; \
		exit 1; \
	fi
	@echo "🔍 Analyzing $(FILE)..."
	@if [ -z "$(CLANG_TIDY)" ]; then \
		echo "❌ clang-tidy not found on PATH or at /opt/homebrew/opt/llvm/bin"; \
		exit 1; \
	fi
	@$(CLANG_TIDY) \
		$(FILE) \
		--config-file=.clang-tidy \
		--format-style=file \
		-p=$(BUILD_DIR) $(TIDY_SYSROOT)
	@echo "✅ Analysis complete"

# Show help
.PHONY: help
help:
	@echo "🎵 MAGDA DAW - Build System"
	@echo ""
	@echo "Build targets:"
	@echo "  all, debug     - Build debug version (default)"
	@echo "  debug-cpu      - Build debug analyzer CPU baseline"
	@echo "  debug-webgpu   - Build debug Dawn/WebGPU analyzer POC"
	@echo "  release        - Build release version"
	@echo "  configure      - Reconfigure CMake"
	@echo "  clean          - Remove build artifacts"
	@echo "  rebuild        - Clean and rebuild"
	@echo ""
	@echo "Run targets:"
	@echo "  run            - Build and run the application"
	@echo "  run-console    - Run with console output visible"
	@echo "  run-console-cpu - Run debug analyzer CPU baseline with console output"
	@echo "  run-console-webgpu - Run debug Dawn/WebGPU analyzer POC with console output"
	@echo "  run-console-cpu-log - Run CPU baseline and write app output to $(CPU_RUN_LOG)"
	@echo "  run-console-webgpu-log - Run WebGPU POC and write app output to $(WEBGPU_RUN_LOG)"
	@echo "  run-profile    - Run with performance profiling enabled"
	@echo "  asan           - Build with AddressSanitizer"
	@echo "  run-asan       - Build and run with AddressSanitizer"
	@echo "  tsan           - Build with ThreadSanitizer (catches data races)"
	@echo "  run-tsan       - Build and run with ThreadSanitizer"
	@echo ""
	@echo "Test targets:"
	@echo "  test-build     - Build tests only"
	@echo "  test-juce-build - Build JUCE integration tests only"
	@echo "  test-juce      - Build and run JUCE integration tests (optional JUCE_TEST=name)"
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
	@echo "  Debug CPU analyzer baseline: $(BUILD_DIR_DEBUG_CPU)"
	@echo "  Debug WebGPU analyzer POC:  $(BUILD_DIR_DEBUG_WEBGPU)"
	@echo "  Release: $(BUILD_DIR_RELEASE)"
	@echo "  ASAN:    $(BUILD_DIR_ASAN)"
