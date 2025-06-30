# Core library sources
set(MAGDA_CORE_SOURCES
    core/command.cpp
    core/mcp_server.cpp
    magda.cpp
)

# Create the core library
add_library(magda_core STATIC ${MAGDA_CORE_SOURCES})

# Link libraries
target_link_libraries(magda_core 
    PRIVATE 
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# Include directories
target_include_directories(magda_core
    PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${websocketpp_SOURCE_DIR}
    ${asio_SOURCE_DIR}/asio/include
)

# Compiler definitions
target_compile_definitions(magda_core
    PRIVATE
    ASIO_STANDALONE
    _WEBSOCKETPP_CPP11_TYPE_TRAITS_
) 