#include "plugins/compiled/MagdaGateExpanderCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_gate_expander.generated.cpp"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaGateExpanderCompiledPlugin::xmlTypeName = "magda_gate_expander";

namespace {

struct GateHarvest {
    struct Control {
        int idx = -1;
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        FAUSTFLOAT* zone = nullptr;
    };
    std::vector<Control> controls;
};

class GateHarvester : public ::UI {
  public:
    GateHarvest harvest;

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

        harvest.controls.push_back(
            {merged.slotIndex, merged.isMenuStyle ? FaustParamSlot::Kind::Discrete : kind, zone});
    }

    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

const GateHarvest::Control* findByIdx(const GateHarvest& h, int idx) {
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

}  // namespace

MagdaGateExpanderCompiledPlugin::MagdaGateExpanderCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    dsp_ = std::make_unique<MagdaGateExpanderDsp>();
    rebuildEngineState(44100);
    buildHostParameters();
}

MagdaGateExpanderCompiledPlugin::~MagdaGateExpanderCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaGateExpanderCompiledPlugin::getName() const {
    return "Gate";
}
juce::String MagdaGateExpanderCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaGateExpanderCompiledPlugin::getShortName(int) {
    return "Gate";
}
juce::String MagdaGateExpanderCompiledPlugin::getSelectableDescription() {
    return "Gate";
}

void MagdaGateExpanderCompiledPlugin::rebuildEngineState(int sampleRate) {
    if (!dsp_)
        return;
    dsp_->init(sampleRate);
    numInputs_ = dsp_->getNumInputs();
    numOutputs_ = dsp_->getNumOutputs();

    GateHarvester harvester;
    dsp_->buildUserInterface(&harvester);

    zones_.fill(nullptr);
    for (int i = 0; i < kHostSlotCount; ++i) {
        if (auto* c = findByIdx(harvester.harvest, i))
            zones_[static_cast<size_t>(i)] = c->zone;
    }
}

void MagdaGateExpanderCompiledPlugin::buildHostParameters() {
    hostSlotInfo_[kAttackSlot] = {.name = "Attack",
                                  .unit =
                                      magda::technicalText(magda::TechnicalTextToken::Milliseconds),
                                  .scale = magda::ParameterScale::Logarithmic,
                                  .minValue = 0.1f,
                                  .maxValue = 100.0f,
                                  .defaultValue = 1.0f,
                                  .scaleAnchor = 1.0f};
    hostSlotInfo_[kReleaseSlot] = {
        .name = "Release",
        .unit = magda::technicalText(magda::TechnicalTextToken::Milliseconds),
        .scale = magda::ParameterScale::Logarithmic,
        .minValue = 5.0f,
        .maxValue = 1000.0f,
        .defaultValue = 120.0f,
        .scaleAnchor = 100.0f};
    hostSlotInfo_[kMixSlot] = {.name = "Mix",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = 0.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 1.0f};
    hostSlotInfo_[kOutputSlot] = {.name = "Output",
                                  .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                                  .scale = magda::ParameterScale::Linear,
                                  .minValue = -24.0f,
                                  .maxValue = 24.0f,
                                  .defaultValue = 0.0f};
    hostSlotInfo_[kThresholdSlot] = {.name = "Threshold",
                                     .unit =
                                         magda::technicalText(magda::TechnicalTextToken::Decibels),
                                     .scale = magda::ParameterScale::Linear,
                                     .minValue = -80.0f,
                                     .maxValue = 0.0f,
                                     .defaultValue = -40.0f};
    hostSlotInfo_[kRatioSlot] = {.name = "Ratio",
                                 .scale = magda::ParameterScale::Logarithmic,
                                 .minValue = 1.0f,
                                 .maxValue = 50.0f,
                                 .defaultValue = 4.0f,
                                 .scaleAnchor = 4.0f};
    hostSlotInfo_[kRangeSlot] = {.name = "Range",
                                 .unit = magda::technicalText(magda::TechnicalTextToken::Decibels),
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 80.0f,
                                 .defaultValue = 60.0f};

    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    auto* undoManager = getUndoManager();

    for (int i = 0; i < kHostSlotCount; ++i) {
        const auto& slot = hostSlotInfo_[static_cast<size_t>(i)];
        const juce::String id = "magda_gate_expander_" + slot.name.toLowerCase().replace(" ", "_");
        const auto info = parameterInfoForSlot(slot);
        const float defaultNormalised =
            magda::ParameterUtils::realToNormalized(slot.defaultValue, info);
        hostCached_[static_cast<size_t>(i)].referTo(state, juce::Identifier(id), undoManager,
                                                    defaultNormalised);

        auto param = addParam(
            id, slot.name, normalisedRange,
            [info](float normalised) {
                const float real = magda::ParameterUtils::normalizedToReal(normalised, info);
                return magda::ParameterUtils::formatValue(real, info);
            },
            [info](const juce::String& text) {
                auto parsed = magda::ParameterUtils::parseValue(text, info);
                if (parsed)
                    return magda::ParameterUtils::realToNormalized(*parsed, info);
                return 0.0f;
            });
        param->attachToCurrentValue(hostCached_[static_cast<size_t>(i)]);
        hostParams_[static_cast<size_t>(i)] = param;
    }
}

void MagdaGateExpanderCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));
    scratchIn_.setSize(numInputs_, info.blockSizeSamples, false, true, true);
    scratchOut_.setSize(numOutputs_, info.blockSizeSamples, false, true, true);
    inPtrs_.assign(static_cast<size_t>(numInputs_), nullptr);
    outPtrs_.assign(static_cast<size_t>(numOutputs_), nullptr);
}

void MagdaGateExpanderCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaGateExpanderCompiledPlugin::reset() {
    if (dsp_)
        dsp_->instanceClear();
}

void MagdaGateExpanderCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0 || !dsp_)
        return;

    for (int slot = 0; slot < kHostSlotCount; ++slot) {
        if (auto* zone = zones_[static_cast<size_t>(slot)]) {
            const auto info = parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slot)]);
            const float norm = hostParams_[static_cast<size_t>(slot)]->getCurrentValue();
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

    const int channelsToSanitise = std::min(hostChannels, numOutputs_);
    for (int ch = 0; ch < channelsToSanitise; ++ch) {
        float* out = fc.destBuffer->getWritePointer(ch, startSample);
        for (int i = 0; i < numSamples; ++i) {
            const float sample = out[i];
            out[i] = std::isfinite(sample) ? juce::jlimit(-16.0f, 16.0f, sample) : 0.0f;
        }
    }
}

te::AutomatableParameter* MagdaGateExpanderCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaGateExpanderCompiledPlugin::HostSlotInfo& MagdaGateExpanderCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaGateExpanderCompiledPlugin::displayValueToNativeValue(int slotIndex,
                                                                 float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
    return magda::ParameterUtils::realToNormalized(
        displayValue, parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]));
}

float MagdaGateExpanderCompiledPlugin::nativeValueToDisplayValue(int slotIndex,
                                                                 float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
    return magda::ParameterUtils::normalizedToReal(
        nativeValue, parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]));
}

constexpr AliasSpec kAliases[] = {
    {"attack", 0, "Attack"}, {"release", 1, "Release"},     {"mix", 2, "Mix"},
    {"output", 3, "Output"}, {"threshold", 4, "Threshold"}, {"ratio", 5, "Ratio"},
    {"range", 6, "Range"},
};

const CompiledPluginSpec& getMagdaGateExpanderSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaGateExpanderCompiledPlugin::xmlTypeName,
        .displayName = "Gate",
        .browserCategory = "Dynamics",
        .description =
            "Compiled Faust stereo gate / downward expander with a linked peak detector. "
            "Threshold sets where the gate opens; Ratio shapes the slope; "
            "Range bounds the deepest cut. "
            "Attack and Release shape the envelope; "
            "Mix blends the gated signal back against dry for parallel gating.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaGateExpanderCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
