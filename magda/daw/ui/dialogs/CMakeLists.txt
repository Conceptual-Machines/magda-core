target_sources(magda_daw_app PRIVATE
    FourOscConversionPrompt.cpp
    ExportAudioDialog.cpp
    ExportMidiDialog.cpp
    GainStagingDialog.cpp
    ParameterConfigDialog.cpp
    ProjectSettingsDialog.cpp
    TrackManagerDialog.cpp
    AboutDialog.cpp
    SplashScreen.cpp
    ExportAudioDialog.hpp
    ExportMidiDialog.hpp
    GainStagingDialog.hpp
    ParameterConfigDialog.hpp
    ProjectSettingsDialog.hpp
    TrackManagerDialog.hpp
    AboutDialog.hpp
    SplashScreen.hpp
)

add_subdirectory(settings)
