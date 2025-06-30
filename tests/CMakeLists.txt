# Test sources
set(TEST_SOURCES
    test_command.cpp
    test_interfaces.cpp
)

# Create test executable
add_executable(magda_tests ${TEST_SOURCES})

# Link libraries
target_link_libraries(magda_tests 
    PRIVATE 
    magda_core
    Catch2::Catch2WithMain
)

# Include directories
target_include_directories(magda_tests
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Add the test to CTest
add_test(NAME magda_unit_tests COMMAND magda_tests)

# For Catch2 test discovery (optional - automatically discovers individual test cases)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include(Catch)
    catch_discover_tests(magda_tests)
endif() 