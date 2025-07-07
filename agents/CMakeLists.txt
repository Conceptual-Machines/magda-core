# Agents - Simple in-process AI agent system
# This provides a lightweight agent framework using JUCE threading

set(AGENTS_SOURCES
    agent_interface.hpp
    agent_interface.cpp
    agent_manager.hpp
    agent_manager.cpp
    simple_agent.hpp
    simple_agent.cpp
)

# Create the agents library
add_library(magica_agents STATIC ${AGENTS_SOURCES})

# Link with JUCE for threading and utilities
target_link_libraries(magica_agents
    PUBLIC
    magica_daw
    PRIVATE
    Threads::Threads
)

# Include directories
target_include_directories(magica_agents
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../daw
)

# Set target properties
set_target_properties(magica_agents PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Compiler warnings
if(MSVC)
    target_compile_options(magica_agents PRIVATE /W4)
else()
    target_compile_options(magica_agents PRIVATE -Wall -Wextra -Wpedantic)
endif() 