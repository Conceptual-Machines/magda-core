#include "plugins/compiled/MagdaEqCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>

#include "core/ParameterUtils.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaEqCompiledPlugin::xmlTypeName = "magda_eq";

namespace {

magda::ParameterInfo parameterInfoForSlot(const CompiledHostSlotInfo& s) {
    magda::ParameterInfo info;
    info.minValue = s.minValue;
    info.maxValue = s.maxValue;
    info.defaultValue = s.defaultValue;
    info.unit = s.unit;
    info.scale = s.scale;
    if (std::isfinite(s.scaleAnchor))
        info.scaleAnchor = s.scaleAnchor;
    info.choices = s.choices;
    return info;
}

// Bands default to disabled Bell filters. That keeps the inserted EQ neutral
// and avoids spending per-sample CPU on inactive biquads.
struct BandDefaults {
    float enabled;
    float type;
    float freq;
    float q;
};
constexpr BandDefaults kBandDefaults[MagdaEqCompiledPlugin::kBandCount] = {
    {0.0f, 2.0f, 30.0f, 1.0f},    {0.0f, 2.0f, 100.0f, 1.0f},   {0.0f, 2.0f, 250.0f, 1.0f},
    {0.0f, 2.0f, 800.0f, 1.0f},   {0.0f, 2.0f, 2000.0f, 1.0f},  {0.0f, 2.0f, 5000.0f, 1.0f},
    {0.0f, 2.0f, 10000.0f, 1.0f}, {0.0f, 2.0f, 18000.0f, 1.0f},
};

constexpr float kTwoPi = 6.28318530717958647692f;

struct RbjCoeffs {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

RbjCoeffs makeRbj(MagdaEqCompiledPlugin::BandType type, float f0, float gainDb, float q,
                  float sampleRate) {
    RbjCoeffs out;
    const float safeQ = std::max(0.05f, q);
    const float fc = juce::jlimit(1.0f, sampleRate * 0.45f, f0);
    const float w0 = kTwoPi * fc / sampleRate;
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / (2.0f * safeQ);

    auto normalise = [&out](float b0, float b1, float b2, float a0, float a1, float a2) {
        const float inv = 1.0f / a0;
        out.b0 = b0 * inv;
        out.b1 = b1 * inv;
        out.b2 = b2 * inv;
        out.a1 = a1 * inv;
        out.a2 = a2 * inv;
    };

    using BandType = MagdaEqCompiledPlugin::BandType;
    switch (type) {
        case BandType::Highpass:
            normalise((1.0f + cw) * 0.5f, -(1.0f + cw), (1.0f + cw) * 0.5f, 1.0f + alpha,
                      -2.0f * cw, 1.0f - alpha);
            break;
        case BandType::Lowpass:
            normalise((1.0f - cw) * 0.5f, 1.0f - cw, (1.0f - cw) * 0.5f, 1.0f + alpha, -2.0f * cw,
                      1.0f - alpha);
            break;
        case BandType::Bell: {
            const float A = std::pow(10.0f, gainDb / 40.0f);
            normalise(1.0f + alpha * A, -2.0f * cw, 1.0f - alpha * A, 1.0f + alpha / A, -2.0f * cw,
                      1.0f - alpha / A);
            break;
        }
        case BandType::LowShelf: {
            const float A = std::pow(10.0f, gainDb / 40.0f);
            const float shelfAlpha = sw / 1.41421356f;
            const float sqA2 = 2.0f * std::sqrt(A) * shelfAlpha;
            normalise(A * ((A + 1.0f) - (A - 1.0f) * cw + sqA2),
                      2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw),
                      A * ((A + 1.0f) - (A - 1.0f) * cw - sqA2),
                      (A + 1.0f) + (A - 1.0f) * cw + sqA2, -2.0f * ((A - 1.0f) + (A + 1.0f) * cw),
                      (A + 1.0f) + (A - 1.0f) * cw - sqA2);
            break;
        }
        case BandType::HighShelf: {
            const float A = std::pow(10.0f, gainDb / 40.0f);
            const float shelfAlpha = sw / 1.41421356f;
            const float sqA2 = 2.0f * std::sqrt(A) * shelfAlpha;
            normalise(A * ((A + 1.0f) + (A - 1.0f) * cw + sqA2),
                      -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw),
                      A * ((A + 1.0f) + (A - 1.0f) * cw - sqA2),
                      (A + 1.0f) - (A - 1.0f) * cw + sqA2, 2.0f * ((A - 1.0f) - (A + 1.0f) * cw),
                      (A + 1.0f) - (A - 1.0f) * cw - sqA2);
            break;
        }
        case BandType::Notch:
            normalise(1.0f, -2.0f * cw, 1.0f, 1.0f + alpha, -2.0f * cw, 1.0f - alpha);
            break;
    }
    return out;
}

float processRbj(float x, const RbjCoeffs& c, MagdaEqCompiledPlugin::BiquadState& s) {
    const float y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
    s.x2 = s.x1;
    s.x1 = x;
    s.y2 = s.y1;
    s.y1 = y;
    return y;
}

// Identifier-safe band name used in TE state ids.
juce::String bandIdPrefix(int band) {
    return "band" + juce::String(band + 1);
}

juce::String bandDisplayPrefix(int band) {
    return "Band " + juce::String(band + 1);
}

}  // namespace

MagdaEqCompiledPlugin::MagdaEqCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    buildHostParameters();

    // Persist the "collapse knobs" toggle on the plugin's state ValueTree
    // so it survives project save/load. Defaults to collapsed (true) — the
    // curve is the EQ's primary surface.
    curveCollapsed_.referTo(state, "curveCollapsed", getUndoManager(), true);
}

MagdaEqCompiledPlugin::~MagdaEqCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaEqCompiledPlugin::getName() const {
    return "EQ";
}
juce::String MagdaEqCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaEqCompiledPlugin::getShortName(int) {
    return "EQ";
}
juce::String MagdaEqCompiledPlugin::getSelectableDescription() {
    return "8-Band Equaliser";
}

void MagdaEqCompiledPlugin::rebuildEngineState(int sampleRate) {
    sampleRate_.store(static_cast<double>(sampleRate), std::memory_order_relaxed);
}

void MagdaEqCompiledPlugin::buildHostParameters() {
    // Per-band slots.
    for (int band = 0; band < kBandCount; ++band) {
        const auto& defaults = kBandDefaults[band];
        const juce::String prefix = bandDisplayPrefix(band);

        const int enabledSlot = bandSlot(band, kBandEnabledOffset);
        hostSlotInfo_[enabledSlot] = {.name = prefix + " Enabled",
                                      .scale = magda::ParameterScale::Boolean,
                                      .minValue = 0.0f,
                                      .maxValue = 1.0f,
                                      .defaultValue = defaults.enabled};

        const int typeSlot = bandSlot(band, kBandTypeOffset);
        hostSlotInfo_[typeSlot] = {
            .name = prefix + " Type",
            .scale = magda::ParameterScale::Discrete,
            .minValue = 0.0f,
            .maxValue = static_cast<float>(kBandTypeCount - 1),
            .defaultValue = defaults.type,
            .choices = {"HP", "LowShelf", "Bell", "HighShelf", "LP", "Notch"}};

        const int freqSlot = bandSlot(band, kBandFreqOffset);
        hostSlotInfo_[freqSlot] = {.name = prefix + " Freq",
                                   .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                                   .scale = magda::ParameterScale::Logarithmic,
                                   .minValue = 20.0f,
                                   .maxValue = 20000.0f,
                                   .defaultValue = defaults.freq,
                                   .scaleAnchor = 1000.0f};

        const int gainSlot = bandSlot(band, kBandGainOffset);
        hostSlotInfo_[gainSlot] = {.name = prefix + " Gain",
                                   .unit =
                                       magda::technicalText(magda::TechnicalTextToken::Decibels),
                                   .scale = magda::ParameterScale::Linear,
                                   .minValue = -24.0f,
                                   .maxValue = 24.0f,
                                   .defaultValue = 0.0f};

        const int qSlot = bandSlot(band, kBandQOffset);
        hostSlotInfo_[qSlot] = {.name = prefix + " Q",
                                .scale = magda::ParameterScale::Logarithmic,
                                .minValue = 0.1f,
                                .maxValue = 10.0f,
                                .defaultValue = defaults.q,
                                .scaleAnchor = 1.0f};
    }

    hostSlotInfo_[kOutputSlot] = {.name = "Output",
                                  .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                                  .scale = magda::ParameterScale::Linear,
                                  .minValue = -24.0f,
                                  .maxValue = 12.0f,
                                  .defaultValue = 0.0f};

    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    auto* undoManager = getUndoManager();

    for (int i = 0; i < kHostSlotCount; ++i) {
        const auto& slot = hostSlotInfo_[i];
        juce::String id;
        if (i < kOutputSlot) {
            const int band = i / kSlotsPerBand;
            const int role = i % kSlotsPerBand;
            static const char* kRoleSuffix[kSlotsPerBand] = {"enabled", "filter_type", "freq",
                                                             "gain", "q"};
            id = "magda_eq_" + bandIdPrefix(band) + "_" + kRoleSuffix[role];
        } else {
            id = "magda_eq_output";
        }
        const juce::Identifier identifier(id);
        const auto info = parameterInfoForSlot(slot);
        const float defaultNormalized =
            magda::ParameterUtils::realToNormalized(slot.defaultValue, info);
        hostCached_[i].referTo(state, identifier, undoManager, defaultNormalized);

        auto param = addParam(
            id, slot.name, normalisedRange,
            [info](float normalized) {
                const float real = magda::ParameterUtils::normalizedToReal(normalized, info);
                return magda::ParameterUtils::formatValue(real, info);
            },
            [info](const juce::String& text) {
                auto parsed = magda::ParameterUtils::parseValue(text, info);
                if (parsed)
                    return magda::ParameterUtils::realToNormalized(*parsed, info);
                return 0.0f;
            });
        param->attachToCurrentValue(hostCached_[i]);
        hostParams_[i] = param;
    }
}

void MagdaEqCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));
    preTapScratch_.assign(static_cast<size_t>(juce::jmax(0, info.blockSizeSamples)), 0.0f);
    postTapScratch_.assign(static_cast<size_t>(juce::jmax(0, info.blockSizeSamples)), 0.0f);
    for (auto& bandStates : biquadStates_)
        bandStates.clear();
}

void MagdaEqCompiledPlugin::deinitialise() {
    preTapScratch_.clear();
    postTapScratch_.clear();
    for (auto& bandStates : biquadStates_)
        bandStates.clear();
}

void MagdaEqCompiledPlugin::reset() {
    for (auto& bandStates : biquadStates_)
        for (auto& state : bandStates)
            state = {};
}

float MagdaEqCompiledPlugin::readSlotDisplayValue(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return 0.0f;

    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    if (const auto* param = hostParams_[static_cast<size_t>(slotIndex)].get())
        return magda::ParameterUtils::normalizedToReal(param->getCurrentValue(), info);

    return hostSlotInfo_[static_cast<size_t>(slotIndex)].defaultValue;
}

MagdaEqCompiledPlugin::BandSnapshot MagdaEqCompiledPlugin::readBandSnapshot(int band) const {
    BandSnapshot snap;
    if (band < 0 || band >= kBandCount)
        return snap;

    snap.enabled = readSlotDisplayValue(bandSlot(band, kBandEnabledOffset)) >= 0.5f;
    const int typeIndex = juce::jlimit(
        0, kBandTypeCount - 1,
        static_cast<int>(std::round(readSlotDisplayValue(bandSlot(band, kBandTypeOffset)))));
    snap.type = static_cast<BandType>(typeIndex);
    snap.freq = readSlotDisplayValue(bandSlot(band, kBandFreqOffset));
    snap.gainDb = readSlotDisplayValue(bandSlot(band, kBandGainOffset));
    snap.q = readSlotDisplayValue(bandSlot(band, kBandQOffset));
    return snap;
}

void MagdaEqCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    const int numSamples = fc.bufferNumSamples;
    const int startSample = fc.bufferStartSample;
    const int hostChannels = fc.destBuffer->getNumChannels();
    if (hostChannels <= 0)
        return;

    if (static_cast<int>(preTapScratch_.size()) < numSamples)
        preTapScratch_.resize(static_cast<size_t>(numSamples));
    if (static_cast<int>(postTapScratch_.size()) < numSamples)
        postTapScratch_.resize(static_cast<size_t>(numSamples));

    for (auto& bandStates : biquadStates_)
        if (static_cast<int>(bandStates.size()) < hostChannels)
            bandStates.resize(static_cast<size_t>(hostChannels));

    const float sr = static_cast<float>(sampleRate_.load(std::memory_order_relaxed));
    std::array<bool, kBandCount> bandEnabled{};
    std::array<RbjCoeffs, kBandCount> coeffs{};
    for (int band = 0; band < kBandCount; ++band) {
        const auto snap = readBandSnapshot(band);
        bandEnabled[static_cast<size_t>(band)] = snap.enabled;
        if (!snap.enabled) {
            auto& states = biquadStates_[static_cast<size_t>(band)];
            std::fill(states.begin(), states.end(), BiquadState{});
            continue;
        }
        coeffs[static_cast<size_t>(band)] = makeRbj(snap.type, snap.freq, snap.gainDb, snap.q, sr);
    }
    const float outputGain = std::pow(10.0f, readSlotDisplayValue(kOutputSlot) / 20.0f);

    std::fill_n(preTapScratch_.data(), numSamples, 0.0f);
    for (int ch = 0; ch < hostChannels; ++ch) {
        const float* src = fc.destBuffer->getReadPointer(ch, startSample);
        for (int i = 0; i < numSamples; ++i)
            preTapScratch_[static_cast<size_t>(i)] += src[i];
    }
    const float preInv = 1.0f / static_cast<float>(hostChannels);
    for (int i = 0; i < numSamples; ++i)
        preTapScratch_[static_cast<size_t>(i)] *= preInv;
    preSpectrumTap_.write(preTapScratch_.data(), numSamples);

    // Heavy boosts at high Q can rarely produce non-finite samples during a
    // quick freq/Q sweep. Clamp+sanitise mirrors the other compiled plugins.
    std::fill_n(postTapScratch_.data(), numSamples, 0.0f);
    for (int ch = 0; ch < hostChannels; ++ch) {
        float* out = fc.destBuffer->getWritePointer(ch, startSample);
        for (int i = 0; i < numSamples; ++i) {
            float sample = out[i];
            for (int band = 0; band < kBandCount; ++band) {
                if (!bandEnabled[static_cast<size_t>(band)])
                    continue;
                sample =
                    processRbj(sample, coeffs[static_cast<size_t>(band)],
                               biquadStates_[static_cast<size_t>(band)][static_cast<size_t>(ch)]);
            }
            sample *= outputGain;
            const float sanitized =
                std::isfinite(sample) ? juce::jlimit(-16.0f, 16.0f, sample) : 0.0f;
            out[i] = sanitized;
            postTapScratch_[static_cast<size_t>(i)] += sanitized;
        }
    }
    const float postInv = 1.0f / static_cast<float>(hostChannels);
    for (int i = 0; i < numSamples; ++i)
        postTapScratch_[static_cast<size_t>(i)] *= postInv;
    postSpectrumTap_.write(postTapScratch_.data(), numSamples);
}

te::AutomatableParameter* MagdaEqCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaEqCompiledPlugin::HostSlotInfo& MagdaEqCompiledPlugin::getSlotInfo(int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaEqCompiledPlugin::displayValueToNativeValue(int slotIndex, float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    return magda::ParameterUtils::realToNormalized(displayValue, info);
}

float MagdaEqCompiledPlugin::nativeValueToDisplayValue(int slotIndex, float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
    const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]);
    return magda::ParameterUtils::normalizedToReal(nativeValue, info);
}

MagdaEqCompiledPlugin::BandSnapshot MagdaEqCompiledPlugin::getBandSnapshot(int band) const {
    return readBandSnapshot(band);
}

float MagdaEqCompiledPlugin::getOutputDb() const {
    return readSlotDisplayValue(kOutputSlot);
}

namespace {

// AI-chat aliases — one per slot. The slot index is what gets passed to
// setSlotValue, so the alias just needs to point at the right host slot.
constexpr AliasSpec kAliases[] = {
    {"band1_enabled", 0, "Band 1 Enabled"},
    {"band1_type", 1, "Band 1 Type"},
    {"band1_freq", 2, "Band 1 Freq"},
    {"band1_gain", 3, "Band 1 Gain"},
    {"band1_q", 4, "Band 1 Q"},
    {"band2_enabled", 5, "Band 2 Enabled"},
    {"band2_type", 6, "Band 2 Type"},
    {"band2_freq", 7, "Band 2 Freq"},
    {"band2_gain", 8, "Band 2 Gain"},
    {"band2_q", 9, "Band 2 Q"},
    {"band3_enabled", 10, "Band 3 Enabled"},
    {"band3_type", 11, "Band 3 Type"},
    {"band3_freq", 12, "Band 3 Freq"},
    {"band3_gain", 13, "Band 3 Gain"},
    {"band3_q", 14, "Band 3 Q"},
    {"band4_enabled", 15, "Band 4 Enabled"},
    {"band4_type", 16, "Band 4 Type"},
    {"band4_freq", 17, "Band 4 Freq"},
    {"band4_gain", 18, "Band 4 Gain"},
    {"band4_q", 19, "Band 4 Q"},
    {"band5_enabled", 20, "Band 5 Enabled"},
    {"band5_type", 21, "Band 5 Type"},
    {"band5_freq", 22, "Band 5 Freq"},
    {"band5_gain", 23, "Band 5 Gain"},
    {"band5_q", 24, "Band 5 Q"},
    {"band6_enabled", 25, "Band 6 Enabled"},
    {"band6_type", 26, "Band 6 Type"},
    {"band6_freq", 27, "Band 6 Freq"},
    {"band6_gain", 28, "Band 6 Gain"},
    {"band6_q", 29, "Band 6 Q"},
    {"band7_enabled", 30, "Band 7 Enabled"},
    {"band7_type", 31, "Band 7 Type"},
    {"band7_freq", 32, "Band 7 Freq"},
    {"band7_gain", 33, "Band 7 Gain"},
    {"band7_q", 34, "Band 7 Q"},
    {"band8_enabled", 35, "Band 8 Enabled"},
    {"band8_type", 36, "Band 8 Type"},
    {"band8_freq", 37, "Band 8 Freq"},
    {"band8_gain", 38, "Band 8 Gain"},
    {"band8_q", 39, "Band 8 Q"},
    {"output", 40, "Output"},
};

}  // namespace

const CompiledPluginSpec& getMagdaEqSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaEqCompiledPlugin::xmlTypeName,
        .displayName = "EQ",
        .browserCategory = "EQ",
        .description = "Built-in 8-band parametric equaliser. Per-band Enabled skips inactive "
                       "biquads; Type selects "
                       "<b>HP</b>, <b>LowShelf</b>, <b>Bell</b>, <b>HighShelf</b>, "
                       "<b>LP</b>, or <b>Notch</b>. "
                       "MAGDA-owned RBJ biquads drive audio and share coefficient math with "
                       "the curve view. "
                       "Each band exposes Freq, Gain, Q; Output trims the final sum.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaEqCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
