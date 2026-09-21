# The parity envelope bench (#2082): the app's two engines, headless on a device with no
# hardware behind it, measured on the real-project corpus. Built with tests off, so both engines
# are compiled as they ship; make parity-bench configures that tree.
# docs/development/parity-bench.md has the method.

juce_add_console_app(magda_parity_bench
    VERSION "${MAGDA_BUNDLE_VERSION}"
    COMPANY_NAME "Conceptual Machines"
    PRODUCT_NAME "Magda Parity Bench"
    BUNDLE_ID "com.MAGDA.MagdaParityBench"
)

set(_parity_harness_dir "${CMAKE_CURRENT_SOURCE_DIR}/..")

target_sources(magda_parity_bench PRIVATE
    parity_bench_main.cpp
    EngineRig.cpp
    LatencyProbe.cpp
    ParityMeasure.cpp
    PumpDevice.cpp
    PumpThread.cpp
    ${_parity_harness_dir}/MgdFixture.cpp
    ${_parity_harness_dir}/MgdFixtures.cpp
    ${_parity_harness_dir}/NullDiffMaterial.cpp
    ${_parity_harness_dir}/NullDiffCompare.cpp
    ${_parity_harness_dir}/NullDiffCorpus.cpp
    ${_parity_harness_dir}/NullDiffHostedPlugin.cpp
    ${_parity_harness_dir}/TestConfigRedirect.cpp
)

target_link_libraries(magda_parity_bench PRIVATE
    magda_daw
    magda_engine
    magda_engine_devices
    magda_engine_host
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

target_compile_definitions(magda_parity_bench PRIVATE
    MAGDA_TEST_CORPUS_DIR="${_parity_harness_dir}/corpus"
)

target_include_directories(magda_parity_bench PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/magda/daw
    ${_parity_harness_dir}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# The corpus's Faust project imports the standard library, as the app does.
magda_stage_faust_libraries(magda_parity_bench)

if(WIN32)
    add_custom_command(TARGET magda_parity_bench POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:magda_parity_bench>"
            "$<TARGET_FILE_DIR:magda_parity_bench>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs alongside magda_parity_bench")
    if(MAGDA_HAVE_CLAP)
        add_custom_command(TARGET magda_parity_bench POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_LIB_DIR}/onnxruntime.dll"
                "${ONNXRUNTIME_LIB_DIR}/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:magda_parity_bench>"
            COMMENT "Copying ONNX Runtime DLLs alongside magda_parity_bench")
    endif()
endif()
