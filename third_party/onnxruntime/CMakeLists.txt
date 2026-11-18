# Wrap ONNX Runtime's NuGet package as an IMPORTED CMake target. The package has no
# CMake: headers sit under build/native/include and one prebuilt library per platform
# under runtimes/<rid>/native.

set(ORT_ROOT "${onnxruntime_SOURCE_DIR}")
if(APPLE AND (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64"
              OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64"))
    set(ORT_RID osx-arm64)
elseif(UNIX AND NOT APPLE)
    set(ORT_RID linux-x64)
elseif(WIN32)
    set(ORT_RID win-x64)
else()
    message(FATAL_ERROR "ONNX Runtime: no prebuilt binary for this platform (set MAGDA_HAVE_CLAP=OFF to skip)")
endif()

set(ORT_NATIVE "${ORT_ROOT}/runtimes/${ORT_RID}/native")
if(NOT EXISTS "${ORT_NATIVE}")
    message(FATAL_ERROR "onnxruntime package has no ${ORT_RID} binaries under ${ORT_ROOT}")
endif()
message(STATUS "ONNX Runtime: ${ORT_NATIVE}")

# The package ships each library once, but binaries load it by its install name or
# soname (libonnxruntime.1.dylib, libonnxruntime.so.1), which the release archives
# provided alongside. Copies under both names make that directory self-contained.
set(ORT_LIB_DIR "${ORT_NATIVE}")
if(UNIX)
    set(ORT_LIB_DIR "${CMAKE_CURRENT_BINARY_DIR}/lib")
    if(APPLE)
        set(ORT_LIB libonnxruntime.dylib)
        set(ORT_LOADED_AS libonnxruntime.1.dylib)
    else()
        set(ORT_LIB libonnxruntime.so)
        set(ORT_LOADED_AS libonnxruntime.so.1)
    endif()
    file(MAKE_DIRECTORY "${ORT_LIB_DIR}")
    foreach(name IN ITEMS ${ORT_LIB} ${ORT_LOADED_AS})
        file(COPY_FILE "${ORT_NATIVE}/${ORT_LIB}" "${ORT_LIB_DIR}/${name}" ONLY_IF_DIFFERENT)
    endforeach()
endif()

add_library(onnxruntime SHARED IMPORTED GLOBAL)
target_include_directories(onnxruntime SYSTEM INTERFACE "${ORT_ROOT}/build/native/include")

if(WIN32)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ORT_LIB_DIR}/onnxruntime.dll"
        IMPORTED_IMPLIB   "${ORT_LIB_DIR}/onnxruntime.lib")
else()
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ORT_LIB_DIR}/${ORT_LIB}")
endif()

# Stash the lib directory for downstream code that needs to copy the runtime
# shared library next to its executable at install time.
set(ONNXRUNTIME_LIB_DIR "${ORT_LIB_DIR}" CACHE INTERNAL "Path to ORT shared lib dir")
