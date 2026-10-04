target_sources(magda_daw PRIVATE
    ActiveTheme.cpp
    ThemeSerialization.cpp
    UserTheme.cpp
    ThemePrompt.cpp
    SdkColourRoles.cpp
)

# Display lists from SDK UI cores draw through juce::Graphics (#2941).
magda_sdk_target_juce_display(magda_daw)

target_sources(magda_daw_app PRIVATE
    FontManager.cpp
    CursorManager.cpp
    MixerLookAndFeel.cpp
    StyledText.cpp
    ActiveTheme.hpp
    ThemeSerialization.hpp
    UserTheme.hpp
    ThemeFileWatcher.hpp
    ThemePrompt.hpp
    SdkColourRoles.hpp
    FontManager.hpp
    LocalizedText.hpp
    CursorManager.hpp
    MixerLookAndFeel.hpp
    StyledText.hpp
)
