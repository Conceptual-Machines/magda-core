# Scalar vs vectorized codegen benchmark for the compiled Faust devices
# (issue #1397).
#
# Configures two ways:
#   * standalone  - cmake -S tools/faust_vec_bench -B build-bench
#                   Builds only the Faust compiler and this benchmark. This is
#                   what CI uses; the repository root would drag in JUCE,
#                   Tracktion, llama.cpp and vcpkg for a job that compiles one
#                   executable.
#   * in-tree     - cmake -DMAGDA_BUILD_FAUST_VEC_BENCH=ON ...
#                   Reuses the `faust` target the main build already declares.

cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(magda_faust_vec_bench CXX)
    set(_bench_standalone TRUE)
    get_filename_component(MAGDA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
else()
    set(_bench_standalone FALSE)
    set(MAGDA_ROOT "${CMAKE_SOURCE_DIR}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED MAGDA_FAUST_VECTOR_SIZE)
    set(MAGDA_FAUST_VECTOR_SIZE 4 CACHE STRING "Faust -vs vector size to benchmark")
endif()

if(_bench_standalone)
    # Only the C++ backend and the `faust` driver are needed to emit source.
    # Everything else — libfaust, the runtime backends, OSC/HTTP — is dead
    # weight for this job.
    set(INCLUDE_EXECUTABLE ON CACHE BOOL "" FORCE)
    set(INCLUDE_STATIC OFF CACHE BOOL "" FORCE)
    set(INCLUDE_DYNAMIC OFF CACHE BOOL "" FORCE)
    set(INCLUDE_OSC OFF CACHE BOOL "" FORCE)
    set(INCLUDE_HTTP OFF CACHE BOOL "" FORCE)
    set(INCLUDE_ITP OFF CACHE BOOL "" FORCE)
    set(INCLUDE_EMCC OFF CACHE BOOL "" FORCE)
    set(INCLUDE_WASM_GLUE OFF CACHE BOOL "" FORCE)
    set(INCLUDE_WASMTIME OFF CACHE BOOL "" FORCE)
    set(CPP_BACKEND "COMPILER" CACHE STRING "" FORCE)
    foreach(_off C CMAJOR CSHARP DLANG FIR JAVA JAX JULIA JSFX OLDCPP RUST
                 TEMPLATE VHDL INTERP INTERP_COMP WASM LLVM)
        set(${_off}_BACKEND OFF CACHE STRING "" FORCE)
    endforeach()

    add_subdirectory("${MAGDA_ROOT}/third_party/faust/build" faust EXCLUDE_FROM_ALL)

    if(MSVC)
        target_compile_options(faust PRIVATE /wd4244 /wd4267 /wd4305)
        target_compile_definitions(faust PRIVATE _MBCS)
    else()
        target_compile_options(faust PRIVATE -w)
    endif()
endif()

set(_bench_dsp_root "${MAGDA_ROOT}/magda/daw/audio/faust_dsp/compiled")
set(_bench_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_bench_generated_dir}")

# Representative rather than exhaustive: both compressor engines, a recursive
# filter (the shape that vectorizes worst), a feedback reverb and delay, a
# near-memoryless saturator (the shape that vectorizes best), a gate, and a
# polyphonic instrument.
#
# A device must appear here and measure positive on all three platforms before
# it may be given VECTORIZE in magda/daw/audio/CMakeLists.txt.
#
# magda_multiband is deliberately absent: the Multiband device runs
# hand-written C++, so its .dsp is generated but never compiled and measuring
# it would describe code that does not ship.
set(_bench_devices
    "saturator:distortion/magda_saturator"
    "filter_svf:filter/magda_filter_svf"
    "compressor:dynamics/magda_compressor"
    "compressor_glue:dynamics/magda_compressor_glue"
    "gate_expander:dynamics/magda_gate_expander"
    "reverb_hall:reverb/magda_reverb_hall"
    "delay:delay/magda_delay"
    "polysynth:synth/magda_polysynth"
)

set(_bench_generated_sources "")
set(_bench_includes "")
set(_bench_entries "")

foreach(_entry IN LISTS _bench_devices)
    string(REPLACE ":" ";" _parts "${_entry}")
    list(GET _parts 0 _name)
    list(GET _parts 1 _relative_path)

    set(_dsp "${_bench_dsp_root}/${_relative_path}.dsp")
    if(NOT EXISTS "${_dsp}")
        message(FATAL_ERROR "faust_vec_bench: no such DSP: ${_dsp}")
    endif()

    foreach(_variant scalar vec)
        set(_class "Bench_${_name}_${_variant}")
        set(_out "${_bench_generated_dir}/${_name}_${_variant}.cpp")

        if(_variant STREQUAL "vec")
            set(_flags -vec -vs ${MAGDA_FAUST_VECTOR_SIZE})
        else()
            set(_flags "")
        endif()

        add_custom_command(
            OUTPUT "${_out}"
            COMMAND $<TARGET_FILE:faust>
                    -lang cpp
                    -cn ${_class}
                    -I "${MAGDA_ROOT}/third_party/faust/libraries"
                    -single
                    ${_flags}
                    -o "${_out}"
                    "${_dsp}"
            DEPENDS "${_dsp}" $<TARGET_FILE:faust>
            COMMENT "faust vec-bench: ${_name} (${_variant})"
            VERBATIM
        )
        list(APPEND _bench_generated_sources "${_out}")
        string(APPEND _bench_includes "#include \"${_out}\"\n")
    endforeach()

    string(APPEND _bench_entries
           "    MAGDA_VEC_BENCH_ENTRY(${_name}, Bench_${_name}_scalar, Bench_${_name}_vec) \\\n")
endforeach()

# The generated classes are #include'd into the benchmark TU rather than
# compiled separately. Faust emits a self-contained class whose file-scope
# helpers are named after it, so distinct -cn values coexist in one TU, and
# both variants then meet the compiler under identical inlining rules.
set(_bench_manifest "#pragma once\n\n")
string(APPEND _bench_manifest
       "// Generated by tools/faust_vec_bench/CMakeLists.txt. Do not edit.\n\n")
string(APPEND _bench_manifest
       "#define MAGDA_VEC_BENCH_VECTOR_SIZE ${MAGDA_FAUST_VECTOR_SIZE}\n\n")
string(APPEND _bench_manifest "${_bench_includes}\n")
string(APPEND _bench_manifest "#define MAGDA_VEC_BENCH_ENTRIES \\\n${_bench_entries}\n")

file(WRITE "${_bench_generated_dir}/FaustVecBenchGenerated.hpp" "${_bench_manifest}")

# The generated files are inputs to the manifest, not separate TUs. Listing
# them as HEADER_FILE_ONLY sources is what makes the build wait for the
# faust invocations without compiling each one twice.
add_executable(magda_faust_vec_bench FaustVecBench.cpp ${_bench_generated_sources})
set_source_files_properties(${_bench_generated_sources} PROPERTIES HEADER_FILE_ONLY ON)

target_include_directories(magda_faust_vec_bench PRIVATE
    "${_bench_generated_dir}"
    "${MAGDA_ROOT}/third_party/faust/architecture"
)

# A Debug configuration would measure the debug allocator rather than the
# generated code. Force optimization whatever CMAKE_BUILD_TYPE the caller used.
if(MSVC)
    target_compile_options(magda_faust_vec_bench PRIVATE /O2 /DNDEBUG)
else()
    target_compile_options(magda_faust_vec_bench PRIVATE -O3 -DNDEBUG)
endif()
