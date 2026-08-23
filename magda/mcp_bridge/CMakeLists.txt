# magda-mcp — stdio-to-HTTP bridge for the MCP endpoint (issue #1858).
#
# Deliberately does NOT link magda_daw. This is a proxy: it parses just enough of
# each message to route it and never touches a model object, so linking the DAW
# would trade a ~1MB tool for a ~200MB one and put Tracktion Engine on the
# critical path of a build that only needs JSON and an HTTP client.
#
# The one thing it does borrow is the data-directory resolution, which it
# reimplements in ~15 lines rather than link for. See the comment on dataDir()
# in main.cpp.
juce_add_console_app(magda_mcp_bridge
    VERSION "${MAGDA_BUNDLE_VERSION}"
    COMPANY_NAME "Conceptual Machines"
    PRODUCT_NAME "magda-mcp"
    BUNDLE_ID "com.MAGDA.MAGDAMCPBridge"
)

target_sources(magda_mcp_bridge PRIVATE main.cpp)

# The command an MCP host is configured with. juce_add_console_app names the
# artefact after the target, and "magda_mcp_bridge" is an internal name — what
# goes in a user's .mcp.json should read like a tool.
set_target_properties(magda_mcp_bridge PROPERTIES OUTPUT_NAME "magda-mcp")

target_compile_definitions(magda_mcp_bridge
    PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
)

target_link_libraries(magda_mcp_bridge
    PRIVATE
    juce::juce_core
    httplib::httplib
)

if(APPLE)
    target_link_options(magda_mcp_bridge PRIVATE "-Wl,-exported_symbol,_main")
endif()
