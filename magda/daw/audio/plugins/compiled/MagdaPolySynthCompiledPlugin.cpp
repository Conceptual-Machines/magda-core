#include "plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"

#include "core/ParameterInfo.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_polysynth.generated.cpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaPolySynthCompiledPlugin::xmlTypeName = "magda_polysynth";

MagdaPolySynthCompiledPlugin::MagdaPolySynthCompiledPlugin() {
    initInstrument();
}

::dsp* MagdaPolySynthCompiledPlugin::createVoiceDsp() const {
    return new MagdaPolySynthDsp();
}

std::vector<MagdaPolySynthCompiledPlugin::HostSlotInfo> MagdaPolySynthCompiledPlugin::slotInfos()
    const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    const std::vector<juce::String> waveChoices{"Sine", "Saw", "Square", "Triangle"};

    // Four contiguous slots per oscillator (wave / level / coarse / fine).
    // Osc 1 is audible by default; the rest start silent.
    for (int osc = 0; osc < kNumOscillators; ++osc) {
        const int base = kOscBaseSlot + osc * kOscSlotCount;
        const juce::String prefix = "Osc " + juce::String(osc + 1) + " ";

        infos[base + 0] = {.name = prefix + "Wave",
                           .scale = magda::ParameterScale::Discrete,
                           .minValue = 0.0f,
                           .maxValue = static_cast<float>(waveChoices.size() - 1),
                           .defaultValue = 1.0f,  // Saw
                           .choices = waveChoices};
        infos[base + 1] = {.name = prefix + "Level",
                           .unit = "dB",
                           .scale = magda::ParameterScale::FaderDB,
                           .minValue = -60.0f,
                           .maxValue = 6.0f,
                           .defaultValue = (osc == 0) ? -12.0f : -60.0f};
        infos[base + 2] = {.name = prefix + "Coarse",
                           .unit = magda::technicalText(magda::TechnicalTextToken::Semitones),
                           .scale = magda::ParameterScale::Linear,
                           .minValue = -24.0f,
                           .maxValue = 24.0f,
                           .defaultValue = 0.0f};
        infos[base + 3] = {.name = prefix + "Fine",
                           .unit = magda::technicalText(magda::TechnicalTextToken::Cents),
                           .scale = magda::ParameterScale::Linear,
                           .minValue = -100.0f,
                           .maxValue = 100.0f,
                           .defaultValue = 0.0f};
    }

    infos[kFilterTypeSlot] = {.name = "Filter Type",
                              .scale = magda::ParameterScale::Discrete,
                              .minValue = 0.0f,
                              .maxValue = 3.0f,
                              .defaultValue = 0.0f,
                              .choices = {"Lowpass", "Highpass", "Bandpass", "Notch"}};
    infos[kCutoffSlot] = {.name = "Cutoff",
                          .unit = "Hz",
                          .scale = magda::ParameterScale::Logarithmic,
                          .minValue = 50.0f,
                          .maxValue = 18000.0f,
                          .defaultValue = 3000.0f};
    infos[kResonanceSlot] = {.name = "Resonance",
                             .scale = magda::ParameterScale::Linear,
                             .minValue = 0.0f,
                             .maxValue = 0.95f,
                             .defaultValue = 0.3f};
    infos[kFilterEnvAmtSlot] = {.name = "Filter Env",
                                .unit = "oct",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = -4.0f,
                                .maxValue = 4.0f,
                                .defaultValue = 0.0f};
    // Envelope times are in milliseconds (the formatter shows ms below 1 s, s
    // above). The DSP divides them back to seconds.
    infos[kFilterAttackSlot] = {.name = "Filter Attack",
                                .unit = "ms",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = 1.0f,
                                .maxValue = 2000.0f,
                                .defaultValue = 5.0f};
    infos[kFilterDecaySlot] = {.name = "Filter Decay",
                               .unit = "ms",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = 1.0f,
                               .maxValue = 2000.0f,
                               .defaultValue = 200.0f};
    infos[kFilterSustainSlot] = {.name = "Filter Sustain",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 1.0f,
                                 .defaultValue = 0.7f};
    infos[kFilterReleaseSlot] = {.name = "Filter Release",
                                 .unit = "ms",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 1.0f,
                                 .maxValue = 4000.0f,
                                 .defaultValue = 400.0f};

    infos[kAmpAttackSlot] = {.name = "Amp Attack",
                             .unit = "ms",
                             .scale = magda::ParameterScale::Linear,
                             .minValue = 1.0f,
                             .maxValue = 2000.0f,
                             .defaultValue = 5.0f};
    infos[kAmpDecaySlot] = {.name = "Amp Decay",
                            .unit = "ms",
                            .scale = magda::ParameterScale::Linear,
                            .minValue = 1.0f,
                            .maxValue = 2000.0f,
                            .defaultValue = 200.0f};
    infos[kAmpSustainSlot] = {.name = "Amp Sustain",
                              .scale = magda::ParameterScale::Linear,
                              .minValue = 0.0f,
                              .maxValue = 1.0f,
                              .defaultValue = 0.7f};
    infos[kAmpReleaseSlot] = {.name = "Amp Release",
                              .unit = "ms",
                              .scale = magda::ParameterScale::Linear,
                              .minValue = 1.0f,
                              .maxValue = 4000.0f,
                              .defaultValue = 400.0f};

    infos[kFilterDriveSlot] = {.name = "Filter Drive",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = 0.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 0.0f};

    infos[kFilterSlopeSlot] = {.name = "Filter Slope",
                               .scale = magda::ParameterScale::Discrete,
                               .minValue = 0.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 0.0f,
                               .choices = {"12 dB", "24 dB"}};

    infos[kBendRangeSlot] = {.name = "Bend Range",
                             .unit = magda::technicalText(magda::TechnicalTextToken::Semitones),
                             .scale = magda::ParameterScale::Linear,
                             .minValue = 0.0f,
                             .maxValue = 24.0f,
                             .defaultValue = 2.0f};

    infos[kVoiceModeSlot] = {.name = "Voice Mode",
                             .scale = magda::ParameterScale::Discrete,
                             .minValue = 0.0f,
                             .maxValue = 2.0f,
                             .defaultValue = 0.0f,
                             .choices = {"Poly", "Mono", "Legato"}};

    infos[kGlideSlot] = {.name = "Glide",
                         .unit = "ms",
                         .scale = magda::ParameterScale::Linear,
                         .minValue = 0.0f,
                         .maxValue = 2000.0f,
                         .defaultValue = 0.0f};

    for (int osc = 0; osc < kNumOscillators; ++osc) {
        infos[kOscResetBaseSlot + osc] = {.name = "Osc " + juce::String(osc + 1) + " Reset",
                                          .scale = magda::ParameterScale::Discrete,
                                          .minValue = 0.0f,
                                          .maxValue = 1.0f,
                                          .defaultValue = 0.0f,
                                          .choices = {"Off", "On"}};
    }

    infos[kVelAmpSlot] = {.name = "Vel Amp",
                          .scale = magda::ParameterScale::Linear,
                          .minValue = 0.0f,
                          .maxValue = 1.0f,
                          .defaultValue = 1.0f};
    infos[kVelFilterSlot] = {.name = "Vel Filter",
                             .unit = "oct",
                             .scale = magda::ParameterScale::Linear,
                             .minValue = 0.0f,
                             .maxValue = 6.0f,
                             .defaultValue = 0.0f};

    for (int osc = 0; osc < kNumOscillators; ++osc) {
        infos[kOscEnableBaseSlot + osc] = {.name = "Osc " + juce::String(osc + 1) + " Enable",
                                           .scale = magda::ParameterScale::Discrete,
                                           .minValue = 0.0f,
                                           .maxValue = 1.0f,
                                           .defaultValue = 1.0f,
                                           .choices = {"Off", "On"}};
    }

    infos[kOutputGainSlot] = {.name = "Output",
                              .unit = "dB",
                              .scale = magda::ParameterScale::FaderDB,
                              .minValue = -60.0f,
                              .maxValue = 6.0f,
                              .defaultValue = 0.0f};

    return infos;
}

const CompiledPluginSpec& getMagdaPolySynthSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaPolySynthCompiledPlugin::xmlTypeName,
        .displayName = "Poly Synth",
        .browserCategory = "Synth",
        .description = "Compiled Faust polyphonic synth: four detunable oscillators "
                       "(sine/saw/square/triangle) into a multimode filter with its own "
                       "envelope, plus an ADSR amp envelope. 16-voice, MIDI-driven.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaPolySynthCompiledPlugin>();
        },
        .isInstrument = true,
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
