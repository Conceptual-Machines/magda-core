#include "FourOscTranslation.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

#include "../core/DeviceState.hpp"
#include "plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"

namespace magda::daw::audio {

namespace {

using PolySynth = compiled::MagdaPolySynthCompiledPlugin;

/// 4OSC's own wave numbering (FourOscPlugin.h).
enum class FourOscWave { none, sine, triangle, sawUp, sawDown, square, random };

/// Poly Synth's, which is a different order and a shorter list.
constexpr int kPolySine = 0;
constexpr int kPolySaw = 1;
constexpr int kPolySquare = 2;
constexpr int kPolyTriangle = 3;

/// The value @p name holds, or nothing. 4OSC's parameters are addressed by the
/// id TE gave them ("tune1", "filterFreq"), which is what a project saves.
std::optional<float> parameterValue(const DeviceInfo& device, const juce::String& name) {
    for (const auto& parameter : device.parameters)
        if (parameter.name.equalsIgnoreCase(name))
            return parameter.currentValue;

    return std::nullopt;
}

/// 4OSC keeps wave shape, filter type and voice mode outside its parameters,
/// as properties on its own ValueTree.
juce::NamedValueSet savedProperties(const DeviceInfo& device) {
    if (const auto doc = device_state::decode(device.pluginState))
        return doc->root.props;

    return {};
}

int propertyOr(const juce::NamedValueSet& props, const juce::String& name, int fallback) {
    const auto* value = props.getVarPointer(name);
    return value != nullptr ? static_cast<int>(*value) : fallback;
}

void setSlot(DeviceInfo& device, int slot, float value) {
    for (auto& parameter : device.parameters)
        if (parameter.paramIndex == slot) {
            parameter.currentValue = value;
            return;
        }
}

/// Clamped to the slot's own range, which the device carries. 4OSC's ranges
/// are wider than Poly Synth's in several places.
float clampToSlot(const DeviceInfo& poly, int slot, float value) {
    for (const auto& parameter : poly.parameters)
        if (parameter.paramIndex == slot)
            return std::clamp(value, parameter.minValue, parameter.maxValue);

    return value;
}

/// The top of a slot's range, for a control that has to be put out of the way.
float slotMaximum(const DeviceInfo& poly, int slot) {
    for (const auto& parameter : poly.parameters)
        if (parameter.paramIndex == slot)
            return parameter.maxValue;

    return 0.0f;
}

/// 4OSC's filter cutoff is a MIDI note number, 0 to 135.076232, which is its
/// way of spacing the control logarithmically. Poly Synth's is Hz.
float cutoffHzFromMidiNote(float note) {
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

/// Poly Synth has no inverted saw and no noise oscillator. Nullopt means the
/// oscillator cannot be translated and is silenced instead.
std::optional<int> polyWaveFor(FourOscWave wave) {
    switch (wave) {
        case FourOscWave::sine:
            return kPolySine;
        case FourOscWave::triangle:
            return kPolyTriangle;
        case FourOscWave::sawUp:
        case FourOscWave::sawDown:
            return kPolySaw;
        case FourOscWave::square:
            return kPolySquare;
        case FourOscWave::none:
        case FourOscWave::random:
            break;
    }

    return std::nullopt;
}

/// 4OSC filterType: 0 off, then lowpass, highpass, bandpass, notch. Poly
/// Synth's list starts at lowpass and has no off, so off is carried as a
/// cutoff wide open rather than as a type.
std::optional<int> polyFilterTypeFor(int fourOscType) {
    return fourOscType >= 1 && fourOscType <= 4 ? std::optional<int>{fourOscType - 1}
                                                : std::nullopt;
}

/// 4OSC voiceMode: 0 mono, 1 legato, 2 poly. Poly Synth: 0 poly, 1 mono,
/// 2 legato.
int polyVoiceModeFor(int fourOscMode) {
    switch (fourOscMode) {
        case 0:
            return 1;
        case 1:
            return 2;
        default:
            return 0;
    }
}

/// A Poly Synth at its defaults, which is what everything 4OSC does not
/// describe is left at.
DeviceInfo polySynthDevice(const DeviceInfo& fourOsc) {
    const PolySynth metadata;

    DeviceInfo device;
    device.id = fourOsc.id;
    device.name = fourOsc.name;
    device.pluginId = PolySynth::xmlTypeName;
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    device.format = PluginFormat::Internal;
    device.audioInputChannels = 0;
    device.audioOutputChannels = 2;
    device.bypassed = fourOsc.bypassed;

    for (auto index = 0; index < metadata.parameterCount(); ++index) {
        auto info = metadata.parameterInfo(index);
        info.currentValue = info.defaultValue;
        device.parameters.push_back(std::move(info));
    }

    return device;
}

void translateOscillators(const DeviceInfo& fourOsc, const juce::NamedValueSet& props,
                          DeviceInfo& poly, std::vector<FourOscGap>& gaps) {
    for (auto osc = 1; osc <= PolySynth::kNumOscillators; ++osc) {
        const auto number = juce::String(osc);
        const auto base = PolySynth::kOscBaseSlot + (osc - 1) * PolySynth::kOscSlotCount;
        const auto shape = static_cast<FourOscWave>(
            propertyOr(props, "waveShape" + number, static_cast<int>(FourOscWave::none)));

        const auto wave = polyWaveFor(shape);
        setSlot(poly, PolySynth::kOscEnableBaseSlot + (osc - 1), wave.has_value() ? 1.0f : 0.0f);

        if (shape == FourOscWave::random)
            gaps.push_back(
                {"Osc " + number + " noise", "silenced: Poly Synth has no noise source"});

        if (!wave.has_value())
            continue;

        setSlot(poly, base + 0, static_cast<float>(*wave));

        if (shape == FourOscWave::sawDown)
            gaps.push_back({"Osc " + number + " saw down", "translated as saw up"});

        if (const auto tune = parameterValue(fourOsc, "tune" + number))
            setSlot(poly, base + 2, clampToSlot(poly, base + 2, *tune));

        if (const auto fine = parameterValue(fourOsc, "fineTune" + number))
            setSlot(poly, base + 3, clampToSlot(poly, base + 3, *fine));

        // Both are already dB. 4OSC reaches -100 where Poly Synth stops at
        // -60, and both are silence.
        if (const auto level = parameterValue(fourOsc, "level" + number))
            setSlot(poly, base + 1, clampToSlot(poly, base + 1, *level));

        // Addressed by 4OSC's own parameter ids, which are what a project
        // saves.
        for (const auto& [id, label, reason] :
             {std::tuple{"pulseWidth", "Pulse Width", "no pulse width control"},
              std::tuple{"detune", "Detune", "no unison"},
              std::tuple{"spread", "Spread", "no unison"},
              std::tuple{"pan", "Pan", "no per-oscillator pan"}})
            if (const auto value = parameterValue(fourOsc, juce::String(id) + number);
                value.has_value() && *value != 0.0f)
                gaps.push_back({"Osc " + number + " " + label, juce::String("dropped: ") + reason});
    }
}

void translateEnvelopes(const DeviceInfo& fourOsc, DeviceInfo& poly) {
    // 4OSC holds envelope times in seconds and sustain as a percentage; Poly
    // Synth uses milliseconds and a 0..1 fraction.
    const auto seconds = [&](const juce::String& name, int slot) {
        if (const auto value = parameterValue(fourOsc, name))
            setSlot(poly, slot, clampToSlot(poly, slot, *value * 1000.0f));
    };
    const auto percent = [&](const juce::String& name, int slot) {
        if (const auto value = parameterValue(fourOsc, name))
            setSlot(poly, slot, clampToSlot(poly, slot, *value / 100.0f));
    };

    seconds("ampAttack", PolySynth::kAmpAttackSlot);
    seconds("ampDecay", PolySynth::kAmpDecaySlot);
    percent("ampSustain", PolySynth::kAmpSustainSlot);
    seconds("ampRelease", PolySynth::kAmpReleaseSlot);
    percent("ampVelocity", PolySynth::kVelAmpSlot);

    seconds("filterAttack", PolySynth::kFilterAttackSlot);
    seconds("filterDecay", PolySynth::kFilterDecaySlot);
    percent("filterSustain", PolySynth::kFilterSustainSlot);
    seconds("filterRelease", PolySynth::kFilterReleaseSlot);
    percent("filterVelocity", PolySynth::kVelFilterSlot);
}

void translateFilter(const DeviceInfo& fourOsc, const juce::NamedValueSet& props, DeviceInfo& poly,
                     std::vector<FourOscGap>& gaps) {
    const auto type = polyFilterTypeFor(propertyOr(props, "filterType", 0));

    if (type.has_value())
        setSlot(poly, PolySynth::kFilterTypeSlot, static_cast<float>(*type));
    else
        // 4OSC bypasses the filter entirely at type 0. Poly Synth's is always
        // in the path, so the nearest thing is a lowpass out of the way.
        setSlot(poly, PolySynth::kCutoffSlot, slotMaximum(poly, PolySynth::kCutoffSlot));

    if (type.has_value())
        if (const auto note = parameterValue(fourOsc, "filterFreq"))
            setSlot(poly, PolySynth::kCutoffSlot,
                    clampToSlot(poly, PolySynth::kCutoffSlot, cutoffHzFromMidiNote(*note)));

    if (const auto resonance = parameterValue(fourOsc, "filterResonance"))
        setSlot(poly, PolySynth::kResonanceSlot,
                clampToSlot(poly, PolySynth::kResonanceSlot,
                            *resonance / 100.0f * slotMaximum(poly, PolySynth::kResonanceSlot)));

    // 4OSC's amount is -1..1 of its own sweep; Poly Synth's is octaves.
    if (const auto amount = parameterValue(fourOsc, "filterAmount"))
        setSlot(poly, PolySynth::kFilterEnvAmtSlot,
                clampToSlot(poly, PolySynth::kFilterEnvAmtSlot,
                            *amount * slotMaximum(poly, PolySynth::kFilterEnvAmtSlot)));

    setSlot(poly, PolySynth::kFilterSlopeSlot,
            propertyOr(props, "filterSlope", 12) >= 24 ? 1.0f : 0.0f);

    if (const auto key = parameterValue(fourOsc, "filterKey"); key.has_value() && *key != 0.0f)
        gaps.push_back({"Filter Key", "dropped: no keyboard tracking"});
}

void reportUnisonAndEffects(const DeviceInfo& fourOsc, const juce::NamedValueSet& props,
                            std::vector<FourOscGap>& gaps) {
    for (auto osc = 1; osc <= PolySynth::kNumOscillators; ++osc)
        if (propertyOr(props, "voices" + juce::String(osc), 1) > 1) {
            gaps.push_back({"Unison", "dropped: Poly Synth has one voice per oscillator"});
            break;
        }

    for (const auto& [property, name] :
         {std::pair{juce::String("distortionOn"), juce::String("Distortion")},
          std::pair{juce::String("reverbOn"), juce::String("Reverb")},
          std::pair{juce::String("delayOn"), juce::String("Delay")},
          std::pair{juce::String("chorusOn"), juce::String("Chorus")}})
        if (propertyOr(props, property, 0) != 0)
            gaps.push_back({name, "dropped: add the device to the chain instead"});

    for (auto lfo = 1; lfo <= 2; ++lfo)
        if (const auto depth = parameterValue(fourOsc, "lfoDepth" + juce::String(lfo));
            depth.has_value() && *depth != 0.0f) {
            gaps.push_back({"LFO", "dropped: use a modifier on the parameter"});
            break;
        }

    for (auto env = 1; env <= 2; ++env)
        if (const auto sustain = parameterValue(fourOsc, "modSustain" + juce::String(env));
            sustain.has_value() && *sustain != 0.0f) {
            gaps.push_back({"Mod envelope", "dropped: use a modifier on the parameter"});
            break;
        }
}

}  // namespace

bool isFourOscDevice(const DeviceInfo& device) {
    return device.pluginId.equalsIgnoreCase("4osc");
}

FourOscTranslation translateFourOsc(const DeviceInfo& fourOsc) {
    const auto props = savedProperties(fourOsc);

    FourOscTranslation translated{.device = polySynthDevice(fourOsc), .gaps = {}};

    translateOscillators(fourOsc, props, translated.device, translated.gaps);
    translateEnvelopes(fourOsc, translated.device);
    translateFilter(fourOsc, props, translated.device, translated.gaps);

    if (const auto legato = parameterValue(fourOsc, "legato"))
        setSlot(translated.device, PolySynth::kGlideSlot,
                clampToSlot(translated.device, PolySynth::kGlideSlot, *legato));

    setSlot(translated.device, PolySynth::kVoiceModeSlot,
            static_cast<float>(polyVoiceModeFor(propertyOr(props, "voiceMode", 2))));

    reportUnisonAndEffects(fourOsc, props, translated.gaps);

    return translated;
}

}  // namespace magda::daw::audio
