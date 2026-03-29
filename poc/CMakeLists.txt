add_executable(llm_prompt_compare llm_prompt_compare.cpp)
target_link_libraries(llm_prompt_compare PRIVATE magda_agents)
set_target_properties(llm_prompt_compare PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
