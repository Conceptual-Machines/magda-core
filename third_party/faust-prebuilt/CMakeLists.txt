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

add_executable(faust IMPORTED GLOBAL)
set_target_properties(faust PROPERTIES IMPORTED_LOCATION "${_faust_exe}")

unset(_faust_lib)
unset(_faust_exe)
