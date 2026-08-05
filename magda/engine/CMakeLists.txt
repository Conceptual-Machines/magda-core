# MAGDA native audio engine (epic #1882).
#
# Dark target: nothing in the app links it yet. It is built on every configure
# so it cannot rot, and it is deliberately isolated: the C++ standard library,
# JUCE headers and the MAGDA model layer, nothing else. No Tracktion Engine, no
# UI, no app services. The boundary check below enforces that at configure time
# rather than leaving it to review.

add_library(magda_engine STATIC
    plan/RenderPlan.cpp
    plan/PlanCompiler.cpp
    plan/PlanDiff.cpp
    plan/PlanDump.cpp
    plan/RenderPlan.hpp
    plan/PlanCompiler.hpp
    plan/PlanDiff.hpp
    plan/PlanDump.hpp
    plan/TrackRouting.hpp
    exec/PlanExecutor.cpp
    exec/PlanLayout.cpp
    exec/PlanValues.cpp
    exec/RuntimeStateStore.cpp
    exec/EngineSession.cpp
    exec/PlanExecutor.hpp
    exec/PlanLayout.hpp
    exec/PlanValues.hpp
    exec/PlanBindings.hpp
    exec/EngineDevice.hpp
    exec/RenderContext.hpp
    exec/RuntimeStateStore.hpp
    exec/EngineSession.hpp
)
add_library(magda::engine ALIAS magda_engine)

target_compile_features(magda_engine PUBLIC cxx_std_20)

# magda_types carries the model-layer headers and publishes magda/daw as its
# include root, so engine sources resolve "core/<model>.hpp".
target_link_libraries(magda_engine PUBLIC magda_types)

target_include_directories(magda_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Vendored, pinned, header-only: farbot (MIT). The engine owns its copy so
# that nothing outside third_party/ can break this target.
#
# Standing rule for the engine: nothing real-time-concurrent is hand-rolled.
target_include_directories(magda_engine PUBLIC ${CMAKE_SOURCE_DIR}/third_party/farbot/include)

# Borrow JUCE's headers and defines without linking the modules: the model
# headers name juce::String and juce::Colour, the executor renders into
# juce::AudioBuffer / juce::dsp::AudioBlock and passes juce::MidiBuffer around,
# and those modules' unity sources are already compiled once in magda_daw, so
# the undefined symbols resolve at the final link instead of duplicating those
# objects here. Same arrangement magda_types uses.
#
# PUBLIC rather than PRIVATE: the executor's headers name those types, so
# anything linking magda_engine (the tests, and eventually the app) has to see
# them too.
set(MAGDA_ENGINE_JUCE_MODULES
    juce::juce_core
    juce::juce_graphics
    juce::juce_audio_basics
    juce::juce_audio_formats  # juce_dsp's own dependency, headers only
    juce::juce_dsp)

foreach(_juce_module IN LISTS MAGDA_ENGINE_JUCE_MODULES)
    target_include_directories(magda_engine PUBLIC
        $<TARGET_PROPERTY:${_juce_module},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(magda_engine PUBLIC
        $<TARGET_PROPERTY:${_juce_module},INTERFACE_COMPILE_DEFINITIONS>)
endforeach()

# --- Dependency boundary check -----------------------------------------------
#
# Angle-bracket includes may name the standard library or JUCE, never Tracktion
# Engine. Quoted includes may only reach the model layer ("core/...") or the
# engine's own headers.

set(MAGDA_ENGINE_ALLOWED_QUOTED_PREFIXES "core/" "plan/" "exec/")

file(GLOB_RECURSE MAGDA_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

foreach(_engine_source ${MAGDA_ENGINE_SOURCES})
    file(RELATIVE_PATH _engine_relative "${CMAKE_SOURCE_DIR}" "${_engine_source}")
    file(STRINGS "${_engine_source}" _engine_includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]")

    foreach(_include_line ${_engine_includes})
        string(REGEX REPLACE "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"].*$" "\\1"
            _included_header "${_include_line}")

        if(_included_header MATCHES "^tracktion")
            message(FATAL_ERROR
                "\n"
                "Native engine boundary violation in ${_engine_relative}:\n"
                "    includes \"${_included_header}\"\n"
                "The engine is built to replace Tracktion Engine and must never depend on it.\n")
        endif()

        if(_include_line MATCHES "#[ \t]*include[ \t]*\"")
            set(_include_allowed FALSE)
            foreach(_allowed_prefix ${MAGDA_ENGINE_ALLOWED_QUOTED_PREFIXES})
                if(_included_header MATCHES "^${_allowed_prefix}")
                    set(_include_allowed TRUE)
                endif()
            endforeach()

            if(NOT _include_allowed)
                message(FATAL_ERROR
                    "\n"
                    "Native engine boundary violation in ${_engine_relative}:\n"
                    "    includes \"${_included_header}\"\n"
                    "The engine may only include the model layer (core/) and its own headers.\n")
            endif()
        endif()
    endforeach()
endforeach()
