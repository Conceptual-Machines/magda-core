cmake_minimum_required(VERSION 3.20)

project(Magda VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Options
option(MAGDA_BUILD_TESTS "Build tests" ON)
option(MAGDA_BUILD_EXAMPLES "Build examples" ON)
option(MAGDA_BUILD_JUCE_ADAPTER "Build JUCE/Tracktion adapter" OFF)
option(MAGDA_BUILD_GRPC "Build with gRPC support" ON)

# Find packages
find_package(Threads REQUIRED)

# Include FetchContent for dependency management
include(FetchContent)

# Set policy for FetchContent
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# nlohmann/json for JSON handling
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

# websocketpp for WebSocket server (use newer commit with better CMake support)
FetchContent_Declare(
    websocketpp
    GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
    GIT_TAG develop
)

# asio for async I/O (required by websocketpp)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-1
)

# gRPC for gRPC server support
if(MAGDA_BUILD_GRPC)
    FetchContent_Declare(
        gRPC
        GIT_REPOSITORY https://github.com/grpc/grpc
        GIT_TAG v1.60.0
    )
    set(FETCHCONTENT_QUIET OFF)
endif()

# Catch2 for testing
if(MAGDA_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.4.0
    )
endif()

# Set CMAKE policy for websocketpp before making it available
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

FetchContent_MakeAvailable(nlohmann_json asio)

if(MAGDA_BUILD_GRPC)
    FetchContent_MakeAvailable(gRPC)
    
    # Find required protobuf components
    find_package(Protobuf CONFIG REQUIRED)
    find_package(gRPC CONFIG REQUIRED)
endif()

# Handle websocketpp separately to avoid CMake issues
FetchContent_GetProperties(websocketpp)
if(NOT websocketpp_POPULATED)
    FetchContent_Populate(websocketpp)
    # Don't add the websocketpp subdirectory as it has CMake issues
    # We'll just use it as header-only
endif()

if(MAGDA_BUILD_TESTS)
    FetchContent_MakeAvailable(Catch2)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${asio_SOURCE_DIR}/asio/include)
include_directories(${websocketpp_SOURCE_DIR})

# Core library
add_subdirectory(src)

# Examples
if(MAGDA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests
if(MAGDA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# JUCE adapter (optional)
if(MAGDA_BUILD_JUCE_ADAPTER)
    add_subdirectory(adapters/juce)
endif()

# Install rules
install(DIRECTORY include/ DESTINATION include)
install(TARGETS magda_core
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
) 