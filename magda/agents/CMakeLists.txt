# Agents - Simple in-process AI agent system
# This provides a lightweight agent framework using JUCE threading

set(AGENTS_SOURCES
    agent_interface.hpp
    agent_interface.cpp
    agent_manager.hpp
    agent_manager.cpp
    simple_agent.hpp
    simple_agent.cpp
dsl_grammar.hpp
    dsl_interpreter.hpp
    dsl_interpreter.cpp
    music_helpers.hpp
    compact_parser.hpp
    compact_parser.cpp
    compact_executor.hpp
    compact_executor.cpp
    llm_config_utils.hpp
    llm_presets.hpp
    llm_presets.cpp
    command_agent.hpp
    command_agent.cpp
    music_agent.hpp
    music_agent.cpp
    automation_parser.hpp
    automation_parser.cpp
    automation_executor.hpp
    automation_executor.cpp
    automation_agent.hpp
    automation_agent.cpp
    router_agent.hpp
    router_agent.cpp
    llama_model_manager.hpp
    llama_model_manager.cpp
    llama_local_client.hpp
    llama_local_client.cpp
    llm_client_factory.hpp
    model_downloader.hpp
    model_downloader.cpp
    daw_agent.hpp
    daw_agent.cpp
)

# Create the agents library
add_library(magda_agents STATIC ${AGENTS_SOURCES})

# Link with JUCE for threading and utilities
target_link_libraries(magda_agents
    PUBLIC
    magda_daw
    PRIVATE
    Threads::Threads
    llama
    ggml
)

# Include directories
target_include_directories(magda_agents
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../daw
)

# Set target properties
set_target_properties(magda_agents PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Compiler warnings
if(MSVC)
    target_compile_options(magda_agents PRIVATE /W4)
else()
    target_compile_options(magda_agents PRIVATE -Wall -Wextra -Wpedantic)
endif()
