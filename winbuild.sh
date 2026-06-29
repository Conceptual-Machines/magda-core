#!/bin/bash
# Windows build helper - sets up MSVC environment and builds

MSVC_VER="14.50.35717"
VS_ROOT="C:/Program Files/Microsoft Visual Studio/18/Community"
WINSDK_VER="10.0.26100.0"
WINSDK_ROOT="C:/Program Files (x86)/Windows Kits/10"
# Bash-style PATH forms: ':' is the separator, so 'C:/...' is parsed as two
# entries. Use '/c/...' instead, and 8.3 short names where parentheses appear.
VS_ROOT_BASH="/c/Program Files/Microsoft Visual Studio/18/Community"
WINSDK_ROOT_SHORT="/c/PROGRA~2/Windows Kits/10"

export INCLUDE="${VS_ROOT}/VC/Tools/MSVC/${MSVC_VER}/include;${WINSDK_ROOT}/Include/${WINSDK_VER}/ucrt;${WINSDK_ROOT}/Include/${WINSDK_VER}/shared;${WINSDK_ROOT}/Include/${WINSDK_VER}/um;${WINSDK_ROOT}/Include/${WINSDK_VER}/winrt;${WINSDK_ROOT}/Include/${WINSDK_VER}/cppwinrt"
export LIB="${VS_ROOT}/VC/Tools/MSVC/${MSVC_VER}/lib/x64;${WINSDK_ROOT}/Lib/${WINSDK_VER}/ucrt/x64;${WINSDK_ROOT}/Lib/${WINSDK_VER}/um/x64"
export PATH="${VS_ROOT_BASH}/VC/Tools/MSVC/${MSVC_VER}/bin/Hostx64/x64:${WINSDK_ROOT_SHORT}/bin/${WINSDK_VER}/x64:${PATH}"

BUILD_DIR="cmake-build-debug"
EXE="${BUILD_DIR}/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.exe"

# libxml2 (DAWproject XSD validation) is a vcpkg dependency on Windows (see
# vcpkg.json). Wire vcpkg in like CI does: point CMake at the vcpkg toolchain
# and force the x64-windows triplet. Without the explicit triplet, manifest-mode
# vcpkg installs the x86 default and find_package(LibXml2) fails for the x64
# build. VCPKG_ROOT must be the native Windows path (e.g. C:/vcpkg) since the
# native cmake.exe can't read an MSYS /c/... path.
VCPKG_ROOT="${VCPKG_ROOT:-C:/vcpkg}"
# Share vcpkg's binary cache with the CI builds on this machine. The self-hosted
# runner uses the same MSVC, so the same vcpkg ABI hash applies and the built
# libxml2 binary is reused: whichever runs first - a CI/release job or a local
# build - warms the cache for the other, so libxml2 is compiled once, not per
# build tree. Override by exporting VCPKG_DEFAULT_BINARY_CACHE beforehand.
export VCPKG_DEFAULT_BINARY_CACHE="${VCPKG_DEFAULT_BINARY_CACHE:-C:/vcpkg-bincache}"
mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"
CMAKE_ARGS=(
  -G Ninja
  -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  -DMAGDA_BUILD_TESTS=ON
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
  -DVCPKG_TARGET_TRIPLET=x64-windows
)

case "${1:-debug}" in
  debug)
    mkdir -p "$BUILD_DIR"
    if [ ! -f "$BUILD_DIR/build.ninja" ]; then
      echo "Configuring..."
      cd "$BUILD_DIR" && cmake "${CMAKE_ARGS[@]}" ..
      cd ..
    fi
    cd "$BUILD_DIR" && ninja
    ;;
  run|run-console)
    bash "$0" debug && "$EXE"
    ;;
  test)
    mkdir -p "$BUILD_DIR"
    if [ ! -f "$BUILD_DIR/build.ninja" ]; then
      cd "$BUILD_DIR" && cmake "${CMAKE_ARGS[@]}" ..
      cd ..
    fi
    cd "$BUILD_DIR" && ninja magda_tests && ./tests/magda_tests.exe
    ;;
  clean)
    rm -rf cmake-build-debug cmake-build-release
    ;;
  configure)
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR" && cmake "${CMAKE_ARGS[@]}" ..
    ;;
  *)
    echo "Usage: bash winbuild.sh [debug|run|test|clean|configure]"
    ;;
esac
