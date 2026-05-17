#include "plugins/compiled/MagdaSaturatorCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_saturator.generated.cpp"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/FaustParamInfo.hpp"
#include "plugins/compiled/CompiledPluginRegistry.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaSaturatorCompiledPlugin::xmlTypeName = "magda_saturator";

namespace {

// Same idx-based harvest pattern as MagdaFilterCompiledPlugin: collect every
// control by [idx:N] and look up the host slots by their pinned indices.
struct SaturatorHarvest {
    struct Control {
        int idx = -1;
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        FAUSTFLOAT* zone = nullptr;
        std::vector<std::pair<float, juce::String>> choices;
    };
    std::vector<Control> controls;
};

class SaturatorHarvester : public ::UI {
  public:
    SaturatorHarvest harvest;

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

        SaturatorHarvest::Control c;
        c.idx = merged.slotIndex;
        c.kind = merged.isMenuStyle ? FaustParamSlot::Kind::Discrete : kind;
        c.zone = zone;
        c.choices = merged.menuChoices;
        harvest.controls.push_back(std::move(c));
    }

    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

const SaturatorHarvest::Control* findByIdx(const SaturatorHarvest& h, int idx) {
    for (const auto& c : h.controls)
        if (c.idx == idx)
            return &c;
    return nullptr;
}

}  // namespace

MagdaSaturatorCompiledPlugin::MagdaSaturatorCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    dsp_ = std::make_unique<MagdaSaturatorDsp>();
    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
}

MagdaSaturatorCompiledPlugin::~MagdaSaturatorCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaSaturatorCompiledPlugin::getName() const {
    return "Saturator";
}
juce::String MagdaSaturatorCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaSaturatorCompiledPlugin::getShortName(int) {
    return "Saturator";
}
juce::String MagdaSaturatorCompiledPlugin::getSelectableDescription() {
    return "Saturator";
}

void MagdaSaturatorCompiledPlugin::rebuildEngineState(int sampleRate) {
    if (!dsp_)
        return;
    dsp_->init(sampleRate);
    numInputs_ = dsp_->getNumInputs();
    numOutputs_ = dsp_->getNumOutputs();

    SaturatorHarvester harvester;
    dsp_->buildUserInterface(&harvester);

    zones_.fill(nullptr);
    modeChoiceValues_.clear();

    for (int i = 0; i < kHostSlotCount; ++i) {
        if (auto* c = findByIdx(harvester.harvest, i))
            zones_[static_cast<size_t>(i)] = c->zone;
    }

    if (auto* c = findByIdx(harvester.harvest, kModeSlot)) {
        auto sorted = c->choices;
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        modeChoiceValues_.reserve(sorted.size());
        for (const auto& choice : sorted)
            modeChoiceValues_.push_back(choice.first);
    }
}

void MagdaSaturatorCompiledPlugin::buildHostParameters() {
    // Slot 0: Drive (dB, linear, 0..24). Smoothing happens inside the DSP.
    hostSlotInfo_[kDriveSlot] = {.name = "Drive",
                                 .unit = "dB",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 24.0f,
                                 .defaultValue = 0.0f};
    // Slot 1: Mode (discrete, 6 options)
    hostSlotInfo_[kModeSlot].name = "Mode";
    hostSlotInfo_[kModeSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kModeSlot].choices = {"Tanh", "Soft", "Hard", "Fold", "Tube", "Tape"};
    hostSlotInfo_[kModeSlot].minValue = 0.0f;
    hostSlotInfo_[kModeSlot].maxValue =
        static_cast<float>(hostSlotInfo_[kModeSlot].choices.size() - 1);
    hostSlotInfo_[kModeSlot].defaultValue = 0.0f;
    // Slot 2: Bias (-1..1, bipolar)
    hostSlotInfo_[kBiasSlot] = {.name = "Bias",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = -1.0f,
                                .maxValue = 1.0f,
                                .defaultValue = 0.0f};
    // Slot 3: Tone (-1..1, bipolar tilt)
    hostSlotInfo_[kToneSlot] = {.name = "Tone",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = -1.0f,
                                .maxValue = 1.0f,
                                .defaultValue = 0.0f};
    // Slot 4: Mix (0..1)
    hostSlotInfo_[kMixSlot] = {.name = "Mix",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = 0.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 1.0f};
    // Slot 5: Output (dB, -24..6)
    hostSlotInfo_[kOutputSlot] = {.name = "Output",
                                  .unit = "dB",
                                  .scale = magda::ParameterScale::Linear,
                                  .minValue = -24.0f,
                                  .maxValue = 6.0f,
                                  .defaultValue = 0.0f};

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
        const juce::String id = "magda_saturator_" + slot.name.toLowerCase().replace(" ", "_");
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

void MagdaSaturatorCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));
    scratchIn_.setSize(numInputs_, info.blockSizeSamples, false, true, true);
    scratchOut_.setSize(numOutputs_, info.blockSizeSamples, false, true, true);
    inPtrs_.assign(static_cast<size_t>(numInputs_), nullptr);
    outPtrs_.assign(static_cast<size_t>(numOutputs_), nullptr);
}

void MagdaSaturatorCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaSaturatorCompiledPlugin::reset() {
    if (dsp_)
        dsp_->instanceClear();
}

void MagdaSaturatorCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0 || !dsp_)
        return;

    // Write each host param's display value into its Faust zone. For
    // continuous params we denormalize via ParameterUtils; the discrete
    // Mode picks the underlying Faust choice value by index.
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
    writeContinuous(kDriveSlot);
    writeContinuous(kBiasSlot);
    writeContinuous(kToneSlot);
    writeContinuous(kMixSlot);
    writeContinuous(kOutputSlot);

    if (auto* modeZone = zones_[kModeSlot]; modeZone && !modeChoiceValues_.empty()) {
        const float norm = hostParams_[kModeSlot]->getCurrentValue();
        const int count = static_cast<int>(modeChoiceValues_.size());
        const int idx = juce::jlimit(
            0, count - 1, static_cast<int>(std::round(norm * static_cast<float>(count - 1))));
        *modeZone = static_cast<FAUSTFLOAT>(modeChoiceValues_[idx]);
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

te::AutomatableParameter* MagdaSaturatorCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaSaturatorCompiledPlugin::HostSlotInfo& MagdaSaturatorCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaSaturatorCompiledPlugin::displayValueToNativeValue(int slotIndex,
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

float MagdaSaturatorCompiledPlugin::nativeValueToDisplayValue(int slotIndex,
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
    {"drive", 0, "Drive"}, {"mode", 1, "Mode"}, {"bias", 2, "Bias"},
    {"tone", 3, "Tone"},   {"mix", 4, "Mix"},   {"output", 5, "Output"},
};

const CompiledPluginSpec& getMagdaSaturatorSpec() {
    static const CompiledPluginSpec kSpec{
        .pluginId = MagdaSaturatorCompiledPlugin::xmlTypeName,
        .displayName = "Saturator",
        .browserCategory = "Distortion",
        .description =
            "Compiled Faust waveshaper with six selectable curves.\n"
            "<b>Tanh</b>: smooth hyperbolic, the classic warm saturation.\n"
            "<b>Soft</b>: gentle polynomial knee with a rolled-off top.\n"
            "<b>Hard</b>: instant clip ceiling for square-edged distortion.\n"
            "<b>Fold</b>: wavefolder, peaks reflect back for metallic overtones.\n"
            "<b>Tube</b>: asymmetric curve (1.4x positive, 1.0x negative) "
            "for valve-style even harmonics.\n"
            "<b>Tape</b>: tanh with an odd-order compression term, tape-style headroom.\n"
            "Drive pushes the input, Bias shifts the operating point, "
            "Tone tilts the post-shape EQ, Mix blends dry.",
        .createPlugin = [](const te::PluginCreationInfo& info) -> te::Plugin::Ptr {
            return new MagdaSaturatorCompiledPlugin(info);
        },
        .aliases = kAliases,
        .aliasCount = static_cast<int>(sizeof(kAliases) / sizeof(kAliases[0])),
    };
    return kSpec;
}

}  // namespace magda::daw::audio::compiled
