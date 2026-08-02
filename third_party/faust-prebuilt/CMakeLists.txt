# Wrap a prebuilt Faust artifact (see #1669) as IMPORTED CMake targets. The
# artifact is produced by a normal source build of third_party/faust/build
# (see the fallback branch in the top-level CMakeLists.txt) — its outputs
# already land at fixed, submodule-relative paths, so CI just needs to cache
# that directory across runs and point MAGDA_FAUST_PREBUILT_DIR at it.
#
# Headers are NOT part of the artifact: consumers include Faust's runtime
# headers straight from third_party/faust/architecture, which is already on
# disk via the submodule checkout regardless of build mode.

if(WIN32)
    set(_faust_lib "${MAGDA_FAUST_PREBUILT_DIR}/lib/libfaust.lib")
    set(_faust_exe "${MAGDA_FAUST_PREBUILT_DIR}/bin/faust.exe")
else()
    set(_faust_lib "${MAGDA_FAUST_PREBUILT_DIR}/lib/libfaust.a")
    set(_faust_exe "${MAGDA_FAUST_PREBUILT_DIR}/bin/faust")
endif()

if(NOT EXISTS "${_faust_lib}")
    message(FATAL_ERROR "Faust prebuilt artifact missing static lib: ${_faust_lib}")
endif()
if(NOT EXISTS "${_faust_exe}")
    message(FATAL_ERROR "Faust prebuilt artifact missing compiler executable: ${_faust_exe}")
endif()

add_library(faust::libfaust STATIC IMPORTED GLOBAL)
set_target_properties(faust::libfaust PROPERTIES IMPORTED_LOCATION "${_faust_lib}")

# An artifact built with the wasm backend leaves wasmtime_* undefined, and the
# cached artifact carries only Faust's own bin/ and lib/. Without this the
# first (uncached) run links fine and every later cache hit fails at the final
# link. WASMTIME_LIB and the wasmtime target come from DevicesDependencies.cmake.
if(MAGDA_FAUST_BACKEND STREQUAL "wasm")
    if(NOT TARGET wasmtime)
        message(FATAL_ERROR
            "MAGDA_FAUST_BACKEND=wasm but the wasmtime target is missing; "
            "the prebuilt Faust artifact cannot be linked")
    endif()
    set_property(TARGET faust::libfaust APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES "${WASMTIME_LIB}" wasmtime)
endif()

add_executable(faust IMPORTED GLOBAL)
set_target_properties(faust PROPERTIES IMPORTED_LOCATION "${_faust_exe}")

unset(_faust_lib)
unset(_faust_exe)
