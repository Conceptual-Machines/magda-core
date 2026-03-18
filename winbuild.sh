#!/bin/bash
# Windows build helper - sets up MSVC environment and builds

MSVC_VER="14.50.35717"
VS_ROOT="C:/Program Files/Microsoft Visual Studio/18/Community"
WINSDK_VER="10.0.26100.0"
WINSDK_ROOT="C:/Program Files (x86)/Windows Kits/10"
# Use 8.3 short path for PATH entries (bash can't handle parentheses in PATH)
WINSDK_ROOT_SHORT="/c/PROGRA~2/Windows Kits/10"

export INCLUDE="${VS_ROOT}/VC/Tools/MSVC/${MSVC_VER}/include;${WINSDK_ROOT}/Include/${WINSDK_VER}/ucrt;${WINSDK_ROOT}/Include/${WINSDK_VER}/shared;${WINSDK_ROOT}/Include/${WINSDK_VER}/um;${WINSDK_ROOT}/Include/${WINSDK_VER}/winrt;${WINSDK_ROOT}/Include/${WINSDK_VER}/cppwinrt"
export LIB="${VS_ROOT}/VC/Tools/MSVC/${MSVC_VER}/lib/x64;${WINSDK_ROOT}/Lib/${WINSDK_VER}/ucrt/x64;${WINSDK_ROOT}/Lib/${WINSDK_VER}/um/x64"
export PATH="${PATH}:${WINSDK_ROOT_SHORT}/bin/${WINSDK_VER}/x64"

BUILD_DIR="cmake-build-debug"
EXE="${BUILD_DIR}/magda/daw/magda_daw_app_artefacts/Debug/MAGDA.exe"

case "${1:-debug}" in
  debug)
    mkdir -p "$BUILD_DIR"
    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
      echo "Configuring..."
      cd "$BUILD_DIR" && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..
      cd ..
    fi
    cd "$BUILD_DIR" && ninja
    ;;
  run|run-console)
    bash "$0" debug && "$EXE"
    ;;
  test)
    mkdir -p "$BUILD_DIR"
    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
      cd "$BUILD_DIR" && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..
      cd ..
    fi
    cd "$BUILD_DIR" && ninja magda_tests && ./tests/magda_tests.exe
    ;;
  clean)
    rm -rf cmake-build-debug cmake-build-release
    ;;
  configure)
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR" && cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON ..
    ;;
  *)
    echo "Usage: bash winbuild.sh [debug|run|test|clean|configure]"
    ;;
esac
