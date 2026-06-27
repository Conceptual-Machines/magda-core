# DAW Domain - Digital Audio Workstation Core + UI
# This file declares the magda_daw library, the magda_daw_app GUI target,
# and the magda_plugin_scanner helper. Per-directory CMakeLists.txt files
# under this tree contribute their own sources via target_sources().

# Generate version header from project version
configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp" @ONLY)

# Safety net: detect git-lfs pointer files before juce_add_binary_data embeds
# them. Without this, a clone without git-lfs would build successfully but
# silently ship a ~130-byte pointer in place of the real font, and CJK text
# would render as tofu at runtime.
set(LFS_TRACKED_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/fonts/NotoSansCJKsc-Regular.otf"
)
foreach(LFS_FILE ${LFS_TRACKED_FILES})
    if(EXISTS "${LFS_FILE}")
        file(READ "${LFS_FILE}" _lfs_head LIMIT 64)
        if(_lfs_head MATCHES "^version https://git-lfs\\.github\\.com/spec/")
            message(FATAL_ERROR
                "\n"
                "Git LFS file not fetched: ${LFS_FILE}\n"
                "This file is tracked by git-lfs but was checked out as a pointer.\n"
                "Install git-lfs and pull the real content:\n"
                "    git lfs install\n"
                "    git lfs pull\n")
        endif()
    endif()
endforeach()

# Create binary data for fonts and icons
juce_add_binary_data(MagdaAssets
    SOURCES
    # Fonts
    ../../assets/fonts/Inter-Regular.ttf
    ../../assets/fonts/Inter-Medium.ttf
    ../../assets/fonts/Inter-SemiBold.ttf
    ../../assets/fonts/Inter-Bold.ttf
    "../../assets/fonts/Microgramma D Extended Bold.otf"
    ../../assets/fonts/JetBrainsMono-Regular.ttf
    # CJK fallback — covers Simplified/Traditional Chinese, Japanese, Korean
    ../../assets/fonts/NotoSansCJKsc-Regular.otf
    # General UI icons
    ../../assets/icons/general/enter.svg
    ../../assets/icons/general/track.svg
    ../../assets/icons/general/clip.svg
    ../../assets/icons/general/copy.svg
    ../../assets/icons/general/copycontent.svg
    ../../assets/icons/general/delete.svg
    ../../assets/icons/general/random.svg
    ../../assets/icons/general/compare.svg
    ../../assets/icons/general/add.svg
    ../../assets/icons/general/settings_nobg.svg
    ../../assets/icons/general/menu.svg
    ../../assets/icons/general/funnel.svg
    ../../assets/icons/general/learn.svg
    ../../assets/icons/general/refresh.svg
    ../../assets/icons/general/send.svg
    ../../assets/icons/general/chevron_left.svg
    ../../assets/icons/general/chevron_right.svg
    ../../assets/icons/general/chevron_down.svg
    ../../assets/icons/general/bar_chart.svg
    ../../assets/icons/general/link_flat.svg
    ../../assets/icons/general/play_bare.svg
    ../../assets/icons/general/server_play.svg
    ../../assets/icons/general/server_stop.svg
    ../../assets/icons/general/open_in_new.svg
    ../../assets/icons/general/power_on.svg
    ../../assets/icons/general/input-output.svg
    ../../assets/icons/general/save.svg
    ../../assets/icons/general/folder-open.svg
    ../../assets/icons/general/piano_roll.svg
    ../../assets/icons/general/drum_grid.svg
    ../../assets/icons/general/faust-text-logo.svg
    ../../assets/icons/general/script.svg
    ../../assets/icons/general/speaker.svg
    ../../assets/icons/general/speaker_simple.svg
    ../../assets/icons/general/speaker_muted.svg
    ../../assets/icons/general/speaker_on.svg
    ../../assets/icons/general/speaker_off.svg
    ../../assets/icons/general/master_on.svg
    ../../assets/icons/general/master_off.svg
    ../../assets/icons/general/master_off_1.svg
    ../../assets/icons/general/chord_on.svg
    ../../assets/icons/general/chord_off.svg
    ../../assets/icons/general/chord_on_1.svg
    ../../assets/icons/general/solo_off.svg
    ../../assets/icons/general/solo_on.svg
    ../../assets/icons/general/track_record_off.svg
    ../../assets/icons/general/track_record_on.svg
    ../../assets/icons/general/monitor_off.svg
    ../../assets/icons/general/monitor_on.svg
    ../../assets/BoldMGlyph.svg
    ../../assets/icons/general/headphones.svg
    ../../assets/icons/general/record-monitor.svg
    ../../assets/icons/general/metronome.svg
    ../../assets/icons/general/project_home.svg
    ../../assets/icons/general/harddrive.svg
    ../../assets/icons/general/database.svg
    ../../assets/icons/general/favorite.svg
    # MIDI editor icons
    ../../assets/icons/midi_editor/icon-velocity-boldm.svg
    ../../assets/icons/midi_editor/icon-cc-boldm.svg
    ../../assets/icons/midi_editor/icon-mpe-boldm.svg
    ../../assets/icons/midi_editor/icon-chord-boldm.svg
    ../../assets/icons/midi_editor/icon-fold-boldm.svg
    ../../assets/icons/midi_editor/lanes.svg
    ../../assets/icons/midi_editor/stacks.svg
    # Track chain icons
    ../../assets/icons/trackchain/icon-mods-boldm.svg
    ../../assets/icons/trackchain/icon-racks-boldm.svg
    ../../assets/icons/trackchain/icon-treeview-boldm.svg
    ../../assets/icons/trackchain/icon-presets-round-boldm.svg
    ../../assets/icons/trackchain/icon-sends-boldm.svg
    ../../assets/icons/trackchain/icon-inserts-boldm.svg
    ../../assets/icons/trackchain/oscilloscope3.svg
    ../../assets/icons/trackchain/icon-spectrum-boldm.svg
    ../../assets/icons/trackchain/icon-levels-boldm.svg
    ../../assets/icons/trackchain/icon-checkmix-boldm.svg
    ../../assets/icons/trackchain/devices/audio-device.svg
    ../../assets/icons/trackchain/devices/midi-device.svg
    ../../assets/icons/trackchain/devices/instrument-device.svg
    ../../assets/icons/trackchain/gain-staging.svg
    # Transport icons
    ../../assets/icons/transport/play_off.svg
    ../../assets/icons/transport/play_on.svg
    ../../assets/icons/transport/pause_off.svg
    ../../assets/icons/transport/pause_on.svg
    ../../assets/icons/transport/stop_off.svg
    ../../assets/icons/transport/stop_on.svg
    ../../assets/icons/transport/record_off.svg
    ../../assets/icons/transport/record_on.svg
    ../../assets/icons/transport/record_circle.svg
    ../../assets/icons/transport/loop_off.svg
    ../../assets/icons/transport/loop_on.svg
    ../../assets/icons/transport/loop_icon.svg
    ../../assets/icons/transport/rewind_off.svg
    ../../assets/icons/transport/rewind_on.svg
    ../../assets/icons/transport/prev_off.svg
    ../../assets/icons/transport/prev_on.svg
    ../../assets/icons/transport/next_off.svg
    ../../assets/icons/transport/next_on.svg
    ../../assets/icons/transport/punchin.svg
    ../../assets/icons/transport/punchin_on.svg
    ../../assets/icons/transport/punchout.svg
    ../../assets/icons/transport/punchout_on.svg
    ../../assets/icons/transport/automation_off.svg
    ../../assets/icons/transport/automation_on.svg
    ../../assets/icons/transport/midi_qwerty_off.svg
    ../../assets/icons/transport/midi_qwerty_on.svg
    # Inspector icons
    ../../assets/icons/inspector/infinito.svg
    ../../assets/icons/inspector/position.svg
    ../../assets/icons/inspector/clip_loop_off.svg
    ../../assets/icons/inspector/clip_loop_on.svg
    ../../assets/icons/inspector/audio_clip.svg
    ../../assets/icons/inspector/midi_clip.svg
    ../../assets/icons/inspector/fade_linear.svg
    ../../assets/icons/inspector/fade_convex.svg
    ../../assets/icons/inspector/fade_concave.svg
    ../../assets/icons/inspector/fade_scurve.svg
    ../../assets/icons/inspector/fade_gain.svg
    ../../assets/icons/inspector/fade_speedramp.svg
    # Resume/arrangement icons
    ../../assets/icons/resume/resume.svg
    ../../assets/icons/resume/resume_on.svg
    # Automation icons
    ../../assets/icons/automation/horizontal_snap.svg
    ../../assets/icons/automation/vertical_snap.svg
    ../../assets/icons/automation/automation.svg
    ../../assets/icons/automation/automation_master_header.svg
    # Waveform icons
    ../../assets/icons/waveform/sine-wave.svg
    ../../assets/icons/waveform/time_bend_off.svg
    ../../assets/icons/waveform/time_bend_on.svg
    ../../assets/icons/waveform/note_slice_off.svg
    ../../assets/icons/waveform/note_slice_on.svg
    ../../assets/icons/waveform/curve.svg
    # Analysis device icons
    ../../assets/icons/analysis/postfx.svg
    ../../assets/icons/analysis/analysis2.svg
    # Toolbar icons
    ../../assets/icons/toolbar/density_small.svg
    ../../assets/icons/toolbar/density_medium.svg
    ../../assets/icons/toolbar/density_large.svg
    ../../assets/icons/toolbar/zoom_out_map.svg
    ../../assets/icons/toolbar/fit_width.svg
    ../../assets/icons/toolbar/fit_loop.svg
    ../../assets/icons/toolbar/horizontal.svg
    ../../assets/icons/toolbar/vertical.svg
    ../../assets/icons/toolbar/io_routing.svg
    ../../assets/icons/toolbar/location.svg
    ../../assets/icons/toolbar/clock.svg
    # Footer view mode icons
    ../../assets/icons/footer/icon-session-boldm.svg
    ../../assets/icons/footer/icon-arrangement-boldm.svg
    ../../assets/icons/footer/icon-mix-boldm.svg
    # Legacy view mode icons (still used by the clip inspector view toggle)
    ../../assets/icons/footer/Session.svg
    ../../assets/icons/footer/Arrangement.svg
    # Sidebar icons
    ../../assets/icons/sidebar/plug.svg
    # Browser icons
    ../../assets/icons/browser/browser.svg
    ../../assets/midi.svg
    ../../assets/icons/browser/icon-audio-boldm.svg
    ../../assets/icons/browser/icon-midi-boldm.svg
    ../../assets/icons/browser/icon-preset-boldm.svg
    ../../assets/icons/browser/icon-chordtrack-boldm.svg
    # Mixer fader icons
    ../../assets/icons/fader/fader_thumb.svg
    ../../assets/icons/fader/fader_track.svg
    # Mixer knob icons
    ../../assets/icons/knob/knob.svg
    ../../assets/icons/knob/knob_body.svg
    ../../assets/icons/knob/knob_pointer.svg
    # I/O routing icons
    ../../assets/icons/IO/Input.svg
    ../../assets/icons/IO/Output.svg
    ../../assets/icons/IO/multiout.svg
    # Panel open/close icons
    ../../assets/icons/panels/left_open.svg
    ../../assets/icons/panels/left_close.svg
    ../../assets/icons/panels/right_open.svg
    ../../assets/icons/panels/right_close.svg
    ../../assets/icons/panels/bottom_open.svg
    ../../assets/icons/panels/bottom_close.svg
    ../../assets/icons/panels/enter_fs.svg
    ../../assets/icons/panels/exit_fs.svg
    ../../assets/icons/panels/collapse.svg
    ../../assets/icons/panels/expand.svg
    # Panel tab icons (BoldM)
    ../../assets/icons/panels/icon-browser-boldm.svg
    ../../assets/icons/panels/icon-console-boldm.svg
    ../../assets/icons/panels/icon-inspector-boldm.svg
    ../../assets/icons/panels/icon-plugins-boldm.svg

    # Fontaudio waveform icons
    ../../assets/icons/fontaudio/fad-modsine.svg
    ../../assets/icons/fontaudio/fad-modtri.svg
    ../../assets/icons/fontaudio/fad-modsawup.svg
    ../../assets/icons/fontaudio/fad-modsquare.svg
    ../../assets/icons/fontaudio/fad-modrandom.svg
    # Fontaudio filter icons
    ../../assets/icons/fontaudio/fad-filter-bypass.svg
    ../../assets/icons/fontaudio/fad-filter-lowpass.svg
    ../../assets/icons/fontaudio/fad-filter-highpass.svg
    ../../assets/icons/fontaudio/fad-filter-bandpass.svg
    ../../assets/icons/fontaudio/fad-filter-notch.svg
    # Fontaudio logo icons
    ../../assets/icons/fontaudio/fad-logo-tracktion.svg
    ../../assets/icons/fontaudio/fad-logo-juce.svg
    # AI icons
    ../../assets/icons/ai/psychology.svg
    ../../assets/icons/ai/ai.svg
    # Session view icons
    ../../assets/icons/session/hide.svg
    ../../assets/icons/session/rate.svg
    # Cloud provider icons
    ../../assets/icons/cloud_providers/openai.svg
    ../../assets/icons/cloud_providers/anthropic.svg
    ../../assets/icons/cloud_providers/gemini.svg
    ../../assets/icons/cloud_providers/deepseek.svg
    ../../assets/icons/cloud_providers/openrouter.svg
    # App logo
    ../../assets/magda-lisa.svg
    ../../assets/conceptual-machines-badge.svg
    # Curated alias dictionaries (JSON path is reserved for third-party
    # plugin packs; MAGDA-internal plugin aliases live in
    # core/aliases/InternalPluginAliases.cpp).
    ../../assets/aliases/curated_index.json
    # Bundled Faust starter DSPs — embedded so users always have something
    # in the loader picker even on a fresh install.
    audio/faust_dsp/magda_drive.dsp
    audio/faust_dsp/magda_tremolo.dsp
    audio/faust_dsp/magda_delay.dsp
    audio/faust_dsp/magda_granular_delay.dsp
    audio/faust_dsp/magda_filter_svf.dsp
    audio/faust_dsp/magda_filter_ladder.dsp
    audio/faust_dsp/magda_filter_korg35.dsp
    audio/faust_dsp/magda_filter_oberheim.dsp
    audio/faust_dsp/magda_filter_sk.dsp
    audio/faust_dsp/magda_filter_diode.dsp
)

# =============================================================================
# Targets
# =============================================================================

# Core library — populated by per-subdir CMakeLists.txt below.
add_library(magda_daw STATIC
    command.cpp
    magda.cpp
    command.hpp
    magda.hpp
)

# JUCE GUI app — sources contributed below via add_subdirectory().
juce_add_gui_app(magda_daw_app
    VERSION "${MAGDA_BUNDLE_VERSION}"
    COMPANY_NAME "Conceptual Machines"
    PRODUCT_NAME "MAGDA"
    # Pin the bundle identifier so renaming COMPANY_NAME does not change it.
    # JUCE derives BUNDLE_ID from COMPANY_NAME + PRODUCT_NAME by default; without
    # this pin it would become com.ConceptualMachines.MAGDA, relocating user
    # settings (the OS keys prefs/permissions off the bundle ID) and changing the
    # macOS signing identity. Keep the original com.MAGDA.MAGDA.
    BUNDLE_ID "com.MAGDA.MAGDA"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/app_icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/app_icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "MAGDA needs microphone access to record audio."
    PLIST_TO_MERGE
    "<plist version=\"1.0\"><dict>
        <key>NSDocumentsFolderUsageDescription</key>
        <string>MAGDA needs access to your Documents folder to load and save projects.</string>
        <key>NSDesktopFolderUsageDescription</key>
        <string>MAGDA needs access to your Desktop to load and save projects.</string>
        <key>NSDownloadsFolderUsageDescription</key>
        <string>MAGDA needs access to your Downloads folder to import audio files.</string>
        <key>NSRemovableVolumesUsageDescription</key>
        <string>MAGDA needs access to external drives to load and save projects and audio files.</string>
        <key>CFBundleDocumentTypes</key>
        <array>
            <dict>
                <key>CFBundleTypeName</key>
                <string>MAGDA Project</string>
                <key>CFBundleTypeRole</key>
                <string>Editor</string>
                <key>LSHandlerRank</key>
                <string>Owner</string>
                <key>CFBundleTypeExtensions</key>
                <array>
                    <string>mgd</string>
                </array>
                <key>CFBundleTypeIconFile</key>
                <string>mgd_doc_icon</string>
            </dict>
        </array>
    </dict></plist>"
)

target_sources(magda_daw_app PRIVATE
    magda_daw_main.cpp
)

# Bundle the .mgd document icon into the app's Resources so Finder shows it for
# project files (referenced by CFBundleTypeIconFile in the plist above).
if(APPLE)
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/mgd_doc_icon.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(magda_daw_app PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/mgd_doc_icon.icns")
endif()

# Out-of-process plugin scanner.
juce_add_console_app(magda_plugin_scanner
    VERSION "${MAGDA_BUNDLE_VERSION}"
    COMPANY_NAME "Conceptual Machines"
    PRODUCT_NAME "MAGDA Plugin Scanner"
    # Pin the bundle identifier so the COMPANY_NAME rename does not change it
    # (see magda_daw_app above). Keep the original com.MAGDA.MAGDAPluginScanner.
    BUNDLE_ID "com.MAGDA.MAGDAPluginScanner"
)

target_sources(magda_plugin_scanner PRIVATE
    engine/plugin_scanner_main.cpp
)

# =============================================================================
# Per-subdirectory sources
# =============================================================================

add_subdirectory(core)
add_subdirectory(audio)
add_subdirectory(engine)
add_subdirectory(project)
add_subdirectory(music)
add_subdirectory(api)
add_subdirectory(interfaces)
add_subdirectory(profiling)
add_subdirectory(ui)
add_subdirectory(media_db)
add_subdirectory(transcription)

# =============================================================================
# Linking, compile defs, includes
# =============================================================================

# Auto-detect Steinberg ASIO SDK for Windows ASIO support
if(WIN32)
    find_path(ASIO_SDK_DIR
        NAMES "common/iasiodrv.h"
        PATHS
            "${CMAKE_SOURCE_DIR}/third_party/asiosdk"
            "${CMAKE_SOURCE_DIR}/third_party/asio_sdk"
            "$ENV{ASIO_SDK_DIR}"
        DOC "Path to Steinberg ASIO SDK"
    )
    if(ASIO_SDK_DIR)
        message(STATUS "ASIO SDK found: ${ASIO_SDK_DIR}")
        set(JUCE_ASIO_ENABLED 1)
    else()
        message(STATUS "ASIO SDK not found - ASIO support disabled. "
            "Download from https://www.steinberg.net/asiosdk and place in third_party/asiosdk/")
        set(JUCE_ASIO_ENABLED 0)
    endif()
endif()

target_link_libraries(magda_daw
    PUBLIC
    juce::juce_core
    juce::juce_cryptography  # SHA-256 verification of downloaded model files
    juce::juce_data_structures
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_gui_extra
    tracktion::tracktion_engine
    juce_llm
    MagdaAssets
    magda::mutable  # Mutable Instruments DSP for the native Elements/Rings/Clouds ports (#1581)
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS>
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
    PRIVATE
    LibXml2::LibXml2
    faust::libfaust
)

target_compile_definitions(magda_daw
    PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    MAGDA_DAWPROJECT_SCHEMA_DIR="${CMAKE_SOURCE_DIR}/third_party/dawproject"
    $<$<BOOL:${JUCE_ASIO_ENABLED}>:JUCE_ASIO=1>
)

target_link_libraries(magda_daw_app
    PRIVATE
    magda_daw
    magda_agents
    magda_scripting
    MagdaAssets
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    tracktion::tracktion_engine
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS>
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

target_compile_definitions(magda_daw_app
    PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    $<$<BOOL:${JUCE_ASIO_ENABLED}>:JUCE_ASIO=1>
)

if(APPLE)
    # Do not let hosted plugins bind to MAGDA's statically linked JUCE symbols.
    target_link_options(magda_daw_app PRIVATE "-Wl,-exported_symbol,_main")
endif()

if(JUCE_ASIO_ENABLED)
    target_include_directories(magda_daw PUBLIC
        "${ASIO_SDK_DIR}/common"
        "${ASIO_SDK_DIR}/host"
    )
    target_include_directories(magda_daw_app PRIVATE
        "${ASIO_SDK_DIR}/common"
        "${ASIO_SDK_DIR}/host"
    )
endif()

target_include_directories(magda_daw
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/interfaces
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/project
    ${CMAKE_CURRENT_SOURCE_DIR}/profiling
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/faust/architecture
)

target_include_directories(magda_daw_app
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/themes
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/windows
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/dialogs
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/panels/state
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/panels/content
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/views
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/layout
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/state
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/common
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/timeline
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/tracks
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/clips
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/pianoroll
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/waveform
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/chain
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/automation
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/components/common/curve
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(magda_plugin_scanner
    PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_audio_processors
    $<$<PLATFORM_ID:Linux>:juce::pkgconfig_JUCE_CURL_LINUX_DEPS>
)

target_compile_definitions(magda_plugin_scanner
    PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_WEB_BROWSER=0
)

if(APPLE)
    # Keep scanner exports aligned with the host app.
    target_link_options(magda_plugin_scanner PRIVATE "-Wl,-exported_symbol,_main")
    target_link_libraries(magda_plugin_scanner
        PRIVATE
        "-framework AudioUnit"
        "-framework CoreAudioKit"
    )
endif()

target_include_directories(magda_plugin_scanner
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# On MSVC Release builds, generate a PDB for crash symbolization.
# /Zi is applied here (not globally) so llama.cpp and other third-party targets
# are not affected and don't hit parallel PDB write conflicts when using sccache.
if(MSVC)
    target_compile_options(magda_daw_app PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(magda_daw_app PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
endif()

install(TARGETS magda_daw_app DESTINATION bin)

# =============================================================================
# Post-build resource copies
# =============================================================================

# Copy the plugin scanner executable next to the main app.
if(APPLE)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:magda_plugin_scanner>"
            "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/MacOS/magda_plugin_scanner"
        COMMENT "Copying plugin scanner to app bundle"
    )
elseif(WIN32)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:magda_plugin_scanner>"
            "$<TARGET_FILE_DIR:magda_daw_app>/magda_plugin_scanner.exe"
        COMMENT "Copying plugin scanner alongside app executable"
    )
else()
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:magda_plugin_scanner>"
            "$<TARGET_FILE_DIR:magda_daw_app>/magda_plugin_scanner"
        COMMENT "Copying plugin scanner alongside app executable"
    )
endif()

# Copy lang/ (localization JSON) into the app so StringTable can load it at runtime.
set(MAGDA_LANG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../lang")
if(APPLE)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_LANG_SRC}"
            "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/Resources/lang"
        COMMENT "Copying localization files to app bundle Resources"
    )
else()
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_LANG_SRC}"
            "$<TARGET_FILE_DIR:magda_daw_app>/lang"
        COMMENT "Copying localization files alongside app executable"
    )
endif()

# Copy resources/controllers/ (hardware profile JSON) into the app so
# ControllerProfileRegistry can load profiles at runtime.
set(MAGDA_CONTROLLERS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/controllers")
if(APPLE)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_CONTROLLERS_SRC}"
            "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/Resources/controllers"
        COMMENT "Copying controller profiles to app bundle Resources"
    )
else()
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_CONTROLLERS_SRC}"
            "$<TARGET_FILE_DIR:magda_daw_app>/controllers"
        COMMENT "Copying controller profiles alongside app executable"
    )
endif()

# Copy resources/drumkits/ (stock .mdgk drumkit templates) into the app so
# DrumkitManager can seed the user's Drumkits folder on first launch.
set(MAGDA_DRUMKITS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/drumkits")
if(APPLE)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_DRUMKITS_SRC}"
            "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/Resources/drumkits"
        COMMENT "Copying stock drumkits to app bundle Resources"
    )
else()
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_DRUMKITS_SRC}"
            "$<TARGET_FILE_DIR:magda_daw_app>/drumkits"
        COMMENT "Copying stock drumkits alongside app executable"
    )
endif()

# Copy resources/models/ (the bundled audio-to-MIDI model, basic_pitch.onnx)
# into the app so TranscriptionService::findBundledModel resolves it at runtime.
# Single small file (~230 KB); always staged regardless of MAGDA_HAVE_CLAP so
# the dev-tree fallback and packaging stay uniform.
set(MAGDA_MODELS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../resources/models")
if(APPLE)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_MODELS_SRC}"
            "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/Resources/models"
        COMMENT "Copying bundled models to app bundle Resources"
    )
else()
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_MODELS_SRC}"
            "$<TARGET_FILE_DIR:magda_daw_app>/models"
        COMMENT "Copying bundled models alongside app executable"
    )
endif()

# Copy faustlibraries (stdfaust.lib + dependencies) into the app so libfaust's
# `import("stdfaust.lib")` resolves at runtime. FaustPlugin passes this dir as
# `-I` argv to the interpreter compiler. Submodule path: third_party/faust/libraries.
set(MAGDA_FAUST_LIBS_SRC "${CMAKE_SOURCE_DIR}/third_party/faust/libraries")
if(APPLE)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_FAUST_LIBS_SRC}"
            "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/Resources/faustlibraries"
        COMMENT "Copying Faust standard libraries to app bundle Resources"
    )
else()
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_FAUST_LIBS_SRC}"
            "$<TARGET_FILE_DIR:magda_daw_app>/faustlibraries"
        COMMENT "Copying Faust standard libraries alongside app executable"
    )
endif()

# Copy third_party/dawproject/ (vendored Project.xsd + MetaData.xsd) into the
# app so DawProjectValidator can validate import/export against the schemas at
# runtime. Without this, the build-time MAGDA_DAWPROJECT_SCHEMA_DIR source path
# does not exist on an end-user machine and every validate call fails.
set(MAGDA_DAWPROJECT_SCHEMA_SRC "${CMAKE_SOURCE_DIR}/third_party/dawproject")
if(APPLE)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_DAWPROJECT_SCHEMA_SRC}"
            "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/Resources/dawproject"
        COMMENT "Copying DAWproject schemas to app bundle Resources"
    )
else()
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MAGDA_DAWPROJECT_SCHEMA_SRC}"
            "$<TARGET_FILE_DIR:magda_daw_app>/dawproject"
        COMMENT "Copying DAWproject schemas alongside app executable"
    )
endif()

# Bundle the ONNX Runtime shared library with the app. The linker only bakes
# the FetchContent `_deps/onnxruntime-src/lib/` build-dir path into the binary's
# rpath, which does not exist on an end-user machine, so the distributed app
# must carry the runtime library itself or it aborts at launch ("Library not
# loaded: @rpath/libonnxruntime.1.dylib"). Skipped when MAGDA_HAVE_CLAP is off
# (Intel macOS) since there's no ORT to bundle.
if(MAGDA_HAVE_CLAP)
    if(APPLE)
        # Place the dylib in the bundle's Frameworks/ — the release codesign step
        # already signs everything there — and add an rpath so the loader finds it.
        # The executable references @rpath/libonnxruntime.1.dylib (the lib's own
        # install name), so the bundled file must use that exact name.
        add_custom_command(TARGET magda_daw_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${ONNXRUNTIME_LIB_DIR}/libonnxruntime.dylib"
                "$<TARGET_BUNDLE_CONTENT_DIR:magda_daw_app>/Frameworks/libonnxruntime.1.dylib"
            COMMENT "Bundling ONNX Runtime dylib into app Frameworks"
        )
        set_property(TARGET magda_daw_app APPEND PROPERTY
            BUILD_RPATH "@executable_path/../Frameworks")
    elseif(WIN32)
        # Windows only searches the exe's directory, the cwd, and PATH — none of
        # which include the FetchContent lib dir — so without this copy the first
        # ORT call (Sample Tagger load) faults when the delay-loaded DLL fails to
        # resolve.
        add_custom_command(TARGET magda_daw_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_LIB_DIR}/onnxruntime.dll"
                "${ONNXRUNTIME_LIB_DIR}/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:magda_daw_app>"
            COMMENT "Copying ONNX Runtime DLLs alongside app executable"
        )
    endif()
endif()

# libxml2 (DAWproject XSD validation) comes from vcpkg on Windows and links as a
# DLL. Windows only searches the exe's own directory, so copy libxml2.dll plus
# its transitive runtime deps next to the executable. TARGET_RUNTIME_DLLS
# resolves the full closure from the imported vcpkg targets, so we don't have to
# track the exact dep set (iconv/lzma/zlib/...) by hand. Unconditional on
# Windows: libxml2 is always linked (unlike ONNX, which is gated on CLAP).
if(WIN32)
    add_custom_command(TARGET magda_daw_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_RUNTIME_DLLS:magda_daw_app>"
            "$<TARGET_FILE_DIR:magda_daw_app>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying runtime DLLs (libxml2 + deps) alongside app executable"
    )
endif()
