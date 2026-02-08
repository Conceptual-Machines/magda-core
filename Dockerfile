FROM ubuntu:24.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies and tools
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    python3 \
    python3-pip \
    python3-venv \
    clang-format \
    clang-tidy \
    make \
    bash \
    libgtk-3-dev \
    libasound2-dev \
    libjack-jackd2-dev \
    ladspa-sdk \
    libcurl4-openssl-dev \
    libfreetype6-dev \
    libx11-dev \
    libxcomposite-dev \
    libxcursor-dev \
    libxext-dev \
    libxinerama-dev \
    libxrandr-dev \
    libxrender-dev \
    libwebkit2gtk-4.1-dev \
    libglu1-mesa-dev \
    mesa-common-dev \
    && rm -rf /var/lib/apt/lists/*

# Install pre-commit for git hooks
RUN pip3 install --break-system-packages pre-commit

# Install cmake-format for CMake formatting hooks
RUN pip3 install --break-system-packages cmake-format

# Configure git for pre-commit (needed for hooks to run)
RUN git config --global --add safe.directory /workspace || true
RUN git config --global init.defaultBranch main || true

# Set working directory
WORKDIR /workspace

# Set C++ standard and build type defaults
ENV CMAKE_CXX_STANDARD=20 \
    CMAKE_BUILD_TYPE=Debug

# Default command
CMD ["/bin/bash"]
