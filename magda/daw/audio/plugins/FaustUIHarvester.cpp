#include "FaustUIHarvester.hpp"

#include <utility>

namespace magda::daw::audio {

namespace {

juce::String fromUtf8OrEmpty(const char* text) {
    return juce::String::fromUTF8(text != nullptr ? text : "");
}

bool isPolyVoiceGroup(const juce::String& label) {
    return label.startsWith("Voice") ||
           (label.length() >= 2 && label[0] == 'V' && juce::CharacterFunctions::isDigit(label[1]));
}

bool isPolyStructuralGroup(const juce::String& label) {
    return label == "Polyphonic" || label == "Voices" || isPolyVoiceGroup(label);
}

}  // namespace

void FaustUIHarvester::pushGroup(const char* label) {
    auto parsed = parseFaustLabel(fromUtf8OrEmpty(label));
    groupMetadataStack_.push_back(std::move(parsed.metadata));
    groupLabelStack_.push_back(std::move(parsed.cleanLabel));
}

ControlMetadata FaustUIHarvester::mergedMetadataFor(FAUSTFLOAT* zone) {
    ControlMetadata merged;
    for (const auto& groupMetadata : groupMetadataStack_)
        mergeFaustMetadata(merged, groupMetadata);

    if (auto it = pendingByZone_.find(zone); it != pendingByZone_.end()) {
        mergeFaustMetadata(merged, it->second);
        pendingByZone_.erase(it);
    }
    return merged;
}

bool FaustUIHarvester::isInsidePolyProxyGroup() const {
    for (auto it = groupLabelStack_.rbegin(); it != groupLabelStack_.rend(); ++it) {
        if (*it == "Voices")
            return true;
        if (isPolyVoiceGroup(*it))
            return false;
    }
    return false;
}

juce::String FaustUIHarvester::controlGroup() const {
    for (const auto& label : groupLabelStack_) {
        if (label.isEmpty())
            continue;
        if (layout_ == Layout::PolyphonicVoices && isPolyStructuralGroup(label))
            continue;
        return label;
    }
    return {};
}

void FaustUIHarvester::emitControl(FaustParamSlot::Kind kind, const char* rawLabel,
                                   FAUSTFLOAT* zone, FAUSTFLOAT init, FAUSTFLOAT min,
                                   FAUSTFLOAT max, FAUSTFLOAT step) {
    auto parsed = parseFaustLabel(fromUtf8OrEmpty(rawLabel));
    // This consumes and erases any pending declare() metadata for the zone.
    // Keep it before the poly-proxy early return so metadata from a skipped
    // proxy control cannot leak onto the next harvested control.
    auto merged = mergedMetadataFor(zone);
    mergeFaustMetadata(merged, parsed.metadata);

    if (layout_ == Layout::PolyphonicVoices && isInsidePolyProxyGroup())
        return;

    HarvestedControl control;
    control.kind = kind;
    control.label = std::move(parsed.cleanLabel);
    control.minValue = static_cast<float>(min);
    control.maxValue = static_cast<float>(max);
    control.stepValue = static_cast<float>(step);
    control.defaultValue = static_cast<float>(init);
    control.zone = zone;
    control.metadata = std::move(merged);
    control.group = controlGroup();
    controls_.push_back(std::move(control));
}

void FaustUIHarvester::openTabBox(const char* label) {
    pushGroup(label);
}

void FaustUIHarvester::openHorizontalBox(const char* label) {
    pushGroup(label);
}

void FaustUIHarvester::openVerticalBox(const char* label) {
    pushGroup(label);
}

void FaustUIHarvester::closeBox() {
    if (!groupMetadataStack_.empty())
        groupMetadataStack_.pop_back();
    if (!groupLabelStack_.empty())
        groupLabelStack_.pop_back();
}

void FaustUIHarvester::addButton(const char* label, FAUSTFLOAT* zone) {
    emitControl(FaustParamSlot::Kind::Trigger, label, zone, 0, 0, 1, 1);
}

void FaustUIHarvester::addCheckButton(const char* label, FAUSTFLOAT* zone) {
    emitControl(FaustParamSlot::Kind::Boolean, label, zone, 0, 0, 1, 1);
}

void FaustUIHarvester::addVerticalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init,
                                         FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) {
    emitControl(FaustParamSlot::Kind::Continuous, label, zone, init, min, max, step);
}

void FaustUIHarvester::addHorizontalSlider(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init,
                                           FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) {
    emitControl(FaustParamSlot::Kind::Continuous, label, zone, init, min, max, step);
}

void FaustUIHarvester::addNumEntry(const char* label, FAUSTFLOAT* zone, FAUSTFLOAT init,
                                   FAUSTFLOAT min, FAUSTFLOAT max, FAUSTFLOAT step) {
    emitControl(FaustParamSlot::Kind::Continuous, label, zone, init, min, max, step);
}

void FaustUIHarvester::declare(FAUSTFLOAT* zone, const char* key, const char* value) {
    ControlMetadata metadata;
    applyFaustAnnotation(fromUtf8OrEmpty(key).toLowerCase(), fromUtf8OrEmpty(value), metadata);
    if (zone == nullptr) {
        if (!groupMetadataStack_.empty())
            mergeFaustMetadata(groupMetadataStack_.back(), metadata);
    } else {
        mergeFaustMetadata(pendingByZone_[zone], metadata);
    }
}

}  // namespace magda::daw::audio
