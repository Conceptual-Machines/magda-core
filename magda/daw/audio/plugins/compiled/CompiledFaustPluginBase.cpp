#include "plugins/compiled/CompiledFaustPluginBase.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "plugins/FaustMetadataParser.hpp"
#include "plugins/FaustParamInfo.hpp"

namespace magda::daw::audio::compiled {

namespace {

juce::String poolParamId(int index) {
    return juce::String("param_") + juce::String(index + 1).paddedLeft('0', 2);
}

std::vector<std::pair<float, juce::String>> sortedChoices(const FaustParamSlot& slot) {
    auto out = slot.choices;
    std::sort(out.begin(), out.end(),
              [](const std::pair<float, juce::String>& a, const std::pair<float, juce::String>& b) {
                  return a.first < b.first;
              });
    return out;
}

juce::String formatSlotValue(const FaustParamSlot& slot, float nativeValue) {
    if (slot.kind == FaustParamSlot::Kind::Boolean)
        return nativeValue >= 0.5f ? "On" : "Off";

    if (slot.kind == FaustParamSlot::Kind::Discrete) {
        for (const auto& choice : sortedChoices(slot)) {
            if (std::abs(choice.first - nativeValue) <= 1.0e-4f)
                return choice.second;
        }
        return juce::String(static_cast<int>(std::round(nativeValue)));
    }

    if (slot.unit.isNotEmpty())
        return juce::String(nativeValue, 2) + " " + slot.unit;
    return juce::String(nativeValue, 3);
}

float parseSlotValue(const FaustParamSlot& slot, const juce::String& text) {
    if (slot.kind == FaustParamSlot::Kind::Boolean)
        return text.equalsIgnoreCase("on") || text == "1" ? 1.0f : 0.0f;

    if (slot.kind == FaustParamSlot::Kind::Discrete) {
        for (const auto& choice : sortedChoices(slot)) {
            if (choice.second.equalsIgnoreCase(text.trim()))
                return choice.first;
        }
    }

    return text.upToFirstOccurrenceOf(" ", false, false).getFloatValue();
}

float displayToNative(const FaustParamSlot& slot, float displayValue) {
    if (slot.kind != FaustParamSlot::Kind::Discrete)
        return juce::jlimit(slot.minValue, slot.maxValue, displayValue);

    const auto choices = sortedChoices(slot);
    if (choices.empty())
        return juce::jlimit(slot.minValue, slot.maxValue, displayValue);

    const int index = juce::jlimit(0, static_cast<int>(choices.size() - 1),
                                   static_cast<int>(std::round(displayValue)));
    return choices[static_cast<size_t>(index)].first;
}

float nativeToDisplay(const FaustParamSlot& slot, float nativeValue) {
    if (slot.kind != FaustParamSlot::Kind::Discrete)
        return juce::jlimit(slot.minValue, slot.maxValue, nativeValue);

    const auto choices = sortedChoices(slot);
    for (size_t i = 0; i < choices.size(); ++i) {
        if (std::abs(choices[i].first - nativeValue) <= 1.0e-4f)
            return static_cast<float>(i);
    }
    return 0.0f;
}

float coerceNativeValue(const FaustParamSlot& slot, float nativeValue) {
    if (slot.kind == FaustParamSlot::Kind::Boolean)
        return nativeValue >= 0.5f ? 1.0f : 0.0f;
    if (slot.kind == FaustParamSlot::Kind::Discrete)
        return displayToNative(slot, nativeToDisplay(slot, nativeValue));
    return juce::jlimit(slot.minValue, slot.maxValue, nativeValue);
}

struct UIHarvester : public ::UI {
    std::vector<HarvestedControl> harvested;
    std::vector<ControlMetadata> groupStack;
    std::map<FAUSTFLOAT*, ControlMetadata> pendingByZone;

    void pushGroup(const char* label) {
        auto parsed = parseFaustLabel(juce::String::fromUTF8(label != nullptr ? label : ""));
        groupStack.push_back(parsed.metadata);
    }

    ControlMetadata mergedFor(FAUSTFLOAT* zone) {
        ControlMetadata merged;
        for (const auto& group : groupStack)
            mergeFaustMetadata(merged, group);
        if (auto it = pendingByZone.find(zone); it != pendingByZone.end()) {
            mergeFaustMetadata(merged, it->second);
            pendingByZone.erase(it);
        }
        return merged;
    }

    void emitControl(FaustParamSlot::Kind kind, const char* rawLabel, FAUSTFLOAT* zone,
                     FAUSTFLOAT init, FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) {
        auto parsed = parseFaustLabel(juce::String::fromUTF8(rawLabel != nullptr ? rawLabel : ""));
        ControlMetadata metadata = mergedFor(zone);
        mergeFaustMetadata(metadata, parsed.metadata);

        HarvestedControl h;
        h.kind = kind;
        h.label = parsed.cleanLabel;
        h.minValue = static_cast<float>(min);
        h.maxValue = static_cast<float>(max);
        h.stepValue = static_cast<float>(step);
        h.defaultValue = static_cast<float>(init);
        h.zone = zone;
        h.metadata = std::move(metadata);
        harvested.push_back(std::move(h));
    }

    void openTabBox(const char* label) override {
        pushGroup(label);
    }
    void openHorizontalBox(const char* label) override {
        pushGroup(label);
    }
    void openVerticalBox(const char* label) override {
        pushGroup(label);
    }
    void closeBox() override {
        if (!groupStack.empty())
            groupStack.pop_back();
    }

    void addButton(const char* label, FAUSTFLOAT* zone) override {
        emitControl(FaustParamSlot::Kind::Boolean, label, zone, 0, 0, 1, 1);
    }
    void addCheckButton(const char* label, FAUSTFLOAT* zone) override {
        emitControl(FaustParamSlot::Kind::Boolean, label, zone, 0, 0, 1, 1);
    }
    void addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                           FAUSTFLOAT max, FAUSTFLOAT step) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone, init, min, max, step);
    }
    void addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                             FAUSTFLOAT max, FAUSTFLOAT step) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone, init, min, max, step);
    }
    void addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                     FAUSTFLOAT max, FAUSTFLOAT step) override {
        emitControl(FaustParamSlot::Kind::Continuous, label, zone, init, min, max, step);
    }
    void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile(const char*, const char*, Soundfile**) override {}

    void declare(FAUSTFLOAT* zone, const char* key, const char* value) override {
        const auto k = juce::String::fromUTF8(key != nullptr ? key : "").toLowerCase();
        const auto v = juce::String::fromUTF8(value != nullptr ? value : "");
        ControlMetadata metadata;
        applyFaustAnnotation(k, v, metadata);
        if (zone == nullptr) {
            if (!groupStack.empty())
                mergeFaustMetadata(groupStack.back(), metadata);
        } else {
            mergeFaustMetadata(pendingByZone[zone], metadata);
        }
    }
};

}  // namespace

CompiledFaustPluginBase::CompiledFaustPluginBase(const te::PluginCreationInfo& info,
                                                 std::unique_ptr<::dsp> dsp,
                                                 juce::String displayName, juce::String xmlType)
    : te::Plugin(info),
      dsp_(std::move(dsp)),
      displayName_(std::move(displayName)),
      xmlType_(std::move(xmlType)) {
    harvestAndCreateParameters();
}

CompiledFaustPluginBase::~CompiledFaustPluginBase() {
    notifyListenersOfDeletion();
    for (auto& param : slotParams_) {
        if (param)
            param->detachFromCurrentValue();
    }
}

juce::String CompiledFaustPluginBase::getName() const {
    return displayName_;
}

juce::String CompiledFaustPluginBase::getPluginType() {
    return xmlType_;
}

juce::String CompiledFaustPluginBase::getShortName(int) {
    return displayName_;
}

juce::String CompiledFaustPluginBase::getSelectableDescription() {
    return displayName_;
}

void CompiledFaustPluginBase::harvestAndCreateParameters() {
    if (!dsp_)
        return;

    UIHarvester harvester;
    dsp_->buildUserInterface(&harvester);
    auto report = pool_.rebindFromHarvest(harvester.harvested);
    activeBindings_ = std::move(report.activeBindings);

    auto* undoManager = getUndoManager();
    for (int i = 0; i < FaustParamPool::kSize; ++i) {
        const auto& slot = pool_.slot(i);
        if (!slot.active || slot.hidden || slot.role != FaustControlRole::User)
            continue;

        const auto id = poolParamId(i);
        const auto identifier = juce::Identifier(id);
        slotValues_[static_cast<size_t>(i)].referTo(state, identifier, undoManager,
                                                    coerceNativeValue(slot, slot.defaultValue));

        juce::NormalisableRange<float> range{slot.minValue, slot.maxValue};
        if (slot.stepValue > 0.0f)
            range.interval = slot.stepValue;

        auto slotCopy = slot;
        auto param = addParam(
            id, id, range, [slotCopy](float v) { return formatSlotValue(slotCopy, v); },
            [slotCopy](const juce::String& s) { return parseSlotValue(slotCopy, s); });
        param->attachToCurrentValue(slotValues_[static_cast<size_t>(i)]);
        slotParams_[static_cast<size_t>(i)] = param;
    }
}

void CompiledFaustPluginBase::initialise(const te::PluginInitialisationInfo& info) {
    if (dsp_)
        dsp_->init(static_cast<int>(info.sampleRate));
    ensureScratchBuffer(info.blockSizeSamples);
}

void CompiledFaustPluginBase::deinitialise() {
    scratchIn_.setSize(0, 0);
    scratchOut_.setSize(0, 0);
    inPtrs_.clear();
    outPtrs_.clear();
}

void CompiledFaustPluginBase::reset() {
    if (dsp_)
        dsp_->instanceClear();
}

void CompiledFaustPluginBase::ensureScratchBuffer(int blockSize) {
    if (!dsp_)
        return;
    const int numInputs = dsp_->getNumInputs();
    const int numOutputs = dsp_->getNumOutputs();
    scratchIn_.setSize(numInputs, blockSize, false, true, true);
    scratchOut_.setSize(numOutputs, blockSize, false, true, true);
    inPtrs_.assign(static_cast<size_t>(numInputs), nullptr);
    outPtrs_.assign(static_cast<size_t>(numOutputs), nullptr);
}

void CompiledFaustPluginBase::applyToBuffer(const te::PluginRenderContext& fc) {
    if (!dsp_ || !fc.destBuffer || fc.bufferNumSamples <= 0)
        return;

    for (const auto& binding : activeBindings_) {
        if (!binding.zone || binding.slotIndex < 0 || binding.slotIndex >= FaustParamPool::kSize)
            continue;

        if (binding.role == FaustControlRole::ProjectTempo) {
            *binding.zone =
                static_cast<FAUSTFLOAT>(edit.tempoSequence.getBpmAt(fc.editTime.getStart()));
            continue;
        }

        const auto& param = slotParams_[static_cast<size_t>(binding.slotIndex)];
        if (!param)
            continue;

        const auto& slot = pool_.slot(binding.slotIndex);
        *binding.zone = static_cast<FAUSTFLOAT>(coerceNativeValue(slot, param->getCurrentValue()));
    }

    const int numSamples = fc.bufferNumSamples;
    const int startSample = fc.bufferStartSample;
    const int hostChannels = fc.destBuffer->getNumChannels();
    const int numInputs = dsp_->getNumInputs();
    const int numOutputs = dsp_->getNumOutputs();
    if (hostChannels <= 0 || numInputs <= 0 || numOutputs <= 0)
        return;

    if (scratchIn_.getNumSamples() < numSamples || scratchOut_.getNumSamples() < numSamples)
        ensureScratchBuffer(numSamples);

    for (int ch = 0; ch < numInputs; ++ch) {
        float* dst = scratchIn_.getWritePointer(ch);
        if (ch < hostChannels) {
            const float* src = fc.destBuffer->getReadPointer(ch, startSample);
            std::copy(src, src + numSamples, dst);
        } else {
            std::fill(dst, dst + numSamples, 0.0f);
        }
        inPtrs_[static_cast<size_t>(ch)] = dst;
    }

    for (int ch = 0; ch < numOutputs; ++ch) {
        if (ch < hostChannels)
            outPtrs_[static_cast<size_t>(ch)] = fc.destBuffer->getWritePointer(ch, startSample);
        else
            outPtrs_[static_cast<size_t>(ch)] = scratchOut_.getWritePointer(ch);
    }

    dsp_->compute(numSamples, inPtrs_.data(), outPtrs_.data());
}

float CompiledFaustPluginBase::displayValueToNativeValue(int slotIndex, float displayValue) const {
    if (slotIndex < 0 || slotIndex >= FaustParamPool::kSize)
        return displayValue;
    return displayToNative(pool_.slot(slotIndex), displayValue);
}

float CompiledFaustPluginBase::nativeValueToDisplayValue(int slotIndex, float nativeValue) const {
    if (slotIndex < 0 || slotIndex >= FaustParamPool::kSize)
        return nativeValue;
    return nativeToDisplay(pool_.slot(slotIndex), nativeValue);
}

}  // namespace magda::daw::audio::compiled
