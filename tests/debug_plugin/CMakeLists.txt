# A VST3 that fails on request, for reproducing hosting bugs without the plugin that
# showed them (#3155). Built with the tests, never shipped. Faults: README.md.

juce_add_plugin(magda_debug_plugin
    PRODUCT_NAME "MAGDA Debug Plugin"
    COMPANY_NAME "MAGDA"
    PLUGIN_MANUFACTURER_CODE Mgda
    PLUGIN_CODE Dbg1
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(magda_debug_plugin PRIVATE
    DebugPluginFaults.cpp
    DebugPluginProcessor.cpp)

# In the VST3 target itself: from the shared-code library the linker drops the export.
target_sources(magda_debug_plugin_VST3 PRIVATE RefusingFactory.cpp)

# JUCE's factory is renamed so RefusingFactory.cpp can export the one hosts call.
target_compile_definitions(magda_debug_plugin PUBLIC
    GetPluginFactory=JuceGetPluginFactory
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(magda_debug_plugin
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
