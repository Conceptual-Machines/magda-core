#include "plugins/compiled/MagdaUtilityCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_utility.generated.cpp"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaUtilityCompiledPlugin::xmlTypeName = "magda_utility";

namespace {

struct UtilHarvest {
    struct Control {
        int idx = -1;
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        FAUSTFLOAT* zone = nullptr;
    };
    std::vector<Control> controls;
};

class UtilHarvester : public ::UI {
  public:
    UtilHarvest harvest;

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

const UtilHarvest::Control* findByIdx(const UtilHarvest& h, int idx) {
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

MagdaUtilityCompiledPlugin::MagdaUtilityCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    dsp_ = std::make_unique<MagdaUtilityDsp>();
    rebuildEngineState(44100);
    buildHostParameters();
}

MagdaUtilityCompiledPlugin::~MagdaUtilityCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaUtilityCompiledPlugin::getName() const {
    return "Utility";
}
juce::String MagdaUtilityCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaUtilityCompiledPlugin::getShortName(int) {
    return "Util";
}
juce::String MagdaUtilityCompiledPlugin::getSelectableDescription() {
    return "Utility";
}

void MagdaUtilityCompiledPlugin::rebuildEngineState(int sampleRate) {
    if (!dsp_)
        return;
    dsp_->init(sampleRate);
    numInputs_ = dsp_->getNumInputs();
    numOutputs_ = dsp_->getNumOutputs();

    UtilHarvester harvester;
    dsp_->buildUserInterface(&harvester);

    zones_.fill(nullptr);
    for (int i = 0; i < kHostSlotCount; ++i) {
        if (auto* c = findByIdx(harvester.harvest, i))
            zones_[static_cast<size_t>(i)] = c->zone;
    }
}

void MagdaUtilityCompiledPlugin::buildHostParameters() {
    hostSlotInfo_[kGainSlot] = {.name = "Gain",
                                .unit = "dB",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = -60.0f,
                                .maxValue = 12.0f,
                                .defaultValue = 0.0f};
    hostSlotInfo_[kPanSlot] = {.name = "Pan",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = -1.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 0.0f};
    hostSlotInfo_[kWidthSlot] = {.name = "Width",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 2.0f,
                                 .defaultValue = 1.0f};
    hostSlotInfo_[kLowMonoFreqSlot] = {.name = "Low Mono Freq",
                                       .unit = "Hz",
                                       .scale = magda::ParameterScale::Logarithmic,
                                       .minValue = 20.0f,
                                       .maxValue = 500.0f,
                                       .defaultValue = 120.0f,
                                       .scaleAnchor = 120.0f};
    hostSlotInfo_[kMonoSlot] = {.name = "Mono",
                                .scale = magda::ParameterScale::Discrete,
                                .minValue = 0.0f,
                                .maxValue = 1.0f,
                                .defaultValue = 0.0f};
    hostSlotInfo_[kLowMonoSlot] = {.name = "Low Mono",
                                   .scale = magda::ParameterScale::Discrete,
                                   .minValue = 0.0f,
                                   .maxValue = 1.0f,
                                   .defaultValue = 0.0f};
    hostSlotInfo_[kFlipLSlot] = {.name = "Flip L",
                                 .scale = magda::ParameterScale::Discrete,
                                 .minValue = 0.0f,
                                 .maxValue = 1.0f,
                                 .defaultValue = 0.0f};
    hostSlotInfo_[kFlipRSlot] = {.name = "Flip R",
                                 .scale = magda::ParameterScale::Discrete,
                                 .minValue = 0.0f,
                                 .maxValue = 1.0f,
                                 .defaultValue = 0.0f};

    juce::NormalisableRange<float> normalisedRange{0.0f, 1.0f};
    auto* undoManager = getUndoManager();

    for (int i = 0; i < kHostSlotCount; ++i) {
        const auto& slot = hostSlotInfo_[static_cast<size_t>(i)];
        const juce::String id = "magda_utility_" + slot.name.toLowerCase().replace(" ", "_");
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

void MagdaUtilityCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));
    scratchIn_.setSize(numInputs_, info.blockSizeSamples, false, true, true);
    scratchOut_.setSize(numOutputs_, info.blockSizeSamples, false, true, true);
    inPtrs_.assign(static_cast<size_t>(numInputs_), nullptr);
    outPtrs_.assign(static_cast<size_t>(numOutputs_), nullptr);
}

void MagdaUtilityCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaUtilityCompiledPlugin::reset() {
    if (dsp_)
        dsp_->instanceClear();
}

void MagdaUtilityCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
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

te::AutomatableParameter* MagdaUtilityCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaUtilityCompiledPlugin::HostSlotInfo& MagdaUtilityCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaUtilityCompiledPlugin::displayValueToNativeValue(int slotIndex,
                                                            float displayValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return displayValue;
    return magda::ParameterUtils::realToNormalized(
        displayValue, parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]));
}

float MagdaUtilityCompiledPlugin::nativeValueToDisplayValue(int slotIndex,
                                                            float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nativeValue;
    return magda::ParameterUtils::normalizedToReal(
        nativeValue, parameterInfoForSlot(hostSlotInfo_[static_cast<size_t>(slotIndex)]));
}

constexpr AliasSpec kUtilAliases[] = {
    {"gain", 0, "Gain"},    {"pan", 1, "Pan"},
    {"width", 2, "Width"},  {"lowmonofreq", 3, "Low Mono Freq"},
    {"mono", 4, "Mono"},    {"lowmono", 5, "Low Mono"},
    {"flipl", 6, "Flip L"}, {"flipr", 7, "Flip R"},
};

const CompiledPluginSpec& getMagdaUtilitySpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaUtilityCompiledPlugin::xmlTypeName,
        .displayName = "Utility",
        .browserCategory = "Utility",
        .description =
            "Stereo utility: gain, pan, stereo width, mono, low mono, per-channel polarity flip.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaUtilityCompiledPlugin(info);
        },
        .aliases = kUtilAliases,
        .aliasCount = static_cast<int>(sizeof(kUtilAliases) / sizeof(kUtilAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
