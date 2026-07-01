# Wrap a prebuilt magda_mutable artifact (see #1669) as an IMPORTED CMake
# target. Same rationale as third_party/faust-prebuilt: the static lib is
# produced by a normal source build (see the fallback branch in the
# top-level CMakeLists.txt), which pins its output to a fixed,
# submodule-relative path so CI can cache it across runs.
#
# Headers come straight from third_party/eurorack (the submodule checkout),
# not from the artifact. TEST must stay on the interface: consumers include
# Mutable Instruments headers directly and need the same portable
# (non-Cortex-M4) code path the static lib itself was built with.

if(WIN32)
    set(_mutable_lib "${MAGDA_MUTABLE_PREBUILT_DIR}/lib/magda_mutable.lib")
else()
    set(_mutable_lib "${MAGDA_MUTABLE_PREBUILT_DIR}/lib/libmagda_mutable.a")
endif()

if(NOT EXISTS "${_mutable_lib}")
    message(FATAL_ERROR "Mutable prebuilt artifact missing static lib: ${_mutable_lib}")
endif()

add_library(magda_mutable STATIC IMPORTED GLOBAL)
set_target_properties(magda_mutable PROPERTIES IMPORTED_LOCATION "${_mutable_lib}")
target_include_directories(magda_mutable INTERFACE "${CMAKE_SOURCE_DIR}/third_party/eurorack")
target_compile_definitions(magda_mutable INTERFACE TEST)
add_library(magda::mutable ALIAS magda_mutable)

unset(_mutable_lib)
