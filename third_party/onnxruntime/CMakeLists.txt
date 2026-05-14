# Wrap the FetchContent-extracted ONNX Runtime prebuilt binaries as an
# IMPORTED CMake target. We can't add it as a subdirectory because the
# upstream archive is just headers + a precompiled shared lib, no CMake.
#
# FetchContent strips the archive's top-level directory, so include/ and
# lib/ live directly under ${onnxruntime_SOURCE_DIR}.

set(ORT_ROOT "${onnxruntime_SOURCE_DIR}")
if(NOT EXISTS "${ORT_ROOT}/lib")
    message(FATAL_ERROR "onnxruntime archive missing lib/ under ${ORT_ROOT}")
endif()
message(STATUS "ONNX Runtime: ${ORT_ROOT}")

add_library(onnxruntime SHARED IMPORTED GLOBAL)
target_include_directories(onnxruntime SYSTEM INTERFACE "${ORT_ROOT}/include")

if(APPLE)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ORT_ROOT}/lib/libonnxruntime.dylib")
elseif(UNIX)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ORT_ROOT}/lib/libonnxruntime.so")
elseif(WIN32)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ORT_ROOT}/lib/onnxruntime.dll"
        IMPORTED_IMPLIB   "${ORT_ROOT}/lib/onnxruntime.lib")
endif()

# Stash the lib directory for downstream code that needs to copy the runtime
# shared library next to its executable at install time.
set(ONNXRUNTIME_LIB_DIR "${ORT_ROOT}/lib" CACHE INTERNAL "Path to ORT shared lib dir")
