# Audio thread, plugin hosting, and routing layer.
# Most of these compile into magda_daw; AudioEngineOptimizer is app-only.

# Compiled-from-Faust DSP (#1214): build-time .dsp → .cpp generation,
# linked into magda_daw as native plugin code. Each compiled DSP needs
# a corresponding wrapper TU under plugins/compiled/ that #include's the
# generated file. Add new entries by:
#   1. listing the wrapper .cpp in target_sources below
#   2. invoking magda_compile_faust_dsp() to produce the generated .cpp
#   3. adding the generated path to target_sources via OBJECT_DEPENDS
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_filter_svf.dsp"
    MagdaSVFDsp
    MAGDA_SVF_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_filter_ladder.dsp"
    MagdaLadderDsp
    MAGDA_LADDER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_filter_korg35.dsp"
    MagdaKorg35Dsp
    MAGDA_KORG35_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_filter_oberheim.dsp"
    MagdaOberheimDsp
    MAGDA_OBERHEIM_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_filter_sk.dsp"
    MagdaSallenKeyDsp
    MAGDA_SALLEN_KEY_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_filter_diode.dsp"
    MagdaDiodeDsp
    MAGDA_DIODE_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_saturator.dsp"
    MagdaSaturatorDsp
    MAGDA_SATURATOR_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_polysynth.dsp"
    MagdaPolySynthDsp
    MAGDA_POLYSYNTH_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_fm.dsp"
    MagdaFMDsp
    MAGDA_FM_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_kick.dsp"
    MagdaKickDsp
    MAGDA_KICK_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_snare.dsp"
    MagdaSnareDsp
    MAGDA_SNARE_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_clap.dsp"
    MagdaClapDsp
    MAGDA_CLAP_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_hat.dsp"
    MagdaHatDsp
    MAGDA_HAT_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_tom.dsp"
    MagdaTomDsp
    MAGDA_TOM_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_marimba.dsp"
    MagdaMarimbaDsp
    MAGDA_MARIMBA_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_djembe.dsp"
    MagdaDjembeDsp
    MAGDA_DJEMBE_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_bell.dsp"
    MagdaBellDsp
    MAGDA_BELL_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_delay.dsp"
    MagdaDelayDsp
    MAGDA_DELAY_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_granular_delay.dsp"
    MagdaGrainDelayDsp
    MAGDA_GRAIN_DELAY_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_grit.dsp"
    MagdaGritDsp
    MAGDA_GRIT_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_multiband.dsp"
    MagdaMultibandDsp
    MAGDA_MULTIBAND_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_phaser.dsp"
    MagdaPhaserDsp
    MAGDA_PHASER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_compressor.dsp"
    MagdaCompressorDsp
    MAGDA_COMPRESSOR_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_compressor_glue.dsp"
    MagdaCompressorGlueDsp
    MAGDA_COMPRESSOR_GLUE_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_mod.dsp"
    MagdaModDsp
    MAGDA_MOD_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_chorus.dsp"
    MagdaChorusDsp
    MAGDA_CHORUS_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_flanger.dsp"
    MagdaFlangerDsp
    MAGDA_FLANGER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_ring_mod.dsp"
    MagdaRingModDsp
    MAGDA_RING_MOD_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_freq_shift.dsp"
    MagdaFreqShiftDsp
    MAGDA_FREQ_SHIFT_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_gate_expander.dsp"
    MagdaGateExpanderDsp
    MAGDA_GATE_EXPANDER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_clipper.dsp"
    MagdaClipperDsp
    MAGDA_CLIPPER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_reverb_plate.dsp"
    MagdaReverbPlateDsp
    MAGDA_REVERB_PLATE_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_reverb_hall.dsp"
    MagdaReverbHallDsp
    MAGDA_REVERB_HALL_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_reverb_room.dsp"
    MagdaReverbRoomDsp
    MAGDA_REVERB_ROOM_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_dimension_dim.dsp"
    MagdaDimensionDimDsp
    MAGDA_DIMENSION_DIM_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_dimension_haas.dsp"
    MagdaDimensionHaasDsp
    MAGDA_DIMENSION_HAAS_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_dimension_ms.dsp"
    MagdaDimensionMSDsp
    MAGDA_DIMENSION_MS_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_pitch_shifter.dsp"
    MagdaPitchShifterDsp
    MAGDA_PITCH_SHIFTER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_pitch_detuner.dsp"
    MagdaPitchDetunerDsp
    MAGDA_PITCH_DETUNER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_pitch_harmonizer.dsp"
    MagdaPitchHarmonizerDsp
    MAGDA_PITCH_HARMONIZER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/magda_bitcrusher.dsp"
    MagdaBitcrusherDsp
    MAGDA_BITCRUSHER_GENERATED_CPP
)
# A phony target that just depends on every generated DSP .cpp; making
# magda_devices depend on it forces the custom commands to fire before any
# wrapper TU is compiled.
add_custom_target(magda_compiled_dsps
    DEPENDS
        "${MAGDA_SVF_GENERATED_CPP}"
        "${MAGDA_LADDER_GENERATED_CPP}"
        "${MAGDA_KORG35_GENERATED_CPP}"
        "${MAGDA_OBERHEIM_GENERATED_CPP}"
        "${MAGDA_SALLEN_KEY_GENERATED_CPP}"
        "${MAGDA_DIODE_GENERATED_CPP}"
        "${MAGDA_SATURATOR_GENERATED_CPP}"
        "${MAGDA_POLYSYNTH_GENERATED_CPP}"
        "${MAGDA_FM_GENERATED_CPP}"
        "${MAGDA_KICK_GENERATED_CPP}"
        "${MAGDA_SNARE_GENERATED_CPP}"
        "${MAGDA_CLAP_GENERATED_CPP}"
        "${MAGDA_HAT_GENERATED_CPP}"
        "${MAGDA_TOM_GENERATED_CPP}"
        "${MAGDA_MARIMBA_GENERATED_CPP}"
        "${MAGDA_DJEMBE_GENERATED_CPP}"
        "${MAGDA_BELL_GENERATED_CPP}"
        "${MAGDA_DELAY_GENERATED_CPP}"
        "${MAGDA_GRAIN_DELAY_GENERATED_CPP}"
        "${MAGDA_GRIT_GENERATED_CPP}"
        "${MAGDA_MULTIBAND_GENERATED_CPP}"
        "${MAGDA_PHASER_GENERATED_CPP}"
        "${MAGDA_COMPRESSOR_GENERATED_CPP}"
        "${MAGDA_COMPRESSOR_GLUE_GENERATED_CPP}"
        "${MAGDA_MOD_GENERATED_CPP}"
        "${MAGDA_CHORUS_GENERATED_CPP}"
        "${MAGDA_FLANGER_GENERATED_CPP}"
        "${MAGDA_RING_MOD_GENERATED_CPP}"
        "${MAGDA_FREQ_SHIFT_GENERATED_CPP}"
        "${MAGDA_GATE_EXPANDER_GENERATED_CPP}"
        "${MAGDA_CLIPPER_GENERATED_CPP}"
        "${MAGDA_REVERB_PLATE_GENERATED_CPP}"
        "${MAGDA_REVERB_HALL_GENERATED_CPP}"
        "${MAGDA_REVERB_ROOM_GENERATED_CPP}"
        "${MAGDA_DIMENSION_DIM_GENERATED_CPP}"
        "${MAGDA_DIMENSION_HAAS_GENERATED_CPP}"
        "${MAGDA_DIMENSION_MS_GENERATED_CPP}"
        "${MAGDA_PITCH_SHIFTER_GENERATED_CPP}"
        "${MAGDA_PITCH_DETUNER_GENERATED_CPP}"
        "${MAGDA_PITCH_HARMONIZER_GENERATED_CPP}"
        "${MAGDA_BITCRUSHER_GENERATED_CPP}"
)
add_dependencies(magda_devices magda_compiled_dsps)
target_include_directories(magda_devices PRIVATE "${CMAKE_BINARY_DIR}/compiled_dsps")

file(GLOB_RECURSE _magda_device_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/processors/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/processors/*.hpp"
)
file(GLOB _magda_device_dsp_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/*.dsp")
set_source_files_properties(${_magda_device_dsp_sources} PROPERTIES HEADER_FILE_ONLY TRUE)

target_sources(magda_devices PRIVATE
    ${_magda_device_sources}
    ${_magda_device_dsp_sources}
    FaustResources.cpp
    FaustResources.hpp
    analysis/BandSpectrum.cpp
    analysis/BandSpectrum.hpp
)

target_sources(magda_daw PRIVATE
    AudioBridge.cpp
    DeviceParameterDisplayTextProvider.cpp
    DeviceParameterDisplayTextProvider.hpp
    ClipCommands.cpp
    TrackMeasurementManager.cpp
    AudioBridgeMixer.cpp
    AudioBridgeMixer.hpp
    CompService.cpp
    CompService.hpp
    AudioThumbnailManager.cpp
    WaveformPeakCache.cpp
    DeviceMeteringManager.cpp
    analysis/MixAnalysisInput.cpp
    analysis/OfflineMixAnalysis.cpp
    MidiBridge.cpp
    midi/MidiDeviceMatch.cpp
    midi/MidiInputRouter.cpp
    midi/MidiInputRouter.hpp
    sampling/SamplerFileLoader.cpp
    sampling/SamplerFileLoader.hpp
    sidechain/SidechainRoutingManager.cpp
    sidechain/SidechainRoutingManager.hpp
    midi/QwertyMidiKeyboard.cpp
    session/SessionClipScheduler.cpp
    session/SessionClipAudioMonitor.cpp
    session/SessionMonitorPlugin.cpp
    session/SessionRecorder.cpp
    # Phase 1: Pure data managers
    transport/TransportStateManager.cpp
    midi/MidiActivityMonitor.cpp
    params/ParameterManager.cpp
    # Phase 2: Independent features
    PluginWindowBridge.cpp
    WarpMarkerManager.cpp
    # Phase 3: Core controllers
    TrackController.cpp
    plugin_manager/PluginManager.cpp
    plugin_manager/PluginManagerSync.cpp
    plugin_manager/PluginManagerMacros.cpp
    plugin_manager/PluginManagerModifiers.cpp
    plugin_manager/PluginManagerSidechain.cpp
    plugin_manager/PluginManagerMeasurement.cpp
    racks/InstrumentRackManager.cpp
    racks/RackSyncManager.cpp
    modifiers/ModifierSync.cpp
    automation/ControlTargetResolver.cpp
    automation/ControlTargetResolver.hpp
    automation/AutomationPlaybackEngine.cpp
    automation/AutomationRecordingEngine.cpp
    automation/AutomationCurveSimplifier.cpp
    automation/ModulationBaker.cpp
    session/ArrangementClipSyncPlanner.cpp
    session/ClipEngineIdMap.cpp
    session/ClipLaunchQuantization.cpp
    session/ClipSynchronizer.cpp
    session/ClipWarpSynchronizer.cpp
    insert_capture/InsertRenderCaptureService.cpp
    ExternalInsertDeviceEnablement.cpp
    transport/StepClock.cpp
    # Controller routing (issue #1050 -- Phase C)
    controllers/ControllerParamWriter.cpp
    controllers/ControllerRouter.cpp
    # Headers (listed for IDE visibility)
    AudioBridge.hpp
    session/SessionClipScheduler.hpp
    session/SessionClipAudioMonitor.hpp
    session/SessionClipCommandQueue.hpp
    session/SessionClipStateQueue.hpp
    session/SessionMonitorPlugin.hpp
    MeteringBuffer.hpp
    params/ParameterQueue.hpp
    transport/TransportStateManager.hpp
    midi/MidiActivityMonitor.hpp
    params/ParameterManager.hpp
    PluginWindowBridge.hpp
    WarpMarkerManager.hpp
    TrackController.hpp
    plugin_manager/PluginManager.hpp
    racks/InstrumentRackManager.hpp
    racks/RackSyncManager.hpp
    automation/AutomationPlaybackEngine.hpp
    session/ArrangementClipSyncPlanner.hpp
    session/ClipEngineIdMap.hpp
    session/ClipLaunchQuantization.hpp
    session/ClipSynchronizer.hpp
    session/ClipWarpSynchronizer.hpp
    midi/MidiBroadcastBus.hpp
    analysis/AudioTapBuffer.hpp
    transport/StepClock.hpp
)

# Device implementations must receive project state through DeviceServices and
# depend only on the value-type leaf, never upward DAW/application layers.
set(_magda_device_boundary_sources
    ${_magda_device_sources}
    "${CMAKE_CURRENT_SOURCE_DIR}/FaustResources.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/FaustResources.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/BandSpectrum.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/BandSpectrum.hpp"
)
foreach(_magda_device_source_path IN LISTS _magda_device_boundary_sources)
    file(READ "${_magda_device_source_path}" _magda_device_source_text)
    if(_magda_device_source_text MATCHES "(TrackManager|Config)::getInstance[ \t]*\\(")
        message(FATAL_ERROR
            "Device source calls a DAW singleton instead of DeviceServices: "
            "${_magda_device_source_path}")
    endif()

    string(REGEX MATCHALL
        "#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]"
        _magda_device_includes "${_magda_device_source_text}")
    foreach(_magda_device_include IN LISTS _magda_device_includes)
        if(_magda_device_include MATCHES
           "[<\"]([^>\"]*/)?(engine|project|ui)/[^>\"]+[>\"]")
            message(FATAL_ERROR
                "magda_devices source crosses the DAW include boundary: "
                "${_magda_device_source_path}\n${_magda_device_include}")
        endif()

        if(_magda_device_include MATCHES "[<\"]([^>\"]*/)?core/([^>\"]+)[>\"]")
            set(_magda_device_core_header "${CMAKE_MATCH_2}")
            if(NOT _magda_device_core_header MATCHES
               "^(TypeIds|TechnicalText|ChainNodePath|ControlTarget|ParameterInfo|ParameterUtils|DeviceInfo|KitRow|MacroInfo|ModInfo|TempoUtils)\\.hpp$")
                message(FATAL_ERROR
                    "magda_devices source includes a non-value core header: "
                    "${_magda_device_source_path}\n${_magda_device_include}")
            endif()
        endif()
    endforeach()
endforeach()
unset(_magda_device_core_header)
unset(_magda_device_include)
unset(_magda_device_includes)
unset(_magda_device_source_text)
unset(_magda_device_source_path)
unset(_magda_device_boundary_sources)

target_sources(magda_daw_app PRIVATE
    AudioEngineOptimizer.cpp
    AudioEngineOptimizer.hpp
)
