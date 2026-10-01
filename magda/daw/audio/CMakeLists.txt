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
target_include_directories(magda_base_devices PRIVATE "${CMAKE_BINARY_DIR}/compiled_dsps")

set(_magda_device_sdk_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceParameterHandle.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DevicePluginDefaults.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DevicePluginHandle.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceServices.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceServices.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceSessionKey.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/IFaustEditorModel.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceCatalogParameters.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceCatalogParameters.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceManifests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceManifests.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceJuceInterop.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceStateDocument.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceStateDocument.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SavedDeviceState.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SavedDeviceState.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceStateHydration.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceStateHydration.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceTiming.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceTiming.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/InternalPluginRegistry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/InternalPluginRegistry.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/MagdaDevice.hpp"
)

set(_magda_engine_device_adapter_sources
    # Keep lookup with its factory caller for single-pass static linkers on Linux.
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin_manager/ExternalPluginLookup.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin_manager/ExternalPluginLookup.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/EngineDeviceFactory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/EngineDeviceFactory.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/ControlExecutor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/ControlExecutor.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/DeviceControl.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/DeviceControl.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/EngineExternalDevice.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/EngineExternalDevice.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/EngineMagdaDevice.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/EngineMagdaDevice.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/PluginAssignments.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/engine/PluginAssignments.hpp"
)

set(_magda_base_device_support_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/FaustResources.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/FaustResources.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/AudioTapBuffer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/BandSpectrum.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/BandSpectrum.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/MaskingDetector.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/TrackMeasurer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/insert_capture/CaptureWindowMath.hpp"
)
file(GLOB _magda_device_dsp_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/faust_dsp/*.dsp")
set_source_files_properties(${_magda_device_dsp_sources} PROPERTIES HEADER_FILE_ONLY TRUE)

set(_magda_neutral_base_device_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/analysis/AudioTapBuffer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/transport/RampCurve.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/MonoStepSequencer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/MonoStepSequencer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/NoteSink.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/PolyStepSequencer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/PolyStepSequencer.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/PublishedPattern.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/StepClock.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/StepClock.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/StepPattern.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ArpeggiatorPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ArpeggiatorPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/AnalysisTapPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/FaustMetadataParser.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/FaustMetadataParser.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/MagdaConvolutionPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/MagdaConvolutionPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/DeviceNoteSink.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/MagdaDevice.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/MidiMagdaDevice.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/MidiStrumPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/MidiStrumPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/OscilloscopePlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/OscilloscopePlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/PolyStepSequencerPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/PolyStepSequencerPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/FaustParamInfo.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/FaustParamInfo.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/FaustParamPool.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/FaustParamPool.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/FaustPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/FaustPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SidechainPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SidechainPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/StepSequencerPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/StepSequencerPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SpectrumAnalyzerPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SpectrumAnalyzerPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ToneGeneratorPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/ToneGeneratorPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/CompiledFaustInterface.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/CompiledPluginRegistry.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/MagdaCompiledEffect.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/MagdaCompiledEffect.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/MagdaCompiledPolyInstrument.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/compiled/MagdaCompiledPolyInstrument.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/mutable/MutableCloudsPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/mutable/MutableCloudsPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/mutable/MutableElementsPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/mutable/MutableElementsPlugin.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/mutable/MutableRingsPlugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/mutable/MutableRingsPlugin.hpp"
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

target_sources(magda_devices PRIVATE
    ${_magda_device_sdk_sources}
)

# All shipped device implementations are native.
file(GLOB_RECURSE _magda_native_device_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*.hpp")
list(REMOVE_ITEM _magda_native_device_sources
    ${_magda_device_sdk_sources} ${_magda_engine_device_adapter_sources})
target_sources(magda_base_devices PRIVATE
    ${_magda_native_device_sources}
    ${_magda_base_device_support_sources}
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/MonoStepSequencer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/PolyStepSequencer.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/StepClock.cpp"
    ${_magda_device_dsp_sources})

    add_library(magda_engine_devices STATIC ${_magda_engine_device_adapter_sources})
    add_library(magda::engine_devices ALIAS magda_engine_devices)

    target_compile_features(magda_engine_devices PRIVATE cxx_std_23)
    target_include_directories(magda_engine_devices PRIVATE ${CMAKE_SOURCE_DIR})
    # magda_engine is added after magda/daw (it links the model layer this
    # directory defines), so this names it before it exists. A plain target name
    # resolves at generate time, which is what makes that legal.
    target_link_libraries(magda_engine_devices
        PUBLIC
        magda_device_sdk
        magda_engine
    )

target_sources(magda_daw PRIVATE
    DeviceParameterDisplayTextProvider.cpp
    DeviceParameterDisplayTextProvider.hpp
    DeviceParameterList.cpp
    DeviceParameterList.hpp
    ClipCommands.cpp
    FourOscMigration.cpp
    FourOscTranslation.cpp
    TrackMeasurementManager.cpp
    CompService.cpp
    CompService.hpp
    # The one owner of hardware audio I/O (#2746)
    io/AudioIOService.cpp
    io/AudioIOService.hpp
    io/CoreAudioRate.cpp
    io/CoreAudioRate.hpp
    io/HardwareRouteNames.cpp
    io/HardwareRouteNames.hpp
    io/LegacyAudioSettings.cpp
    io/LegacyAudioSettings.hpp
    AudioThumbnailManager.cpp
    WaveformPeakCache.cpp
    analysis/MixAnalysisInput.cpp
    analysis/OfflineMixAnalysis.cpp
    MidiBridge.cpp
    midi/ActiveMidiInputs.cpp
    midi/ActiveMidiInputs.hpp
    midi/MidiDeviceMatch.cpp
    faust/FaustModelEdits.cpp
    faust/FaustModelEdits.hpp
    sampling/SamplerMedia.cpp
    sampling/SamplerModelEdits.cpp
    sampling/SamplerModelEdits.hpp
    midi/QwertyMidiKeyboard.cpp
    # Phase 1: Pure data managers
    transport/TransportStateManager.cpp
    midi/MidiActivityMonitor.cpp
    params/ParameterManager.cpp
    # Phase 2: Independent features
    WarpMarkerManager.cpp
    # Compile-time pins between MAGDA's persisted enums and the engine's (#1887)
    # Phase 3: Core controllers
    # Restore persisted parameters and plugin state onto a live instance (#2243).
    plugin_manager/ExternalPluginState.cpp
    plugin_manager/ExternalPluginState.hpp
    automation/ModulationBaker.cpp
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
    session/SessionClipStateQueue.hpp
    TrackMeters.hpp
    transport/TransportStateManager.hpp
    midi/MidiActivityMonitor.hpp
    params/ParameterManager.hpp
    WarpMarkerManager.hpp
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

target_sources(magda_daw_app PRIVATE
    AudioEngineOptimizer.cpp
    AudioEngineOptimizer.hpp
)
