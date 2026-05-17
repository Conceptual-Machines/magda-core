#include "plugins/compiled/MagdaRingModCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_ring_mod.generated.cpp"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaRingModCompiledPlugin::xmlTypeName = "magda_ring_mod";

namespace {

struct RingModHarvest {
    struct Control {
        int idx = -1;
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        FAUSTFLOAT* zone = nullptr;
        std::vector<std::pair<float, juce::String>> choices;
        int gateSlotIndex = -1;
        bool gateNegated = false;
    };
    std::vector<Control> controls;
};

class RingModHarvester : public ::UI {
  public:
    RingModHarvest harvest;

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

        RingModHarvest::Control c;
        c.idx = merged.slotIndex;
        c.kind = merged.isMenuStyle ? FaustParamSlot::Kind::Discrete : kind;
        c.zone = zone;
        c.choices = merged.menuChoices;
        c.gateSlotIndex = merged.gateSlotIndex;
        c.gateNegated = merged.gateNegated;
        harvest.controls.push_back(std::move(c));
    }

    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

const RingModHarvest::Control* findByIdx(const RingModHarvest& h, int idx) {
    for (const auto& c : h.controls)
        if (c.idx == idx)
            return &c;
    return nullptr;
}

}  // namespace

MagdaRingModCompiledPlugin::MagdaRingModCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    dsp_ = std::make_unique<MagdaRingModDsp>();
    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
}

MagdaRingModCompiledPlugin::~MagdaRingModCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaRingModCompiledPlugin::getName() const {
    return "Ring Mod";
}
juce::String MagdaRingModCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaRingModCompiledPlugin::getShortName(int) {
    return "Ring Mod";
}
juce::String MagdaRingModCompiledPlugin::getSelectableDescription() {
    return "Ring Mod";
}

void MagdaRingModCompiledPlugin::rebuildEngineState(int sampleRate) {
    if (!dsp_)
        return;
    dsp_->init(sampleRate);
    numInputs_ = dsp_->getNumInputs();
    numOutputs_ = dsp_->getNumOutputs();

    RingModHarvester harvester;
    dsp_->buildUserInterface(&harvester);

    zones_.fill(nullptr);
    bpmZone_ = nullptr;
    divisionChoiceValues_.clear();
    divisionChoiceLabels_.clear();

    for (int i = 0; i < kHostSlotCount; ++i) {
        harvestedGates_[static_cast<size_t>(i)] = {-1, false};
        if (auto* c = findByIdx(harvester.harvest, i)) {
            zones_[static_cast<size_t>(i)] = c->zone;
            harvestedGates_[static_cast<size_t>(i)] = {c->gateSlotIndex, c->gateNegated};
        }
    }
    if (auto* c = findByIdx(harvester.harvest, kBpmSlot))
        bpmZone_ = c->zone;

    if (auto* c = findByIdx(harvester.harvest, kDivisionSlot)) {
        auto sorted = c->choices;
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        divisionChoiceValues_.reserve(sorted.size());
        divisionChoiceLabels_.reserve(sorted.size());
        for (const auto& choice : sorted) {
            divisionChoiceValues_.push_back(choice.first);
            divisionChoiceLabels_.push_back(choice.second);
        }
    }
}

void MagdaRingModCompiledPlugin::buildHostParameters() {
    hostSlotInfo_[kSyncSlot] = {.name = "Sync",
                                .scale = magda::ParameterScale::Discrete,
                                .minValue = 0.0f,
                                .maxValue = 1.0f,
                                .defaultValue = 0.0f,
                                .scaleAnchor = std::numeric_limits<float>::quiet_NaN(),
                                .choices = {"Off", "On"}};

    hostSlotInfo_[kFrequencySlot] = {.name = "Frequency",
                                     .unit = "Hz",
                                     .scale = magda::ParameterScale::Logarithmic,
                                     .minValue = 1.0f,
                                     .maxValue = 5000.0f,
                                     .defaultValue = 100.0f,
                                     .scaleAnchor = 200.0f};

    hostSlotInfo_[kDivisionSlot].name = "Division";
    hostSlotInfo_[kDivisionSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kDivisionSlot].minValue = 0.0f;
    hostSlotInfo_[kDivisionSlot].maxValue = 0.0f;
    hostSlotInfo_[kDivisionSlot].defaultValue = 0.0f;

    hostSlotInfo_[kShapeSlot].name = "Shape";
    hostSlotInfo_[kShapeSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kShapeSlot].choices = {"Sine", "Triangle", "Square"};
    hostSlotInfo_[kShapeSlot].minValue = 0.0f;
    hostSlotInfo_[kShapeSlot].maxValue =
        static_cast<float>(hostSlotInfo_[kShapeSlot].choices.size() - 1);
    hostSlotInfo_[kShapeSlot].defaultValue = 0.0f;

    hostSlotInfo_[kMixSlot] = {.name = "Mix",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = 0.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 0.5f};

    hostSlotInfo_[kWidthSlot] = {.name = "Width",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 1.0f,
                                 .defaultValue = 0.5f};

    hostSlotInfo_[kSourceSlot].name = "Source";
    hostSlotInfo_[kSourceSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kSourceSlot].choices = {"Oscillator", "Sidechain"};
    hostSlotInfo_[kSourceSlot].minValue = 0.0f;
    hostSlotInfo_[kSourceSlot].maxValue =
        static_cast<float>(hostSlotInfo_[kSourceSlot].choices.size() - 1);
    hostSlotInfo_[kSourceSlot].defaultValue = 0.0f;

    if (!divisionChoiceValues_.empty()) {
        const int n = static_cast<int>(divisionChoiceValues_.size());
        hostSlotInfo_[kDivisionSlot].choices = divisionChoiceLabels_;
        hostSlotInfo_[kDivisionSlot].maxValue = static_cast<float>(n - 1);
        for (int i = 0; i < n; ++i) {
            if (std::abs(divisionChoiceValues_[static_cast<size_t>(i)] - 1.0f) < 1e-3f) {
                hostSlotInfo_[kDivisionSlot].defaultValue = static_cast<float>(i);
                break;
            }
        }
    }

    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    auto* undoManager = getUndoManager();

    auto buildInfo = [](const HostSlotInfo& s) {
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
    };

    for (int i = 0; i < kHostSlotCount; ++i) {
        const auto& slot = hostSlotInfo_[i];
        const juce::String id = "magda_ring_mod_" + slot.name.toLowerCase().replace(" ", "_");
        const juce::Identifier identifier(id);
        const auto info = buildInfo(slot);
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

    for (int i = 0; i < kHostSlotCount; ++i) {
        hostSlotInfo_[static_cast<size_t>(i)].gateSlotIndex =
            harvestedGates_[static_cast<size_t>(i)].slotIndex;
        hostSlotInfo_[static_cast<size_t>(i)].gateNegated =
            harvestedGates_[static_cast<size_t>(i)].negated;
    }
}

void MagdaRingModCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));
    scratchIn_.setSize(numInputs_, info.blockSizeSamples, false, true, true);
    scratchOut_.setSize(numOutputs_, info.blockSizeSamples, false, true, true);
    inPtrs_.assign(static_cast<size_t>(numInputs_), nullptr);
    outPtrs_.assign(static_cast<size_t>(numOutputs_), nullptr);
}

void MagdaRingModCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaRingModCompiledPlugin::reset() {
    if (dsp_)
        dsp_->instanceClear();
}

void MagdaRingModCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0 || !dsp_)
        return;

    if (fc.isPlaying && !wasPlaying_)
        dsp_->instanceClear();
    wasPlaying_ = fc.isPlaying;

    auto writeContinuous = [&](int slot) {
        if (auto* zone = zones_[static_cast<size_t>(slot)]) {
            const auto& s = hostSlotInfo_[static_cast<size_t>(slot)];
            magda::ParameterInfo info;
            info.minValue = s.minValue;
            info.maxValue = s.maxValue;
            info.scale = s.scale;
            if (std::isfinite(s.scaleAnchor))
                info.scaleAnchor = s.scaleAnchor;
            const float norm = hostParams_[static_cast<size_t>(slot)]->getCurrentValue();
            *zone = static_cast<FAUSTFLOAT>(magda::ParameterUtils::normalizedToReal(norm, info));
        }
    };
    writeContinuous(kFrequencySlot);
    writeContinuous(kMixSlot);
    writeContinuous(kWidthSlot);

    auto writeMenuIndex = [&](int slot) {
        if (auto* z = zones_[static_cast<size_t>(slot)]) {
            const auto& s = hostSlotInfo_[static_cast<size_t>(slot)];
            const float norm = hostParams_[static_cast<size_t>(slot)]->getCurrentValue();
            const int count = static_cast<int>(s.choices.size());
            const int idx =
                count > 1 ? juce::jlimit(
                                0, count - 1,
                                static_cast<int>(std::round(norm * static_cast<float>(count - 1))))
                          : 0;
            *z = static_cast<FAUSTFLOAT>(idx);
        }
    };
    writeMenuIndex(kShapeSlot);
    writeMenuIndex(kSourceSlot);

    if (auto* z = zones_[kSyncSlot])
        *z = hostParams_[kSyncSlot]->getCurrentValue() >= 0.5f ? FAUSTFLOAT(1) : FAUSTFLOAT(0);

    if (auto* z = zones_[kDivisionSlot]; z && !divisionChoiceValues_.empty()) {
        const float norm = hostParams_[kDivisionSlot]->getCurrentValue();
        const int count = static_cast<int>(divisionChoiceValues_.size());
        const int idx = juce::jlimit(
            0, count - 1, static_cast<int>(std::round(norm * static_cast<float>(count - 1))));
        *z = static_cast<FAUSTFLOAT>(divisionChoiceValues_[static_cast<size_t>(idx)]);
    }

    const double bpm = edit.tempoSequence.getBpmAt(fc.editTime.getStart());
    currentBpm_.store(static_cast<float>(bpm), std::memory_order_relaxed);
    if (bpmZone_)
        *bpmZone_ = static_cast<FAUSTFLOAT>(bpm);

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

    const int channelsToSanitise = std::min(hostChannels, numOutputs_);
    for (int ch = 0; ch < channelsToSanitise; ++ch) {
        float* out = fc.destBuffer->getWritePointer(ch, startSample);
        for (int i = 0; i < numSamples; ++i) {
            const float sample = out[i];
            out[i] = std::isfinite(sample) ? juce::jlimit(-16.0f, 16.0f, sample) : 0.0f;
        }
    }
}

te::AutomatableParameter* MagdaRingModCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaRingModCompiledPlugin::HostSlotInfo& MagdaRingModCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaRingModCompiledPlugin::divisionFaustValueForIndex(int index) const {
    if (divisionChoiceValues_.empty())
        return 1.0f;
    const int safeIdx = juce::jlimit(0, static_cast<int>(divisionChoiceValues_.size()) - 1, index);
    return divisionChoiceValues_[static_cast<size_t>(safeIdx)];
}

float MagdaRingModCompiledPlugin::displayValueToNativeValue(int slotIndex,
                                                            float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
    const auto& s = hostSlotInfo_[static_cast<size_t>(slotIndex)];
    magda::ParameterInfo info;
    info.minValue = s.minValue;
    info.maxValue = s.maxValue;
    info.scale = s.scale;
    if (std::isfinite(s.scaleAnchor))
        info.scaleAnchor = s.scaleAnchor;
    info.choices = s.choices;
    return magda::ParameterUtils::realToNormalized(displayValue, info);
}

float MagdaRingModCompiledPlugin::nativeValueToDisplayValue(int slotIndex,
                                                            float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
    const auto& s = hostSlotInfo_[static_cast<size_t>(slotIndex)];
    magda::ParameterInfo info;
    info.minValue = s.minValue;
    info.maxValue = s.maxValue;
    info.scale = s.scale;
    if (std::isfinite(s.scaleAnchor))
        info.scaleAnchor = s.scaleAnchor;
    info.choices = s.choices;
    return magda::ParameterUtils::normalizedToReal(nativeValue, info);
}

constexpr AliasSpec kAliases[] = {
    {"sync", 0, "Sync"}, {"freq", 1, "Frequency"}, {"div", 2, "Division"},  {"shape", 3, "Shape"},
    {"mix", 4, "Mix"},   {"width", 5, "Width"},    {"source", 6, "Source"},
};

const CompiledPluginSpec& getMagdaRingModSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaRingModCompiledPlugin::xmlTypeName,
        .displayName = "Ring Mod",
        .browserCategory = "Modulation",
        .description =
            "Compiled Faust stereo ring modulator. Multiplies the input by an internal carrier "
            "from 1 Hz (slow tremolo) to 5 kHz (metallic clang).\n"
            "<b>Sine</b>: pure tonal carrier, cleanest sideband structure.\n"
            "<b>Triangle</b>: softer overtone series than square.\n"
            "<b>Square</b>: rich odd-harmonic spectrum, aggressive sideband stack.\n"
            "Rate runs free in Hz or locks to tempo Division. "
            "Width offsets the carrier phase per channel; Mix blends wet against dry.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaRingModCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
