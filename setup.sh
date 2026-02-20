#!/bin/bash

# Magica DAW Setup Script
# This script handles all the initial setup including git submodules

set -e  # Exit on any error

echo "🔮 Magica DAW Setup Script"
echo "=========================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in a git repository
if [ ! -d ".git" ]; then
    print_error "This script must be run from the root of the Magica repository"
    exit 1
fi

# Check if git is installed
if ! command -v git &> /dev/null; then
    print_error "git is not installed. Please install git first."
    exit 1
fi

# Initialize and update submodules
print_status "Initializing git submodules..."
git submodule update --init --recursive

# Check if submodules were initialized successfully
if [ ! -d "third_party/tracktion_engine/modules/juce/modules" ]; then
    print_error "Failed to initialize submodules. Please check your network connection and try again."
    exit 1
fi

print_success "Git submodules initialized successfully"

# Check for CMake
if ! command -v cmake &> /dev/null; then
    print_warning "CMake is not installed. You'll need CMake 3.20+ to build the project."
    print_status "Install CMake from: https://cmake.org/download/"
else
    CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
    print_success "CMake found: $CMAKE_VERSION"
fi

# Check for C compiler
C_COMPILER_FOUND=false
if command -v clang &> /dev/null; then
    C_COMPILER_VERSION=$(clang --version | head -n1)
    print_success "C compiler found: $C_COMPILER_VERSION"
    C_COMPILER_FOUND=true
elif command -v gcc &> /dev/null; then
    C_COMPILER_VERSION=$(gcc --version | head -n1)
    print_success "C compiler found: $C_COMPILER_VERSION"
    C_COMPILER_FOUND=true
else
    print_warning "No C compiler found. You'll need a C compiler to build JUCE components."
fi

# Check for C++ compiler
CXX_COMPILER_FOUND=false
if command -v clang++ &> /dev/null; then
    CXX_COMPILER_VERSION=$(clang++ --version | head -n1)
    print_success "C++ compiler found: $CXX_COMPILER_VERSION"
    CXX_COMPILER_FOUND=true
elif command -v g++ &> /dev/null; then
    CXX_COMPILER_VERSION=$(g++ --version | head -n1)
    print_success "C++ compiler found: $CXX_COMPILER_VERSION"
    CXX_COMPILER_FOUND=true
else
    print_warning "No C++ compiler found. You'll need C++20 support to build the project."
fi

# Overall compiler check
if [ "$C_COMPILER_FOUND" = false ] || [ "$CXX_COMPILER_FOUND" = false ]; then
    print_warning "Missing compilers detected. On macOS, install with: xcode-select --install"
    print_warning "On Linux, install with: sudo apt-get install build-essential (Ubuntu/Debian)"
    print_warning "Or: sudo dnf install gcc gcc-c++ make (Fedora/RHEL)"
fi

# Check for Make
if ! command -v make &> /dev/null; then
    print_warning "Make is not installed. You'll need Make to use the build system."
else
    print_success "Make found"
fi

# Optional: Set up pre-commit hooks
if command -v python3 &> /dev/null || command -v python &> /dev/null; then
    print_status "Setting up pre-commit hooks (optional)..."
    if command -v pip3 &> /dev/null; then
        pip3 install pre-commit --user 2>/dev/null || true
    elif command -v pip &> /dev/null; then
        pip install pre-commit --user 2>/dev/null || true
    fi

    if command -v pre-commit &> /dev/null; then
        pre-commit install 2>/dev/null || true
        print_success "Pre-commit hooks installed"
    else
        print_warning "pre-commit not found. Run 'pip install pre-commit && pre-commit install' to enable quality checks."
    fi
else
    print_warning "Python not found. Pre-commit hooks will not be installed."
fi

# Create build directory
print_status "Creating build directory..."
mkdir -p cmake-build-debug
print_success "Build directory created"

echo ""
print_success "Setup complete! 🎉"
echo ""
echo "Next steps:"
echo "  1. Build the project:    make debug"
echo "  2. Run tests:           make test"
echo "  3. Check code quality:  make quality"
echo "  4. Run the DAW:         make run"
echo ""
echo "For more information, see: make help"
