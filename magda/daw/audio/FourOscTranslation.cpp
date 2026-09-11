#include "FourOscTranslation.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <tuple>

#include "../core/DeviceState.hpp"
#include "../core/RackInfo.hpp"
#include "plugins/compiled/MagdaChorusCompiledPlugin.hpp"
#include "plugins/compiled/MagdaClipperCompiledPlugin.hpp"
#include "plugins/compiled/MagdaDelayCompiledPlugin.hpp"
#include "plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "plugins/compiled/MagdaReverbCompiledPlugin.hpp"
#include "plugins/tracktion/TracktionDeviceStateBridge.hpp"

namespace magda::daw::audio {

namespace {

using PolySynth = compiled::MagdaPolySynthCompiledPlugin;

/// 4OSC's own wave numbering (FourOscPlugin.h).
enum class FourOscWave { none, sine, triangle, sawUp, sawDown, square, random };

/// The rack fader's own range (RackComponent.cpp), which is what the gains
/// moved onto it have to fit in.
constexpr float kRackFaderMinDb = -60.0f;
constexpr float kRackFaderMaxDb = 6.0f;

/// Poly Synth's, which is a different order and a shorter list.
constexpr int kPolySine = 0;
constexpr int kPolySaw = 1;
constexpr int kPolySquare = 2;
constexpr int kPolyTriangle = 3;

/// 4OSC's parameters in the order it declares them, which is the order a
/// project's paramIndex counts in (tests/device_param_schema.txt). The names
/// a project saves are display names ("Tune 1"), so the id cannot be matched
/// against them and the index is what addresses a parameter.
///
/// Each carries the value 4OSC holds when nothing writes the property
/// (FourOscPlugin.cpp), which is not the value Poly Synth holds.
struct FourOscParameter {
    const char* id;
    float defaultValue;
};

constexpr FourOscParameter kFourOscParameters[] = {
    {"tune1", 0.0f},
    {"fineTune1", 0.0f},
    {"level1", 0.0f},
    {"pulseWidth1", 0.5f},
    {"detune1", 0.0f},
    {"spread1", 0.0f},
    {"pan1", 0.0f},
    {"tune2", 0.0f},
    {"fineTune2", 0.0f},
    {"level2", 0.0f},
    {"pulseWidth2", 0.5f},
    {"detune2", 0.0f},
    {"spread2", 0.0f},
    {"pan2", 0.0f},
    {"tune3", 0.0f},
    {"fineTune3", 0.0f},
    {"level3", 0.0f},
    {"pulseWidth3", 0.5f},
    {"detune3", 0.0f},
    {"spread3", 0.0f},
    {"pan3", 0.0f},
    {"tune4", 0.0f},
    {"fineTune4", 0.0f},
    {"level4", 0.0f},
    {"pulseWidth4", 0.5f},
    {"detune4", 0.0f},
    {"spread4", 0.0f},
    {"pan4", 0.0f},
    {"lfoRate1", 1.0f},
    {"lfoDepth1", 1.0f},
    {"lfoRate2", 1.0f},
    {"lfoDepth2", 1.0f},
    {"modAttack1", 0.1f},
    {"modDecay1", 0.1f},
    {"modSustain1", 80.0f},
    {"modRelease1", 0.1f},
    {"modAttack2", 0.1f},
    {"modDecay2", 0.1f},
    {"modSustain2", 80.0f},
    {"modRelease2", 0.1f},
    {"ampAttack", 0.1f},
    {"ampDecay", 0.1f},
    {"ampSustain", 80.0f},
    {"ampRelease", 0.1f},
    {"ampVelocity", 100.0f},
    {"filterAttack", 0.1f},
    {"filterDecay", 0.1f},
    {"filterSustain", 80.0f},
    {"filterRelease", 0.1f},
    {"filterFreq", 69.0f},
    {"filterResonance", 0.5f},
    {"filterAmount", 0.0f},
    {"filterKey", 0.0f},
    {"filterVelocity", 0.0f},
    {"distortion", 0.0f},
    {"reverbSize", 0.0f},
    {"reverbDamping", 0.0f},
    {"reverbWidth", 0.0f},
    {"reverbMix", 0.0f},
    {"delayFeedback", -10.0f},
    {"delayCrossfeed", -100.0f},
    {"delayMix", 0.0f},
    {"chorusSpeed", 1.0f},
    {"chorusDepth", 3.0f},
    {"chorusWidth", 0.5f},
    {"chorusMix", 0.0f},
    {"legato", 0.0f},
    {"masterLevel", 0.0f},
};

/// Where @p id sits in the list above, or -1.
int fourOscParameterIndex(const juce::String& id) {
    for (auto index = 0; index < static_cast<int>(std::size(kFourOscParameters)); ++index)
        if (id == kFourOscParameters[index].id)
            return index;

    return -1;
}

/**
 * @brief The value 4OSC's @p id holds.
 *
 * The model first, addressed by index. A legacy project also writes the
 * non-default values into its plugin XML, and that is the fallback: a project
 * saved before the model became the authority for parameters (#2317) can have
 * the value in only one of the two.
 *
 * Written by neither means 4OSC was sitting on its own default, and 4OSC's
 * defaults are not Poly Synth's: leaving the slot alone put an untouched
 * patch at -12 dB and a 5 ms attack where 4OSC had 0 dB and 100 ms.
 */
float parameterValue(const DeviceInfo& device, const juce::ValueTree& props,
                     const juce::String& id) {
    const auto index = fourOscParameterIndex(id);

    if (index >= 0)
        for (const auto& parameter : device.parameters)
            if (parameter.paramIndex == index)
                return parameter.currentValue;

    if (const auto saved = props.getProperty(juce::Identifier(id)); !saved.isVoid())
        return static_cast<float>(saved);

    return index >= 0 ? kFourOscParameters[index].defaultValue : 0.0f;
}

/// Whether the patch moved @p id off 4OSC's default. What makes a control
/// Poly Synth has no place for worth reporting is that somebody set it.
bool isSetByPatch(const DeviceInfo& device, const juce::ValueTree& props, const juce::String& id) {
    const auto index = fourOscParameterIndex(id);
    return index >= 0 &&
           parameterValue(device, props, id) != kFourOscParameters[index].defaultValue;
}

/// 4OSC keeps wave shape, filter type and voice mode outside its parameters,
/// as properties on its own ValueTree.
///
/// Through devicePluginTreeFromState() rather than device_state::decode():
/// a project old enough to hold a 4OSC often stores it as legacy engine XML,
/// which decode() refuses. Reading nothing there left every oscillator
/// looking like "none" and the translated synth silent.
juce::ValueTree savedProperties(const DeviceInfo& device) {
    return tracktion_adapter::devicePluginTreeFromState(device.pluginState);
}

int propertyOr(const juce::ValueTree& props, const juce::String& name, int fallback) {
    const auto value = props.getProperty(juce::Identifier(name));
    return value.isVoid() ? fallback : static_cast<int>(value);
}

float floatPropertyOr(const juce::ValueTree& props, const juce::String& name, float fallback) {
    const auto value = props.getProperty(juce::Identifier(name));
    return value.isVoid() ? fallback : static_cast<float>(value);
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
///
/// Built from the 4OSC rather than from nothing: the slot's gain, its macros
/// and modulators, its panel state and its name belong to the device in the
/// chain, not to the plugin being swapped underneath it. Only what describes
/// 4OSC itself is replaced.
DeviceInfo polySynthDevice(const DeviceInfo& fourOsc) {
    const PolySynth metadata;

    DeviceInfo device = fourOsc;
    device.pluginId = PolySynth::xmlTypeName;
    device.deviceType = DeviceType::Instrument;
    device.isInstrument = true;
    device.canReceiveMidi = true;
    device.producesMidi = false;
    device.format = PluginFormat::Internal;
    device.audioInputChannels = 0;
    device.audioOutputChannels = 2;

    // 4OSC's, all of it: the saved patch, the scan identity, the parameters
    // and every selection made by index into them.
    device.pluginState = {};
    device.uniqueId = {};
    device.fileOrIdentifier = {};
    device.vst3ClassId = {};
    device.vst3Preset = {};
    device.parameters.clear();
    device.wrapperParameters.clear();
    device.meters.clear();
    device.visibleParameters.clear();
    device.miniMixerParameters.clear();
    device.aiSoundDesignerParameters.clear();
    device.currentParameterPage = 0;

    for (auto index = 0; index < metadata.parameterCount(); ++index) {
        auto info = metadata.parameterInfo(index);
        info.currentValue = info.defaultValue;
        device.parameters.push_back(std::move(info));
    }

    return device;
}

void translateOscillators(const DeviceInfo& fourOsc, const juce::ValueTree& props, DeviceInfo& poly,
                          std::vector<FourOscGap>& gaps) {
    for (auto osc = 1; osc <= PolySynth::kNumOscillators; ++osc) {
        const auto number = juce::String(osc);
        const auto base = PolySynth::kOscBaseSlot + (osc - 1) * PolySynth::kOscSlotCount;
        // TE writes only what differs from a property's default, so a patch
        // left on 4OSC's own defaults has no waveShape at all. Osc 1 defaults
        // to sine and the rest to none (FourOscPlugin.cpp): defaulting all
        // four to none silenced every such patch.
        const auto fallback =
            osc == 1 ? static_cast<int>(FourOscWave::sine) : static_cast<int>(FourOscWave::none);
        const auto shape =
            static_cast<FourOscWave>(propertyOr(props, "waveShape" + number, fallback));

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

        const auto tune = parameterValue(fourOsc, props, "tune" + number);
        setSlot(poly, base + 2, clampToSlot(poly, base + 2, tune));

        const auto fine = parameterValue(fourOsc, props, "fineTune" + number);
        setSlot(poly, base + 3, clampToSlot(poly, base + 3, fine));

        // Both are already dB. 4OSC reaches -100 where Poly Synth stops at
        // -60, and both are silence.
        const auto level = parameterValue(fourOsc, props, "level" + number);
        setSlot(poly, base + 1, clampToSlot(poly, base + 1, level));

        // Addressed by 4OSC's own parameter ids, which are what a project
        // saves.
        for (const auto& [id, label, reason] :
             {std::tuple{"pulseWidth", "Pulse Width", "no pulse width control"},
              std::tuple{"detune", "Detune", "no unison"},
              std::tuple{"spread", "Spread", "no unison"},
              std::tuple{"pan", "Pan", "no per-oscillator pan"}})
            if (isSetByPatch(fourOsc, props, juce::String(id) + number))
                gaps.push_back({"Osc " + number + " " + label, juce::String("dropped: ") + reason});
    }
}

void translateEnvelopes(const DeviceInfo& fourOsc, const juce::ValueTree& props, DeviceInfo& poly) {
    // 4OSC holds envelope times in seconds and sustain as a percentage; Poly
    // Synth uses milliseconds and a 0..1 fraction.
    const auto seconds = [&](const juce::String& name, int slot) {
        setSlot(poly, slot,
                clampToSlot(poly, slot, parameterValue(fourOsc, props, name) * 1000.0f));
    };
    const auto percent = [&](const juce::String& name, int slot) {
        setSlot(poly, slot, clampToSlot(poly, slot, parameterValue(fourOsc, props, name) / 100.0f));
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

void translateFilter(const DeviceInfo& fourOsc, const juce::ValueTree& props, DeviceInfo& poly,
                     std::vector<FourOscGap>& gaps) {
    const auto type = polyFilterTypeFor(propertyOr(props, "filterType", 0));

    if (type.has_value())
        setSlot(poly, PolySynth::kFilterTypeSlot, static_cast<float>(*type));
    else
        // 4OSC bypasses the filter entirely at type 0. Poly Synth's is always
        // in the path, so the nearest thing is a lowpass out of the way.
        setSlot(poly, PolySynth::kCutoffSlot, slotMaximum(poly, PolySynth::kCutoffSlot));

    if (type.has_value()) {
        const auto note = parameterValue(fourOsc, props, "filterFreq");
        setSlot(poly, PolySynth::kCutoffSlot,
                clampToSlot(poly, PolySynth::kCutoffSlot, cutoffHzFromMidiNote(note)));
    }

    const auto resonance = parameterValue(fourOsc, props, "filterResonance");
    setSlot(poly, PolySynth::kResonanceSlot,
            clampToSlot(poly, PolySynth::kResonanceSlot,
                        resonance / 100.0f * slotMaximum(poly, PolySynth::kResonanceSlot)));

    // 4OSC's amount is -1..1 of its own sweep; Poly Synth's is octaves.
    const auto amount = parameterValue(fourOsc, props, "filterAmount");
    setSlot(poly, PolySynth::kFilterEnvAmtSlot,
            clampToSlot(poly, PolySynth::kFilterEnvAmtSlot,
                        amount * slotMaximum(poly, PolySynth::kFilterEnvAmtSlot)));

    setSlot(poly, PolySynth::kFilterSlopeSlot,
            propertyOr(props, "filterSlope", 12) >= 24 ? 1.0f : 0.0f);

    if (isSetByPatch(fourOsc, props, "filterKey"))
        gaps.push_back({"Filter Key", "dropped: no keyboard tracking"});
}

void reportUnisonAndEffects(const DeviceInfo& fourOsc, const juce::ValueTree& props,
                            std::vector<FourOscGap>& gaps) {
    for (auto osc = 1; osc <= PolySynth::kNumOscillators; ++osc)
        if (propertyOr(props, "voices" + juce::String(osc), 1) > 1) {
            gaps.push_back({"Unison", "dropped: Poly Synth has one voice per oscillator"});
            break;
        }

    for (auto lfo = 1; lfo <= 2; ++lfo)
        if (isSetByPatch(fourOsc, props, "lfoDepth" + juce::String(lfo))) {
            gaps.push_back({"LFO", "dropped: use a modifier on the parameter"});
            break;
        }

    for (auto env = 1; env <= 2; ++env)
        if (isSetByPatch(fourOsc, props, "modSustain" + juce::String(env))) {
            gaps.push_back({"Mod envelope", "dropped: use a modifier on the parameter"});
            break;
        }
}

/// A compiled device at its defaults, ready for the slots below to move.
template <typename Device> DeviceInfo compiledDevice(DeviceId id) {
    const Device metadata;

    DeviceInfo device;
    device.id = id;
    device.name = metadata.deviceName();
    device.pluginId = Device::xmlTypeName;
    device.deviceType = DeviceType::Effect;
    device.format = PluginFormat::Internal;
    device.audioInputChannels = 2;
    device.audioOutputChannels = 2;

    for (auto index = 0; index < metadata.parameterCount(); ++index) {
        auto info = metadata.parameterInfo(index);
        info.currentValue = info.defaultValue;
        device.parameters.push_back(std::move(info));
    }

    return device;
}

/// dB to a 0..1 gain, for the two 4OSC controls held in dB that reach a
/// normalised slot.
float gainFromDecibels(float decibels) {
    return decibels <= -100.0f ? 0.0f : std::pow(10.0f, decibels / 20.0f);
}

/// The Division choice nearest @p beats, as the index the slot stores. Both
/// scales count quarter notes, so the number carries across and only has to
/// land on an entry; the slot holds that entry's position, not its value, and
/// the entries come from the dsp's own menu rather than a copy of it.
float nearestDelayDivision(const compiled::MagdaDelayCompiledPlugin& delay, float beats) {
    const auto values = delay.menuValuesForIdx(compiled::MagdaDelayCompiledPlugin::kDivisionSlot);
    if (values.empty())
        return 0.0f;

    std::size_t nearest = 0;
    for (std::size_t index = 0; index < values.size(); ++index)
        if (std::abs(values[index] - beats) < std::abs(values[nearest] - beats))
            nearest = index;

    return static_cast<float>(nearest);
}

/**
 * @brief 4OSC's built-in effects as MAGDA devices, in one rack.
 *
 * Ordered as 4OSC processes them (FourOscPlugin.cpp): distortion, chorus,
 * delay, reverb. Only the ones its `<fx>On` properties switched on.
 *
 * @p nextId hands out ids for the devices, which the caller owns: these are
 * new devices in the project and cannot reuse the synth's.
 */
std::unique_ptr<RackInfo> buildEffects(const DeviceInfo& fourOsc, const juce::ValueTree& props,
                                       const std::function<DeviceId()>& nextId) {
    ChainInfo chain;

    const auto on = [&props](const juce::String& name) { return propertyOr(props, name, 0) != 0; };
    const auto value = [&fourOsc, &props](const juce::String& name) {
        return parameterValue(fourOsc, props, name);
    };

    if (on("distortionOn")) {
        using Clipper = compiled::MagdaClipperCompiledPlugin;
        auto device = compiledDevice<Clipper>(nextId());
        // 4OSC multiplies by drive and clamps at 1/(2*drive), so its 0..1 is
        // the whole range of the effect.
        setSlot(device, Clipper::kDriveSlot,
                clampToSlot(device, Clipper::kDriveSlot, value("distortion") * 24.0f));
        chain.elements.push_back(ChainElement{std::move(device)});
    }

    if (on("chorusOn")) {
        using Chorus = compiled::MagdaChorusCompiledPlugin;
        auto device = compiledDevice<Chorus>(nextId());
        setSlot(device, Chorus::kRateSlot,
                clampToSlot(device, Chorus::kRateSlot, value("chorusSpeed")));
        // 4OSC's depth is milliseconds of delay, up to 20; MAGDA's is a
        // fraction of its own range.
        setSlot(device, Chorus::kDepthSlot,
                clampToSlot(device, Chorus::kDepthSlot, value("chorusDepth") / 20.0f));
        setSlot(device, Chorus::kWidthSlot,
                clampToSlot(device, Chorus::kWidthSlot, value("chorusWidth")));
        setSlot(device, Chorus::kMixSlot,
                clampToSlot(device, Chorus::kMixSlot, value("chorusMix")));
        chain.elements.push_back(ChainElement{std::move(device)});
    }

    if (on("delayOn")) {
        using Delay = compiled::MagdaDelayCompiledPlugin;
        auto device = compiledDevice<Delay>(nextId());
        // 4OSC holds its delay in beats and divides by the tempo at render
        // time (FourOscPlugin.cpp), so the synced division is the same number
        // and the free-time slot would be the wrong home for it.
        setSlot(device, Delay::kSyncSlot, 1.0f);
        setSlot(device, Delay::kDivisionSlot,
                clampToSlot(device, Delay::kDivisionSlot,
                            nearestDelayDivision(Delay{}, floatPropertyOr(props, "delay", 1.0f))));
        setSlot(
            device, Delay::kFeedbackSlot,
            clampToSlot(device, Delay::kFeedbackSlot, gainFromDecibels(value("delayFeedback"))));
        setSlot(device, Delay::kCrossSlot,
                clampToSlot(device, Delay::kCrossSlot, gainFromDecibels(value("delayCrossfeed"))));
        setSlot(device, Delay::kMixSlot, clampToSlot(device, Delay::kMixSlot, value("delayMix")));
        chain.elements.push_back(ChainElement{std::move(device)});
    }

    if (on("reverbOn")) {
        using Reverb = compiled::MagdaReverbCompiledPlugin;
        auto device = compiledDevice<Reverb>(nextId());
        // 4OSC holds all four as 0..1; MAGDA's decay, damping and width are
        // percentages.
        setSlot(device, Reverb::kDecaySlot,
                clampToSlot(device, Reverb::kDecaySlot, value("reverbSize") * 100.0f));
        setSlot(device, Reverb::kDampingSlot,
                clampToSlot(device, Reverb::kDampingSlot, value("reverbDamping") * 100.0f));
        setSlot(device, Reverb::kWidthSlot,
                clampToSlot(device, Reverb::kWidthSlot, value("reverbWidth") * 100.0f));
        setSlot(device, Reverb::kMixSlot,
                clampToSlot(device, Reverb::kMixSlot, value("reverbMix")));
        chain.elements.push_back(ChainElement{std::move(device)});
    }

    if (chain.elements.empty())
        return nullptr;

    auto rack = std::make_unique<RackInfo>();
    rack->name = "4OSC FX";
    chain.id = ChainId{1};
    rack->chains.push_back(std::move(chain));

    return rack;
}

}  // namespace

bool isFourOscDevice(const DeviceInfo& device) {
    return device.pluginId.equalsIgnoreCase("4osc");
}

int fourOscParameterCount() {
    return static_cast<int>(std::size(kFourOscParameters));
}

FourOscTranslation translateFourOsc(const DeviceInfo& fourOsc,
                                    const std::function<DeviceId()>& nextEffectId) {
    const auto props = savedProperties(fourOsc);

    FourOscTranslation translated{.device = polySynthDevice(fourOsc)};

    translateOscillators(fourOsc, props, translated.device, translated.gaps);
    translateEnvelopes(fourOsc, props, translated.device);
    translateFilter(fourOsc, props, translated.device, translated.gaps);

    const auto legato = parameterValue(fourOsc, props, "legato");
    setSlot(translated.device, PolySynth::kGlideSlot,
            clampToSlot(translated.device, PolySynth::kGlideSlot, legato));

    setSlot(translated.device, PolySynth::kVoiceModeSlot,
            static_cast<float>(polyVoiceModeFor(propertyOr(props, "voiceMode", 2))));

    reportUnisonAndEffects(fourOsc, props, translated.gaps);

    if (nextEffectId)
        translated.effects = buildEffects(fourOsc, props, nextEffectId);

    const auto master = parameterValue(fourOsc, props, "masterLevel");

    if (translated.effects == nullptr) {
        // Nothing between the synth and the slot's output, so the master level
        // is the synth's own output gain.
        setSlot(translated.device, PolySynth::kOutputGainSlot,
                clampToSlot(translated.device, PolySynth::kOutputGainSlot, master));

        return translated;
    }

    // 4OSC runs its effects BEFORE its master level, and the slot's own trim
    // follows the whole plugin (FourOscPlugin::applyEffects). Split across two
    // slots, both of those now sit in front of the rack, where a gain is a
    // different distortion drive and a different delay and reverb balance.
    // The rack's fader is where they land: it is the one control in the
    // translation that is downstream of all four effects.
    translated.effects->volume =
        std::clamp(master + translated.device.gainDb, kRackFaderMinDb, kRackFaderMaxDb);
    translated.device.gainDb = 0.0f;
    translated.device.gainValue = 1.0f;

    return translated;
}

}  // namespace magda::daw::audio
