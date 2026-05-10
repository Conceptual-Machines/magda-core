#include "plugins/compiled/MagdaDelayCompiledPlugin.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ParameterUtils.hpp"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "magda_delay.generated.cpp"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/FaustParamInfo.hpp"

namespace magda::daw::audio::compiled {

const char* MagdaDelayCompiledPlugin::xmlTypeName = "magda_delay";

namespace {

// idx-based harvest, identical pattern to the saturator. Same ControlMetadata
// / parseFaustLabel helpers — keeps the harvest logic uniform across compiled
// plugins so a future refactor can pull it into a shared file.
struct DelayHarvest {
    struct Control {
        int idx = -1;
        FaustParamSlot::Kind kind = FaustParamSlot::Kind::Continuous;
        FAUSTFLOAT* zone = nullptr;
        std::vector<std::pair<float, juce::String>> choices;
    };
    std::vector<Control> controls;
};

class DelayHarvester : public ::UI {
  public:
    DelayHarvest harvest;

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

        DelayHarvest::Control c;
        c.idx = merged.slotIndex;
        c.kind = merged.isMenuStyle ? FaustParamSlot::Kind::Discrete : kind;
        c.zone = zone;
        c.choices = merged.menuChoices;
        harvest.controls.push_back(std::move(c));
    }

    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone_;
};

const DelayHarvest::Control* findByIdx(const DelayHarvest& h, int idx) {
    for (const auto& c : h.controls)
        if (c.idx == idx)
            return &c;
    return nullptr;
}

}  // namespace

MagdaDelayCompiledPlugin::MagdaDelayCompiledPlugin(const te::PluginCreationInfo& info)
    : te::Plugin(info) {
    dsp_ = std::make_unique<MagdaDelayDsp>();
    constexpr int kProvisionalSampleRate = 44100;
    rebuildEngineState(kProvisionalSampleRate);
    buildHostParameters();
}

MagdaDelayCompiledPlugin::~MagdaDelayCompiledPlugin() {
    notifyListenersOfDeletion();
    for (auto& p : hostParams_)
        if (p)
            p->detachFromCurrentValue();
}

juce::String MagdaDelayCompiledPlugin::getName() const {
    return "Delay";
}
juce::String MagdaDelayCompiledPlugin::getPluginType() {
    return xmlTypeName;
}
juce::String MagdaDelayCompiledPlugin::getShortName(int) {
    return "Delay";
}
juce::String MagdaDelayCompiledPlugin::getSelectableDescription() {
    return "Delay";
}

void MagdaDelayCompiledPlugin::rebuildEngineState(int sampleRate) {
    if (!dsp_)
        return;
    dsp_->init(sampleRate);
    numInputs_ = dsp_->getNumInputs();
    numOutputs_ = dsp_->getNumOutputs();

    DelayHarvester harvester;
    dsp_->buildUserInterface(&harvester);

    zones_.fill(nullptr);
    bpmZone_ = nullptr;
    divisionChoiceValues_.clear();

    for (int i = 0; i < kHostSlotCount; ++i) {
        if (auto* c = findByIdx(harvester.harvest, i))
            zones_[static_cast<size_t>(i)] = c->zone;
    }
    if (auto* c = findByIdx(harvester.harvest, kBpmSlot))
        bpmZone_ = c->zone;

    if (auto* c = findByIdx(harvester.harvest, kDivisionSlot)) {
        auto sorted = c->choices;
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        divisionChoiceValues_.reserve(sorted.size());
        for (const auto& choice : sorted)
            divisionChoiceValues_.push_back(choice.first);
    }
}

void MagdaDelayCompiledPlugin::buildHostParameters() {
    // Slot 0: Time (ms, 1..2000). Faust smooths internally; host slot is
    // greyed when Sync is on (gateSlotIndex below).
    hostSlotInfo_[kTimeSlot] = {.name = "Time",
                                .unit = "ms",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = 1.0f,
                                .maxValue = 2000.0f,
                                .defaultValue = 250.0f};
    // Slot 1: Division (discrete menu). Choices populated from the harvest
    // at construction; we don't hard-code names here so the [style:menu{...}]
    // in the DSP stays the source of truth.
    hostSlotInfo_[kDivisionSlot].name = "Division";
    hostSlotInfo_[kDivisionSlot].scale = magda::ParameterScale::Discrete;
    hostSlotInfo_[kDivisionSlot].minValue = 0.0f;
    hostSlotInfo_[kDivisionSlot].maxValue = 0.0f;  // filled in below from divisionChoiceValues_
    hostSlotInfo_[kDivisionSlot].defaultValue = 0.0f;
    // Slot 2: Sync (boolean checkbox)
    hostSlotInfo_[kSyncSlot] = {.name = "Sync",
                                .scale = magda::ParameterScale::Discrete,
                                .minValue = 0.0f,
                                .maxValue = 1.0f,
                                .defaultValue = 0.0f,
                                .scaleAnchor = std::numeric_limits<float>::quiet_NaN(),
                                .choices = {"Off", "On"}};
    // Slot 3: Feedback (0..0.95, the DSP's safety ceiling)
    hostSlotInfo_[kFeedbackSlot] = {.name = "Feedback",
                                    .scale = magda::ParameterScale::Linear,
                                    .minValue = 0.0f,
                                    .maxValue = 0.95f,
                                    .defaultValue = 0.45f};
    // Slot 4: Mix (0..1)
    hostSlotInfo_[kMixSlot] = {.name = "Mix",
                               .scale = magda::ParameterScale::Linear,
                               .minValue = 0.0f,
                               .maxValue = 1.0f,
                               .defaultValue = 0.35f};
    // Slot 5: Tone (-1..1 tilt)
    hostSlotInfo_[kToneSlot] = {.name = "Tone",
                                .scale = magda::ParameterScale::Linear,
                                .minValue = -1.0f,
                                .maxValue = 1.0f,
                                .defaultValue = 0.0f};
    // Slot 6: Cross (0..1 ping-pong amount)
    hostSlotInfo_[kCrossSlot] = {.name = "Cross",
                                 .scale = magda::ParameterScale::Linear,
                                 .minValue = 0.0f,
                                 .maxValue = 1.0f,
                                 .defaultValue = 0.0f};

    // Division choice list comes from the DSP harvest. Populate the Discrete
    // slot's choices by index — the underlying Faust value is mapped at
    // applyToBuffer time via divisionChoiceValues_, so the host only ever
    // deals in 0..N-1 indices.
    if (!divisionChoiceValues_.empty()) {
        const int n = static_cast<int>(divisionChoiceValues_.size());
        // We don't know the human-readable labels from the harvest here —
        // FaustMetadataParser drops them. Fall back to numeric stringified
        // tokens; the menu in the DSP source is the canonical set.
        for (int i = 0; i < n; ++i)
            hostSlotInfo_[kDivisionSlot].choices.push_back(
                juce::String(divisionChoiceValues_[static_cast<size_t>(i)], 3));
        hostSlotInfo_[kDivisionSlot].maxValue = static_cast<float>(n - 1);
        // Default to "1/4" if it's in the set (value 1.0); otherwise middle.
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
        const juce::String id = "magda_delay_" + slot.name.toLowerCase().replace(" ", "_");
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

void MagdaDelayCompiledPlugin::initialise(const te::PluginInitialisationInfo& info) {
    rebuildEngineState(static_cast<int>(info.sampleRate));
    scratchIn_.setSize(numInputs_, info.blockSizeSamples, false, true, true);
    scratchOut_.setSize(numOutputs_, info.blockSizeSamples, false, true, true);
    inPtrs_.assign(static_cast<size_t>(numInputs_), nullptr);
    outPtrs_.assign(static_cast<size_t>(numOutputs_), nullptr);
}

void MagdaDelayCompiledPlugin::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void MagdaDelayCompiledPlugin::reset() {
    if (dsp_)
        dsp_->instanceClear();
}

void MagdaDelayCompiledPlugin::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!fc.destBuffer || fc.bufferNumSamples <= 0 || !dsp_)
        return;

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
    writeContinuous(kTimeSlot);
    writeContinuous(kFeedbackSlot);
    writeContinuous(kMixSlot);
    writeContinuous(kToneSlot);
    writeContinuous(kCrossSlot);

    // Sync: 0/1 boolean. Host param is normalized 0..1 with two choices,
    // so >= 0.5 means On.
    if (auto* z = zones_[kSyncSlot])
        *z = hostParams_[kSyncSlot]->getCurrentValue() >= 0.5f ? FAUSTFLOAT(1) : FAUSTFLOAT(0);

    // Division: the host param picks an index 0..N-1; map to the Faust
    // choice value via divisionChoiceValues_, same idiom the saturator
    // uses for its mode dropdown.
    if (auto* z = zones_[kDivisionSlot]; z && !divisionChoiceValues_.empty()) {
        const float norm = hostParams_[kDivisionSlot]->getCurrentValue();
        const int count = static_cast<int>(divisionChoiceValues_.size());
        const int idx = juce::jlimit(
            0, count - 1, static_cast<int>(std::round(norm * static_cast<float>(count - 1))));
        *z = static_cast<FAUSTFLOAT>(divisionChoiceValues_[static_cast<size_t>(idx)]);
    }

    // Project tempo: pull the live BPM from TE's transport into the
    // hidden BPM zone every block. Same ProjectTempo plumbing the
    // FaustPlugin uses for interpreted DSPs — we just bypass the pool.
    if (bpmZone_) {
        const double bpm = edit.tempoSequence.getBpmAt(fc.editTime.getStart());
        *bpmZone_ = static_cast<FAUSTFLOAT>(bpm);
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

te::AutomatableParameter* MagdaDelayCompiledPlugin::getSlotParameter(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return nullptr;
    return hostParams_[static_cast<size_t>(slotIndex)].get();
}

const MagdaDelayCompiledPlugin::HostSlotInfo& MagdaDelayCompiledPlugin::getSlotInfo(
    int slotIndex) const {
    static const HostSlotInfo kEmpty;
    if (slotIndex < 0 || slotIndex >= kHostSlotCount)
        return kEmpty;
    return hostSlotInfo_[static_cast<size_t>(slotIndex)];
}

float MagdaDelayCompiledPlugin::displayValueToNativeValue(int slotIndex, float displayValue) const {
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

float MagdaDelayCompiledPlugin::nativeValueToDisplayValue(int slotIndex, float nativeValue) const {
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

}  // namespace magda::daw::audio::compiled
