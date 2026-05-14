#include "plugins/compiled/MagdaMultibandCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_multiband.generated.cpp"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaMultibandCompiledPlugin::xmlTypeName = "magda_multiband";

namespace {

// Same idx-based harvest as the rest of the compiled-Faust pack — every
// host control is identified by its [idx:N] annotation in the .dsp.
struct MultibandHarvest {
    struct Control {
        int idx = -1;
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        FAUSTFLOAT* zone = nullptr;
        std::vector<std::pair<float, juce::String>> choices;
    };
    std::vector<Control> controls;
};

class MultibandHarvester : public ::UI {
  public:
    MultibandHarvest harvest;

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

        MultibandHarvest::Control c;
        c.idx = merged.slotIndex;
        c.kind = merged.isMenuStyle ? FaustParamSlot::Kind::Discrete : kind;
        c.zone = zone;
        c.choices = merged.menuChoices;
        harvest.controls.push_back(std::move(c));
    }

    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

const MultibandHarvest::Control* findByIdx(const MultibandHarvest& h, int idx) {
    for (const auto& c : h.controls)
        if (c.idx == idx)
            return &c;
    return nullptr;
}

}  // namespace

MagdaMultibandCompiledPlugin::MagdaMultibandCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    dsp_ = std::make_unique<MagdaMultibandDsp>();
    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
}

MagdaMultibandCompiledPlugin::~MagdaMultibandCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaMultibandCompiledPlugin::getName() const {
    return "Multiband Compressor";
}
juce::String MagdaMultibandCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaMultibandCompiledPlugin::getShortName(int) {
    return "Multiband";
}
juce::String MagdaMultibandCompiledPlugin::getSelectableDescription() {
    return "Multiband Compressor";
}

void MagdaMultibandCompiledPlugin::rebuildEngineState(int sampleRate) {
    if (!dsp_)
        return;
    dsp_->init(sampleRate);
    numInputs_ = dsp_->getNumInputs();
    numOutputs_ = dsp_->getNumOutputs();

    MultibandHarvester harvester;
    dsp_->buildUserInterface(&harvester);

    zones_.fill(nullptr);
    for (int i = 0; i < kHostSlotCount; ++i) {
        if (auto* c = findByIdx(harvester.harvest, i))
            zones_[static_cast<size_t>(i)] = c->zone;
    }
}

void MagdaMultibandCompiledPlugin::buildHostParameters() {
    // Slot 0: Low XO (log Hz, 40..500, anchored at 200 Hz).
    hostSlotInfo_[kLowXoSlot] = {.name = "Low XO",
                                 .unit = "Hz",
                                 .scale = magda::ParameterScale::Logarithmic,
                                 .minValue = 40.0f,
                                 .maxValue = 500.0f,
                                 .defaultValue = 120.0f,
                                 .scaleAnchor = 200.0f};
    // Slot 1: High XO (log Hz, 500..8000, anchored at 2 kHz).
    hostSlotInfo_[kHighXoSlot] = {.name = "High XO",
                                  .unit = "Hz",
                                  .scale = magda::ParameterScale::Logarithmic,
                                  .minValue = 500.0f,
                                  .maxValue = 8000.0f,
                                  .defaultValue = 2500.0f,
                                  .scaleAnchor = 2000.0f};
    // Slot 2: Depth (master compression amount, 0..1).
    hostSlotInfo_[kDepthSlot] = {.name = "Depth",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 1.0f,
                                 .defaultValue = 1.0f};
    // Slot 3: Time (attack/release scaling, 0..1).
    hostSlotInfo_[kTimeSlot] = {.name = "Time",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = 0.0f,
                                .maxValue = 1.0f,
                                .defaultValue = 0.4f};
    // Slots 4-6: per-band makeup (dB, ±24).
    hostSlotInfo_[kLowGainSlot] = {.name = "Low Gain",
                                   .unit = "dB",
                                   .scale = magda::ParameterScale::Linear,
                                   .minValue = -24.0f,
                                   .maxValue = 24.0f,
                                   .defaultValue = 0.0f};
    hostSlotInfo_[kMidGainSlot] = {.name = "Mid Gain",
                                   .unit = "dB",
                                   .scale = magda::ParameterScale::Linear,
                                   .minValue = -24.0f,
                                   .maxValue = 24.0f,
                                   .defaultValue = 0.0f};
    hostSlotInfo_[kHighGainSlot] = {.name = "High Gain",
                                    .unit = "dB",
                                    .scale = magda::ParameterScale::Linear,
                                    .minValue = -24.0f,
                                    .maxValue = 24.0f,
                                    .defaultValue = 0.0f};
    // Slot 7: Mix (parallel-compression dry/wet blend).
    hostSlotInfo_[kMixSlot] = {.name = "Mix",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = 0.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 1.0f};
    // Slot 8: Output (dB, -24..+12).
    hostSlotInfo_[kOutputSlot] = {.name = "Output",
                                  .unit = "dB",
                                  .scale = magda::ParameterScale::Linear,
                                  .minValue = -24.0f,
                                  .maxValue = 12.0f,
                                  .defaultValue = 0.0f};

    auto setThreshAbove = [this](int slot, juce::String name) {
        hostSlotInfo_[static_cast<size_t>(slot)] = {.name = std::move(name),
                                                    .unit = "dB",
                                                    .scale = magda::ParameterScale::Linear,
                                                    .minValue = -60.0f,
                                                    .maxValue = 0.0f,
                                                    .defaultValue = -24.0f};
    };
    auto setThreshBelow = [this](int slot, juce::String name) {
        hostSlotInfo_[static_cast<size_t>(slot)] = {.name = std::move(name),
                                                    .unit = "dB",
                                                    .scale = magda::ParameterScale::Linear,
                                                    .minValue = -80.0f,
                                                    .maxValue = 0.0f,
                                                    .defaultValue = -48.0f};
    };
    auto setRatio = [this](int slot, juce::String name) {
        hostSlotInfo_[static_cast<size_t>(slot)] = {.name = std::move(name),
                                                    .scale = magda::ParameterScale::Linear,
                                                    .minValue = 1.0f,
                                                    .maxValue = 20.0f,
                                                    .defaultValue = 4.0f};
    };

    setThreshAbove(kLowThreshAboveSlot, "Low Thresh Above");
    setThreshBelow(kLowThreshBelowSlot, "Low Thresh Below");
    setRatio(kLowRatioSlot, "Low Ratio");
    setThreshAbove(kMidThreshAboveSlot, "Mid Thresh Above");
    setThreshBelow(kMidThreshBelowSlot, "Mid Thresh Below");
    setRatio(kMidRatioSlot, "Mid Ratio");
    setThreshAbove(kHighThreshAboveSlot, "High Thresh Above");
    setThreshBelow(kHighThreshBelowSlot, "High Thresh Below");
    setRatio(kHighRatioSlot, "High Ratio");

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
        const juce::String id = "magda_multiband_" + slot.name.toLowerCase().replace(" ", "_");
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
}

void MagdaMultibandCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));
    scratchIn_.setSize(numInputs_, info.blockSizeSamples, false, true, true);
    scratchOut_.setSize(numOutputs_, info.blockSizeSamples, false, true, true);
    inPtrs_.assign(static_cast<size_t>(numInputs_), nullptr);
    outPtrs_.assign(static_cast<size_t>(numOutputs_), nullptr);
}

void MagdaMultibandCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaMultibandCompiledPlugin::reset() {
    if (dsp_)
        dsp_->instanceClear();
}

void MagdaMultibandCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0 || !dsp_)
        return;

    auto writeSlot = [&](int slot) {
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
    for (int i = 0; i < kHostSlotCount; ++i)
        writeSlot(i);

    // Enforce Low XO < High XO at the audio boundary so the LR4 cascade
    // never sees an inverted split. The clamp is gentle (1 Hz minimum gap)
    // so the user can still drag the two markers right next to each other
    // without the bands collapsing.
    if (auto* lo = zones_[kLowXoSlot]; lo != nullptr) {
        if (auto* hi = zones_[kHighXoSlot]; hi != nullptr) {
            if (*lo >= *hi - 1.0f)
                *lo = *hi - 1.0f;
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

te::AutomatableParameter* MagdaMultibandCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaMultibandCompiledPlugin::HostSlotInfo& MagdaMultibandCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaMultibandCompiledPlugin::displayValueToNativeValue(int slotIndex,
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

float MagdaMultibandCompiledPlugin::nativeValueToDisplayValue(int slotIndex,
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
    {"low_xo", 0, "Low XO"},
    {"high_xo", 1, "High XO"},
    {"depth", 2, "Depth"},
    {"time", 3, "Time"},
    {"low_gain", 4, "Low Gain"},
    {"mid_gain", 5, "Mid Gain"},
    {"high_gain", 6, "High Gain"},
    {"mix", 7, "Mix"},
    {"output", 8, "Output"},
    {"low_thresh_above", 9, "Low Thresh Above"},
    {"low_thresh_below", 10, "Low Thresh Below"},
    {"low_ratio", 11, "Low Ratio"},
    {"mid_thresh_above", 12, "Mid Thresh Above"},
    {"mid_thresh_below", 13, "Mid Thresh Below"},
    {"mid_ratio", 14, "Mid Ratio"},
    {"high_thresh_above", 15, "High Thresh Above"},
    {"high_thresh_below", 16, "High Thresh Below"},
    {"high_ratio", 17, "High Ratio"},
};

const CompiledPluginSpec& getMagdaMultibandSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaMultibandCompiledPlugin::xmlTypeName,
        .displayName = "Multiband Compressor",
        .browserCategory = "Dynamics",
        .description = "Compiled Faust multiband compressor with editable band thresholds.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaMultibandCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
