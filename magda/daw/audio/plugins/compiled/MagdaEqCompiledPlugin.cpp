#include "plugins/compiled/MagdaEqCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_eq.generated.cpp"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaEqCompiledPlugin::xmlTypeName = "magda_eq";

namespace {

// idx-based harvest, identical shape to the other single-engine compiled
// plugins (multiband / clipper / etc).
struct EqHarvest {
    struct Control {
        int idx = -1;
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        FAUSTFLOAT* zone = nullptr;
        std::vector<std::pair<float, juce::String>> choices;
    };
    std::vector<Control> controls;
};

class EqHarvester : public ::UI {
  public:
    EqHarvest harvest;

    void openTabBox(const char*) override {}
    void openHorizontalBox(const char*) override {}
    void openVerticalBox(const char*) override {}
    void closeBox() override {}

    void addButton(const char* label, FAUSTFLOAT* zone) override {
        emitControl(FaustParamSlot::Kind::Boolean, label, zone);
    }
    void addCheckButton(const char* label, FAUSTFLOAT* zone) override {
        emitControl(FaustParamSlot::Kind::Boolean, label, zone);
    }
    void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT,
                           FAUSTFLOAT) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone);
    }
    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT,
                             FAUSTFLOAT, FAUSTFLOAT) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone);
    }
    void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT,
                     FAUSTFLOAT) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone);
    }
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}

    void declare(FAUSTFLOAT* zone, const char* key, const char* value) override {
        if (zone == nullptr)
            return;
        const auto k = juce::String::fromUTF8(key != nullptr ? key : "").toLowerCase();
        const auto v = juce::String::fromUTF8(value != nullptr ? value : "");
        applyFaustAnnotation(k, v, pendingByZone_[zone]);
    }

  private:
    void emitControl(FaustParamSlot::Kind kind, const char* rawLabel, FAUSTFLOAT* zone) {
        const auto parsed =
            parseFaustLabel(juce::String::fromUTF8(rawLabel != nullptr ? rawLabel : ""));
        ControlMetadata merged = parsed.metadata;
        if (zone != nullptr) {
            if (auto it = pendingByZone_.find(zone); it != pendingByZone_.end()) {
                mergeFaustMetadata(merged, it->second);
                pendingByZone_.erase(it);
            }
        }

        EqHarvest::Control c;
        c.idx = merged.slotIndex;
        c.kind = merged.isMenuStyle ? FaustParamSlot::Kind::Discrete : kind;
        c.zone = zone;
        c.choices = merged.menuChoices;
        harvest.controls.push_back(std::move(c));
    }

    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

const EqHarvest::Control* findByIdx(const EqHarvest& h, int idx) {
    for (const auto& c : h.controls)
        if (c.idx == idx)
            return &c;
    return nullptr;
}

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

// Per-band defaults — keep the audio thread and the curve view aligned by
// reading these in both places.
struct BandDefaults {
    float type;
    float freq;
    float q;
};
constexpr BandDefaults kBandDefaults[MagdaEqCompiledPlugin::kBandCount] = {
    {0.0f, 30.0f, 0.707f},     // HP
    {1.0f, 100.0f, 0.707f},    // LowShelf
    {2.0f, 250.0f, 1.0f},      // Bell
    {2.0f, 800.0f, 1.0f},      // Bell
    {2.0f, 2000.0f, 1.0f},     // Bell
    {2.0f, 5000.0f, 1.0f},     // Bell
    {3.0f, 10000.0f, 0.707f},  // HighShelf
    {4.0f, 18000.0f, 0.707f},  // LP
};

// Identifier-safe band name used in TE state ids ("magda_eq_band1_type").
juce::String bandIdPrefix(int band) {
    return "band" + juce::String(band + 1);
}

juce::String bandDisplayPrefix(int band) {
    return "Band " + juce::String(band + 1);
}

}  // namespace

MagdaEqCompiledPlugin::MagdaEqCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    dsp_ = std::make_unique<MagdaEqDsp>();
    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
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
    if (!dsp_)
        return;
    dsp_->init(sampleRate);
    numInputs_ = dsp_->getNumInputs();
    numOutputs_ = dsp_->getNumOutputs();

    EqHarvester harvester;
    dsp_->buildUserInterface(&harvester);

    zones_.fill(nullptr);
    for (int i = 0; i < kHostSlotCount; ++i) {
        if (auto* c = findByIdx(harvester.harvest, i))
            zones_[static_cast<size_t>(i)] = c->zone;
    }
}

void MagdaEqCompiledPlugin::buildHostParameters() {
    // Per-band slots.
    for (int band = 0; band < kBandCount; ++band) {
        const auto& defaults = kBandDefaults[band];
        const juce::String prefix = bandDisplayPrefix(band);

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
                                   .unit = "Hz",
                                   .scale = magda::ParameterScale::Logarithmic,
                                   .minValue = 20.0f,
                                   .maxValue = 20000.0f,
                                   .defaultValue = defaults.freq,
                                   .scaleAnchor = 1000.0f};

        const int gainSlot = bandSlot(band, kBandGainOffset);
        hostSlotInfo_[gainSlot] = {.name = prefix + " Gain",
                                   .unit = "dB",
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
                                  .unit = "dB",
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
            static const char* kRoleSuffix[kSlotsPerBand] = {"type", "freq", "gain", "q"};
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
    scratchIn_.setSize(numInputs_, info.blockSizeSamples, false, true, true);
    scratchOut_.setSize(numOutputs_, info.blockSizeSamples, false, true, true);
    inPtrs_.assign(static_cast<size_t>(numInputs_), nullptr);
    outPtrs_.assign(static_cast<size_t>(numOutputs_), nullptr);
}

void MagdaEqCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaEqCompiledPlugin::reset() {
    if (dsp_)
        dsp_->instanceClear();
}

void MagdaEqCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0 || !dsp_)
        return;

    for (int i = 0; i < kHostSlotCount; ++i) {
        if (auto* zone = zones_[static_cast<size_t>(i)]) {
            const auto& s = hostSlotInfo_[static_cast<size_t>(i)];
            const auto info = parameterInfoForSlot(s);
            const float norm = hostParams_[static_cast<size_t>(i)]->getCurrentValue();
            *zone = static_cast<FAUSTFLOAT>(magda::ParameterUtils::normalizedToReal(norm, info));
        }
    }

    const int numSamples = fc.bufferNumSamples;
    const int startSample = fc.bufferStartSample;
    const int hostChannels = fc.destBuffer->getNumChannels();
    if (hostChannels <= 0 || numInputs_ <= 0 || numOutputs_ <= 0)
        return;

    if (scratchIn_.getNumChannels() < numInputs_ || scratchIn_.getNumSamples() < numSamples)
        scratchIn_.setSize(numInputs_, numSamples, false, true, true);
    if (scratchOut_.getNumChannels() < numOutputs_ || scratchOut_.getNumSamples() < numSamples)
        scratchOut_.setSize(numOutputs_, numSamples, false, true, true);
    if (static_cast<int>(inPtrs_.size()) < numInputs_)
        inPtrs_.resize(static_cast<size_t>(numInputs_), nullptr);
    if (static_cast<int>(outPtrs_.size()) < numOutputs_)
        outPtrs_.resize(static_cast<size_t>(numOutputs_), nullptr);

    for (int ch = 0; ch < numInputs_; ++ch) {
        float* dst = scratchIn_.getWritePointer(ch);
        if (ch < hostChannels) {
            const float* src = fc.destBuffer->getReadPointer(ch, startSample);
            std::copy(src, src + numSamples, dst);
        } else {
            std::fill(dst, dst + numSamples, 0.0f);
        }
        inPtrs_[static_cast<size_t>(ch)] = dst;
    }
    for (int ch = 0; ch < numOutputs_; ++ch) {
        outPtrs_[static_cast<size_t>(ch)] = (ch < hostChannels)
                                                ? fc.destBuffer->getWritePointer(ch, startSample)
                                                : scratchOut_.getWritePointer(ch);
    }

    dsp_->compute(numSamples, inPtrs_.data(), outPtrs_.data());

    // Heavy boosts at high Q can rarely produce non-finite samples during a
    // quick freq/Q sweep. Clamp+sanitise mirrors the other compiled plugins.
    const int channelsToSanitise = std::min(hostChannels, numOutputs_);
    for (int ch = 0; ch < channelsToSanitise; ++ch) {
        float* out = fc.destBuffer->getWritePointer(ch, startSample);
        for (int i = 0; i < numSamples; ++i) {
            const float sample = out[i];
            out[i] = std::isfinite(sample) ? juce::jlimit(-16.0f, 16.0f, sample) : 0.0f;
        }
    }
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
    BandSnapshot snap;
    if (band < 0 || band >= kBandCount)
        return snap;

    auto realFor = [this](int slot) {
        const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slot)]);
        const float norm = hostParams_[static_cast<size_t>(slot)]
                               ? hostParams_[static_cast<size_t>(slot)]->getCurrentValue()
                               : 0.0f;
        return magda::ParameterUtils::normalizedToReal(norm, info);
    };

    const int typeIndex =
        juce::jlimit(0, kBandTypeCount - 1,
                     static_cast<int>(std::round(realFor(bandSlot(band, kBandTypeOffset)))));
    snap.type = static_cast<BandType>(typeIndex);
    snap.freq = realFor(bandSlot(band, kBandFreqOffset));
    snap.gainDb = realFor(bandSlot(band, kBandGainOffset));
    snap.q = realFor(bandSlot(band, kBandQOffset));
    return snap;
}

float MagdaEqCompiledPlugin::getOutputDb() const {
    if (!hostParams_[kOutputSlot])
        return 0.0f;
    const auto info = parameterInfoForSlot(hostSlotInfo_[kOutputSlot]);
    return magda::ParameterUtils::normalizedToReal(hostParams_[kOutputSlot]->getCurrentValue(),
                                                   info);
}

namespace {

// AI-chat aliases — one per slot. The slot index is what gets passed to
// setSlotValue, so the alias just needs to point at the right host slot.
constexpr AliasSpec kAliases[] = {
    {"band1_type", 0, "Band 1 Type"},  {"band1_freq", 1, "Band 1 Freq"},
    {"band1_gain", 2, "Band 1 Gain"},  {"band1_q", 3, "Band 1 Q"},
    {"band2_type", 4, "Band 2 Type"},  {"band2_freq", 5, "Band 2 Freq"},
    {"band2_gain", 6, "Band 2 Gain"},  {"band2_q", 7, "Band 2 Q"},
    {"band3_type", 8, "Band 3 Type"},  {"band3_freq", 9, "Band 3 Freq"},
    {"band3_gain", 10, "Band 3 Gain"}, {"band3_q", 11, "Band 3 Q"},
    {"band4_type", 12, "Band 4 Type"}, {"band4_freq", 13, "Band 4 Freq"},
    {"band4_gain", 14, "Band 4 Gain"}, {"band4_q", 15, "Band 4 Q"},
    {"band5_type", 16, "Band 5 Type"}, {"band5_freq", 17, "Band 5 Freq"},
    {"band5_gain", 18, "Band 5 Gain"}, {"band5_q", 19, "Band 5 Q"},
    {"band6_type", 20, "Band 6 Type"}, {"band6_freq", 21, "Band 6 Freq"},
    {"band6_gain", 22, "Band 6 Gain"}, {"band6_q", 23, "Band 6 Q"},
    {"band7_type", 24, "Band 7 Type"}, {"band7_freq", 25, "Band 7 Freq"},
    {"band7_gain", 26, "Band 7 Gain"}, {"band7_q", 27, "Band 7 Q"},
    {"band8_type", 28, "Band 8 Type"}, {"band8_freq", 29, "Band 8 Freq"},
    {"band8_gain", 30, "Band 8 Gain"}, {"band8_q", 31, "Band 8 Q"},
    {"output", 32, "Output"},
};

}  // namespace

const CompiledPluginSpec& getMagdaEqSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaEqCompiledPlugin::xmlTypeName,
        .displayName = "EQ",
        .browserCategory = "EQ",
        .description = "Compiled Faust 8-band parametric equaliser. Per-band Type selects "
                       "<b>HP</b>, <b>LowShelf</b>, <b>Bell</b>, <b>HighShelf</b>, "
                       "<b>LP</b>, or <b>Notch</b>. "
                       "All six filter shapes are instantiated in parallel per band, so Type "
                       "switching is glitch-free at audio rate. "
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
