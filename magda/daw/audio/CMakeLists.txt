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
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/filter/magda_filter_svf.dsp"
    MagdaSVFDsp
    MAGDA_SVF_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/filter/magda_filter_ladder.dsp"
    MagdaLadderDsp
    MAGDA_LADDER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/filter/magda_filter_korg35.dsp"
    MagdaKorg35Dsp
    MAGDA_KORG35_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/filter/magda_filter_oberheim.dsp"
    MagdaOberheimDsp
    MAGDA_OBERHEIM_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/filter/magda_filter_sk.dsp"
    MagdaSallenKeyDsp
    MAGDA_SALLEN_KEY_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/filter/magda_filter_diode.dsp"
    MagdaDiodeDsp
    MAGDA_DIODE_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/distortion/magda_saturator.dsp"
    MagdaSaturatorDsp
    MAGDA_SATURATOR_GENERATED_CPP
    VECTORIZE
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/synth/magda_polysynth.dsp"
    MagdaPolySynthDsp
    MAGDA_POLYSYNTH_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/synth/magda_fm.dsp"
    MagdaFMDsp
    MAGDA_FM_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/drums/magda_kick.dsp"
    MagdaKickDsp
    MAGDA_KICK_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/drums/magda_snare.dsp"
    MagdaSnareDsp
    MAGDA_SNARE_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/drums/magda_clap.dsp"
    MagdaClapDsp
    MAGDA_CLAP_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/drums/magda_hat.dsp"
    MagdaHatDsp
    MAGDA_HAT_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/drums/magda_tom.dsp"
    MagdaTomDsp
    MAGDA_TOM_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/synth/magda_marimba.dsp"
    MagdaMarimbaDsp
    MAGDA_MARIMBA_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/drums/magda_djembe.dsp"
    MagdaDjembeDsp
    MAGDA_DJEMBE_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/drums/magda_bell.dsp"
    MagdaBellDsp
    MAGDA_BELL_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/delay/magda_delay.dsp"
    MagdaDelayDsp
    MAGDA_DELAY_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/delay/magda_granular_delay.dsp"
    MagdaGrainDelayDsp
    MAGDA_GRAIN_DELAY_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/distortion/magda_grit.dsp"
    MagdaGritDsp
    MAGDA_GRIT_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/modulation/magda_phaser.dsp"
    MagdaPhaserDsp
    MAGDA_PHASER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/dynamics/magda_compressor.dsp"
    MagdaCompressorDsp
    MAGDA_COMPRESSOR_GENERATED_CPP
    VECTORIZE
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/dynamics/magda_compressor_glue.dsp"
    MagdaCompressorGlueDsp
    MAGDA_COMPRESSOR_GLUE_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/modulation/magda_mod.dsp"
    MagdaModDsp
    MAGDA_MOD_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/modulation/magda_chorus.dsp"
    MagdaChorusDsp
    MAGDA_CHORUS_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/modulation/magda_flanger.dsp"
    MagdaFlangerDsp
    MAGDA_FLANGER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/modulation/magda_ring_mod.dsp"
    MagdaRingModDsp
    MAGDA_RING_MOD_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/modulation/magda_freq_shift.dsp"
    MagdaFreqShiftDsp
    MAGDA_FREQ_SHIFT_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/dynamics/magda_gate_expander.dsp"
    MagdaGateExpanderDsp
    MAGDA_GATE_EXPANDER_GENERATED_CPP
    VECTORIZE
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/distortion/magda_clipper.dsp"
    MagdaClipperDsp
    MAGDA_CLIPPER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/reverb/magda_reverb_plate.dsp"
    MagdaReverbPlateDsp
    MAGDA_REVERB_PLATE_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/reverb/magda_reverb_hall.dsp"
    MagdaReverbHallDsp
    MAGDA_REVERB_HALL_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/reverb/magda_reverb_room.dsp"
    MagdaReverbRoomDsp
    MAGDA_REVERB_ROOM_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/stereo/magda_dimension_dim.dsp"
    MagdaDimensionDimDsp
    MAGDA_DIMENSION_DIM_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/stereo/magda_dimension_haas.dsp"
    MagdaDimensionHaasDsp
    MAGDA_DIMENSION_HAAS_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/stereo/magda_dimension_ms.dsp"
    MagdaDimensionMSDsp
    MAGDA_DIMENSION_MS_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/pitch/magda_pitch_shifter.dsp"
    MagdaPitchShifterDsp
    MAGDA_PITCH_SHIFTER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/pitch/magda_pitch_detuner.dsp"
    MagdaPitchDetunerDsp
    MAGDA_PITCH_DETUNER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/pitch/magda_pitch_harmonizer.dsp"
    MagdaPitchHarmonizerDsp
    MAGDA_PITCH_HARMONIZER_GENERATED_CPP
)
magda_compile_faust_dsp(
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/compiled/distortion/magda_bitcrusher.dsp"
    MagdaBitcrusherDsp
    MAGDA_BITCRUSHER_GENERATED_CPP
)
# A phony target that just depends on every generated DSP .cpp; making
# magda_base_devices depend on it forces the custom commands to fire before any
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
add_dependencies(magda_base_devices magda_compiled_dsps)
add_dependencies(magda_tracktion_compat_devices magda_compiled_dsps)
target_include_directories(magda_base_devices PRIVATE "${CMAKE_BINARY_DIR}/compiled_dsps")
target_include_directories(magda_tracktion_compat_devices
    PRIVATE "${CMAKE_BINARY_DIR}/compiled_dsps")

file(GLOB_RECURSE _magda_device_pack_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/processors/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/processors/*.hpp"
)
set(_magda_device_sdk_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceParameterHandle.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DevicePluginHandle.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceServices.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceServices.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceSessionKey.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/IFaustEditorModel.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/InternalPluginRegistry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/InternalPluginRegistry.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/MagdaDevice.hpp"
)
list(REMOVE_ITEM _magda_device_pack_sources ${_magda_device_sdk_sources})

set(_magda_tracktion_device_adapter_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/tracktion/CompiledFaustTracktionAdapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/tracktion/CompiledFaustTracktionAdapter.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/tracktion/TracktionDeviceAdapters.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/tracktion/TracktionDeviceStateBridge.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/tracktion/TracktionDeviceStateBridge.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/tracktion/TracktionInternalPluginAdapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/tracktion/TracktionInternalPluginAdapter.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/tracktion/TracktionMagdaDevicePlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/processors/DeviceProcessorFactory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/processors/DeviceProcessorFactory.hpp"
)
list(REMOVE_ITEM _magda_device_pack_sources ${_magda_tracktion_device_adapter_sources})

# The native engine's half of the same job (#2174): one device implementation,
# hosted by whichever engine is running it. Kept out of the pack sweep above for
# the same reason the Tracktion adapters are -- an adapter is host code, not a
# device -- and out of magda_tracktion_compat_devices as well, since the whole
# point of it is that it names no Tracktion type.
set(_magda_engine_device_adapter_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/EngineDeviceFactory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/EngineDeviceFactory.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/EngineMagdaDevice.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/EngineMagdaDevice.hpp"
)
list(REMOVE_ITEM _magda_device_pack_sources ${_magda_engine_device_adapter_sources})

set(_magda_base_device_support_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/FaustResources.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/FaustResources.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TracktionHelpers.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/AudioTapBuffer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/BandSpectrum.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/BandSpectrum.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/MaskingDetector.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/TrackMeasurer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/insert_capture/CaptureWindowMath.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/midi/MidiBroadcastBus.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/session/SessionMonitorPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/session/SessionMonitorPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/transport/StepClock.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/transport/StepClock.hpp"
)
file(GLOB _magda_device_dsp_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/*.dsp")
set_source_files_properties(${_magda_device_dsp_sources} PROPERTIES HEADER_FILE_ONLY TRUE)

set(_magda_neutral_base_device_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/AudioTapBuffer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/AnalysisTapPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/FaustMetadataParser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/FaustMetadataParser.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/MagdaDevice.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/OscilloscopePlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/OscilloscopePlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SpectrumAnalyzerPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SpectrumAnalyzerPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/CompiledFaustInterface.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/CompiledPluginRegistry.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/MagdaCompiledEffect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/MagdaCompiledEffect.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/MagdaCompiledPolyInstrument.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/MagdaCompiledPolyInstrument.hpp"
)
# Devices that are MagdaDevice implementations: one DSP, hosted by whichever
# engine is running it (#2192). Instruments share MagdaCompiledPolyInstrument,
# effects share MagdaCompiledEffect; both lists are just "who has crossed".
foreach(_magda_neutral_device IN ITEMS
        Bell Clap Djembe FM Hat Kick Marimba PolySynth Snare Tom
        Bitcrusher Chorus Clipper Compressor Delay Dimension Filter Flanger
        FreqShift GateExpander GrainDelay Grit Mod Phaser Pitch Reverb RingMod
        Eq Limiter Multiband Saturator Utility)
    list(APPEND _magda_neutral_base_device_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/Magda${_magda_neutral_device}CompiledPlugin.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/Magda${_magda_neutral_device}CompiledPlugin.hpp"
    )
endforeach()

# TE-native devices and the host's all-device registry aggregation remain host
# compatibility code until their DSP classes are migrated to MagdaDevice.
# Keeping them out of the SDK pack makes the build boundary honest:
# magda_base_devices can be compiled and linked without references to
# compatibility-device symbols, while magda_daw owns every current-engine
# lifecycle adapter and the transitional combined catalog.
set(_magda_legacy_tracktion_device_sources
    ${_magda_device_pack_sources}
    ${_magda_base_device_support_sources}
)
list(REMOVE_ITEM _magda_legacy_tracktion_device_sources
    ${_magda_neutral_base_device_sources})

target_sources(magda_devices PRIVATE
    ${_magda_device_sdk_sources}
)

target_sources(magda_base_devices PRIVATE
    ${_magda_neutral_base_device_sources}
    ${_magda_device_dsp_sources}
)

target_sources(magda_tracktion_compat_devices PRIVATE
    ${_magda_legacy_tracktion_device_sources}
)

# The device layer under the native engine (#2174).
#
# Its own static library rather than sources in magda_daw, because the app does
# not link the engine yet and this is the one piece of host code that has to see
# both sides: the frozen device SDK and the engine's executor. The symmetry with
# magda_tracktion_compat_devices is the point -- each target is one engine's
# lifecycle adapter over the same MagdaDevice.
#
# It reaches findCompiledPluginSpec(), which the compatibility target still
# owns; the declaration is in a neutral header and the definition resolves at
# the final link, the same arrangement magda_engine has with the JUCE modules.
if(MAGDA_BUILD_NATIVE_ENGINE)
    add_library(magda_engine_devices STATIC ${_magda_engine_device_adapter_sources})
    add_library(magda::engine_devices ALIAS magda_engine_devices)

    target_compile_features(magda_engine_devices PRIVATE cxx_std_20)
    target_include_directories(magda_engine_devices PRIVATE ${CMAKE_SOURCE_DIR})
    # magda_engine is added after magda/daw (it links the model layer this
    # directory defines), so this names it before it exists. A plain target name
    # resolves at generate time, which is what makes that legal.
    target_link_libraries(magda_engine_devices
        PUBLIC
        magda_device_sdk
        magda_engine
    )
endif()

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
    session/SessionRecorder.cpp
    # Phase 1: Pure data managers
    transport/TransportStateManager.cpp
    midi/MidiActivityMonitor.cpp
    params/ParameterManager.cpp
    # Phase 2: Independent features
    PluginWindowBridge.cpp
    WarpMarkerManager.cpp
    # Compile-time pins between MAGDA's persisted enums and the engine's (#1887)
    EngineEnumPins.cpp
    # Phase 3: Core controllers
    TrackController.cpp
    plugin_manager/PluginManager.cpp
    plugin_manager/PluginManagerSync.cpp
    plugin_manager/PluginManagerMacros.cpp
    plugin_manager/PluginManagerModifiers.cpp
    plugin_manager/PluginManagerSidechain.cpp
    plugin_manager/PluginManagerMeasurement.cpp
    ${_magda_tracktion_device_adapter_sources}
    racks/InstrumentRackManager.cpp
    racks/RackSyncManager.cpp
    modifiers/ModifierSync.cpp
    automation/AutomationBake.cpp
    automation/AutomationBake.hpp
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
    # Controller routing (issue #1050 -- Phase C)
    controllers/ControllerParamReader.cpp
    controllers/ControllerParamWriter.cpp
    controllers/ControllerRouter.cpp
    # OSC control surfaces (issue #1757)
    osc/OscAddress.cpp
    osc/OscFeedback.cpp
    osc/OscPacket.cpp
    osc/OscPeers.cpp
    osc/OscRouter.cpp
    osc/OscService.cpp
    # Headers (listed for IDE visibility)
    AudioBridge.hpp
    session/SessionClipScheduler.hpp
    session/SessionClipAudioMonitor.hpp
    session/SessionClipCommandQueue.hpp
    session/SessionClipStateQueue.hpp
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
)

# The base pack is compiled separately against magda::device_sdk. Enforce both
# the movable SDK include boundary and complete independence from Tracktion.
magda_validate_device_pack_sources(
    "MAGDA base device pack"
    ENGINE_NEUTRAL
    ${_magda_neutral_base_device_sources}
)
unset(_magda_neutral_base_device_sources)
unset(_magda_legacy_tracktion_device_sources)

target_sources(magda_daw_app PRIVATE
    AudioEngineOptimizer.cpp
    AudioEngineOptimizer.hpp
)
