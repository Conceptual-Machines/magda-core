# Wrap the FetchContent-extracted wasmtime C API prebuilt as an IMPORTED
# CMake target. Like ONNX Runtime, the upstream archive is just headers plus
# precompiled libs with no CMake of its own.
#
# We take the STATIC library so nothing extra has to be staged next to the
# executable at install time (see project_exe_adjacent_resources_packaging).
#
# FetchContent strips the archive's top-level directory, so include/ and lib/
# live directly under ${wasmtime_SOURCE_DIR}.

set(WASMTIME_ROOT "${wasmtime_SOURCE_DIR}")
if(NOT EXISTS "${WASMTIME_ROOT}/lib")
    message(FATAL_ERROR "wasmtime archive missing lib/ under ${WASMTIME_ROOT}")
endif()
message(STATUS "wasmtime C API: ${WASMTIME_ROOT}")

if(WIN32)
    set(_wasmtime_static "${WASMTIME_ROOT}/lib/wasmtime.lib")
else()
    set(_wasmtime_static "${WASMTIME_ROOT}/lib/libwasmtime.a")
endif()

if(NOT EXISTS "${_wasmtime_static}")
    message(FATAL_ERROR "wasmtime static library not found at ${_wasmtime_static}")
endif()

# libfaust links the static library itself (it takes WASMTIME_LIB as a plain
# path), so this target deliberately carries only the headers and the system
# dependencies of the Rust runtime -- adding the archive here as well would
# put it on the link line twice.
add_library(wasmtime INTERFACE)
target_include_directories(wasmtime SYSTEM INTERFACE "${WASMTIME_ROOT}/include")

# wasm.h declares the whole C API __declspec(dllimport) on MSVC unless
# LIBWASM_STATIC is defined. We link the static library, so without this every
# call resolves to an __imp_ symbol that does not exist and the Windows link
# fails. Harmless elsewhere: the guard is _WIN32-only.
target_compile_definitions(wasmtime INTERFACE LIBWASM_STATIC)

# Without these the final link fails on undefined symbols rather than at
# configure time.
if(APPLE)
    target_link_libraries(wasmtime INTERFACE "-framework CoreFoundation" "-framework Security")
elseif(UNIX)
    target_link_libraries(wasmtime INTERFACE pthread dl m)
elseif(WIN32)
    target_link_libraries(wasmtime INTERFACE
        ws2_32 advapi32 userenv ntdll shell32 ole32 bcrypt)
endif()

# libfaust's own build wants plain paths (it links ${WASMTIME_LIB} directly and
# includes <wasmtime.h>), so publish those alongside the target.
set(WASMTIME_LIB "${_wasmtime_static}" CACHE INTERNAL "Path to the wasmtime static library")
set(WASMTIME_INCLUDE_DIR "${WASMTIME_ROOT}/include" CACHE INTERNAL "Path to the wasmtime headers")
