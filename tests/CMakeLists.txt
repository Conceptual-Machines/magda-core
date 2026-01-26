# Test sources
set(TEST_SOURCES
    test_audio_bridge.cpp
    test_command.cpp
    test_interfaces.cpp
    test_nested_racks.cpp
    test_modulation.cpp
    test_parameter_utils.cpp
    test_plugin_loading.cpp
    test_plugin_window_manager.cpp
)

# Create test executable
add_executable(magda_tests ${TEST_SOURCES})

# Link libraries
target_link_libraries(magda_tests
    PRIVATE
    magda_daw
    Catch2::Catch2WithMain
    juce::juce_core
    juce::juce_gui_basics
)

# Include directories
target_include_directories(magda_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Add the test to CTest
add_test(NAME magda_unit_tests COMMAND magda_tests)

# For Catch2 test discovery (optional - automatically discovers individual test cases)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include(Catch)
    catch_discover_tests(magda_tests)
endif()

# JUCE Unit Test Console Application
juce_add_console_app(magda_juce_tests
    VERSION "0.1.0"
    COMPANY_NAME "MAGDA"
    PRODUCT_NAME "Magica JUCE Tests"
)

# Add JUCE test source
target_sources(magda_juce_tests PRIVATE test_zoom_simple.cpp)

# Link required libraries for JUCE tests
target_link_libraries(magda_juce_tests
    PRIVATE
    juce::juce_core
    juce::juce_events
    # Link curl dependencies on Linux
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

# Include directories for JUCE tests
target_include_directories(magda_juce_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}
)

# Add JUCE test to CTest
add_test(NAME magda_juce_unit_tests COMMAND magda_juce_tests)
