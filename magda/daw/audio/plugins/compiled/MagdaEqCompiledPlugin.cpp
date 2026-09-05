#include "plugins/compiled/MagdaEqCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>

#include "core/ParameterInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaEqCompiledPlugin::xmlTypeName = "magda_eq";

namespace {

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

MagdaEqCompiledPlugin::MagdaEqCompiledPlugin() {
    initEffect();
}

std::vector<MagdaEqCompiledPlugin::HostSlotInfo> MagdaEqCompiledPlugin::slotInfos() const {
    std::vector<HostSlotInfo> infos(kHostSlotCount);
    // Per-band slots.
    for (int band = 0; band < kBandCount; ++band) {
        const auto& defaults = kBandDefaults[band];
        const juce::String prefix = bandDisplayPrefix(band);

        const int enabledSlot = bandSlot(band, kBandEnabledOffset);
        infos[enabledSlot] = {.name = prefix + " Enabled",
                              .scale = magda::ParameterScale::Boolean,
                              .minValue = 0.0f,
                              .maxValue = 1.0f,
                              .defaultValue = defaults.enabled};

        const int typeSlot = bandSlot(band, kBandTypeOffset);
        infos[typeSlot] = {.name = prefix + " Type",
                           .scale = magda::ParameterScale::Discrete,
                           .minValue = 0.0f,
                           .maxValue = static_cast<float>(kBandTypeCount - 1),
                           .defaultValue = defaults.type,
                           .choices = {"HP", "LowShelf", "Bell", "HighShelf", "LP", "Notch"}};

        const int freqSlot = bandSlot(band, kBandFreqOffset);
        infos[freqSlot] = {.name = prefix + " Freq",
                           .unit = magda::technicalText(magda::TechnicalTextToken::Hertz),
                           .scale = magda::ParameterScale::Logarithmic,
                           .minValue = 20.0f,
                           .maxValue = 20000.0f,
                           .defaultValue = defaults.freq,
                           .scaleAnchor = 1000.0f};

        const int gainSlot = bandSlot(band, kBandGainOffset);
        infos[gainSlot] = {.name = prefix + " Gain",
                           .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                           .scale = magda::ParameterScale::Linear,
                           .minValue = -24.0f,
                           .maxValue = 24.0f,
                           .defaultValue = 0.0f};

        const int qSlot = bandSlot(band, kBandQOffset);
        infos[qSlot] = {.name = prefix + " Q",
                        .scale = magda::ParameterScale::Logarithmic,
                        .minValue = 0.1f,
                        .maxValue = 10.0f,
                        .defaultValue = defaults.q,
                        .scaleAnchor = 1.0f};
    }

    infos[kOutputSlot] = {.name = "Output",
                          .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                          .scale = magda::ParameterScale::Linear,
                          .minValue = -24.0f,
                          .maxValue = 12.0f,
                          .defaultValue = 0.0f};

    return infos;
}

juce::String MagdaEqCompiledPlugin::slotId(int slotIndex) const {
    // Pinned rather than derived from the slot name: these ids key eight bands
    // of saved state, and "band3_filter_type" is not what the default scheme
    // would make of "Band 3 Type".
    if (slotIndex < 0 || slotIndex > kOutputSlot)
        return {};
    if (slotIndex == kOutputSlot)
        return "magda_eq_output";

    static const char* kRoleSuffix[kSlotsPerBand] = {"enabled", "filter_type", "freq", "gain", "q"};
    return "magda_eq_" + bandIdPrefix(slotIndex / kSlotsPerBand) + "_" +
           kRoleSuffix[slotIndex % kSlotsPerBand];
}

void MagdaEqCompiledPlugin::flushState(juce::ValueTree& state) {
    state.setProperty("curveCollapsed", curveCollapsed_, nullptr);
}

void MagdaEqCompiledPlugin::restoreState(const juce::ValueTree& state) {
    curveCollapsed_ = static_cast<bool>(state.getProperty("curveCollapsed", true));
}

MagdaEqCompiledPlugin::BandSnapshot MagdaEqCompiledPlugin::getBandSnapshot(int band) const {
    BandSnapshot snapshot;
    if (band < 0 || band >= kBandCount)
        return snapshot;

    snapshot.enabled = slotDisplayValue(bandSlot(band, kBandEnabledOffset)) >= 0.5f;
    const int typeIndex = juce::jlimit(
        0, kBandTypeCount - 1,
        static_cast<int>(std::lround(slotDisplayValue(bandSlot(band, kBandTypeOffset)))));
    snapshot.type = static_cast<BandType>(typeIndex);
    snapshot.freq = slotDisplayValue(bandSlot(band, kBandFreqOffset));
    snapshot.gainDb = slotDisplayValue(bandSlot(band, kBandGainOffset));
    snapshot.q = slotDisplayValue(bandSlot(band, kBandQOffset));
    return snapshot;
}

void MagdaEqCompiledPlugin::onPrepare(double, int maximumBlockSize) {
    preTapScratch_.assign(static_cast<size_t>(std::max(0, maximumBlockSize)), 0.0f);
    postTapScratch_.assign(static_cast<size_t>(std::max(0, maximumBlockSize)), 0.0f);
    for (auto& bandStates : biquadStates_)
        bandStates.clear();
}

void MagdaEqCompiledPlugin::onRelease() {
    preTapScratch_.clear();
    postTapScratch_.clear();
    for (auto& bandStates : biquadStates_)
        bandStates.clear();
}

void MagdaEqCompiledPlugin::onReset() {
    for (auto& bandStates : biquadStates_)
        for (auto& state : bandStates)
            state = {};
}

void MagdaEqCompiledPlugin::processAudio(DeviceProcessContext& context) {
    // No Faust engine: the bands are MAGDA-owned RBJ biquads so the audible
    // response and the curve view share one piece of coefficient maths.
    const int numSamples = context.numSamples;
    const int startSample = context.startSample;
    const int hostChannels = context.audio->getNumChannels();
    if (hostChannels <= 0)
        return;

    if (static_cast<int>(preTapScratch_.size()) < numSamples)
        preTapScratch_.resize(static_cast<size_t>(numSamples));
    if (static_cast<int>(postTapScratch_.size()) < numSamples)
        postTapScratch_.resize(static_cast<size_t>(numSamples));

    for (auto& bandStates : biquadStates_)
        if (static_cast<int>(bandStates.size()) < hostChannels)
            bandStates.resize(static_cast<size_t>(hostChannels));

    const auto sampleRate = static_cast<float>(currentSampleRate());
    std::array<bool, kBandCount> bandEnabled{};
    std::array<RbjCoeffs, kBandCount> coeffs{};
    for (int band = 0; band < kBandCount; ++band) {
        const auto snapshot = getBandSnapshot(band);
        bandEnabled[static_cast<size_t>(band)] = snapshot.enabled;
        if (!snapshot.enabled) {
            auto& states = biquadStates_[static_cast<size_t>(band)];
            std::fill(states.begin(), states.end(), BiquadState{});
            continue;
        }
        coeffs[static_cast<size_t>(band)] =
            makeRbj(snapshot.type, snapshot.freq, snapshot.gainDb, snapshot.q, sampleRate);
    }
    const float outputGain = std::pow(10.0f, slotDisplayValue(kOutputSlot) / 20.0f);

    std::fill_n(preTapScratch_.data(), numSamples, 0.0f);
    for (int channel = 0; channel < hostChannels; ++channel) {
        const float* source = context.audio->getReadPointer(channel, startSample);
        for (int i = 0; i < numSamples; ++i)
            preTapScratch_[static_cast<size_t>(i)] += source[i];
    }
    const float channelInverse = 1.0f / static_cast<float>(hostChannels);
    for (int i = 0; i < numSamples; ++i)
        preTapScratch_[static_cast<size_t>(i)] *= channelInverse;
    preSpectrumTap_.write(preTapScratch_.data(), numSamples);

    // Heavy boosts at high Q can rarely produce non-finite samples during a
    // quick freq/Q sweep, so the output goes through the same sanitising pass
    // every compiled device applies.
    std::fill_n(postTapScratch_.data(), numSamples, 0.0f);
    for (int channel = 0; channel < hostChannels; ++channel) {
        float* out = context.audio->getWritePointer(channel, startSample);
        for (int i = 0; i < numSamples; ++i) {
            float sample = out[i];
            for (int band = 0; band < kBandCount; ++band) {
                if (!bandEnabled[static_cast<size_t>(band)])
                    continue;
                sample = processRbj(
                    sample, coeffs[static_cast<size_t>(band)],
                    biquadStates_[static_cast<size_t>(band)][static_cast<size_t>(channel)]);
            }
            const float sanitized = sanitise(sample * outputGain);
            out[i] = sanitized;
            postTapScratch_[static_cast<size_t>(i)] += sanitized;
        }
    }
    for (int i = 0; i < numSamples; ++i)
        postTapScratch_[static_cast<size_t>(i)] *= channelInverse;
    postSpectrumTap_.write(postTapScratch_.data(), numSamples);
}

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

// Tracktion's retired 4-band EQ loads here; see core/LegacyDeviceAliases.hpp.
constexpr const char* kLoadAliases[] = {"4bandEq", "eq", "equaliser"};

const CompiledPluginSpec& getMagdaEqSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaEqCompiledPlugin::xmlTypeName,
        .displayName = "EQ",
        .browserCategory = "EQ",
        .description = "Built-in 8-band parametric equaliser. Per-band Enabled skips inactive "
                       "biquads, and double-clicking a band's dot on the curve toggles it; "
                       "Type selects "
                       "<b>HP</b>, <b>LowShelf</b>, <b>Bell</b>, <b>HighShelf</b>, "
                       "<b>LP</b>, or <b>Notch</b>. "
                       "MAGDA-owned RBJ biquads drive audio and share coefficient math with "
                       "the curve view. "
                       "Each band exposes Freq, Gain, Q; Output trims the final sum.",
        .createDevice = [](const DevicePluginCreationContext&) -> std::unique_ptr<MagdaDevice> {
            return std::make_unique<MagdaEqCompiledPlugin>();
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
        .loadAliases = kLoadAliases,
        .loadAliasCount = static_cast<int>(sizeof(kLoadAliases) / sizeof(kLoadAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
