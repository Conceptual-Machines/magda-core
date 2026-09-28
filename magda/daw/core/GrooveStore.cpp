#include "GrooveStore.hpp"

#include <algorithm>
#include <iterator>
#include <memory>

#include "BinaryData.h"

namespace magda {

namespace {

constexpr auto kSettingsTag = "PROPERTIES";
constexpr auto kSettingKey = "GrooveTemplates";
constexpr auto kListTag = "GROOVETEMPLATES";
constexpr auto kGrooveTag = "GROOVETEMPLATE";
constexpr auto kUnnamed = "Unnamed";
constexpr int kMaxNameLength = 32;
constexpr int kMinNotes = 2;
constexpr int kMaxNotes = 1024;
constexpr int kMaxNotesPerBeat = 8;

// Added by name when missing, whatever the list holds, as Tracktion does.
constexpr const char* kBasicSwings[] = {
    R"(<GROOVETEMPLATE name="Basic 8th Swing" numberOfNotes="2" notesPerBeat="2" parameterized="1"><SHIFT delta="0.0"/><SHIFT delta="0.66"/></GROOVETEMPLATE>)",
    R"(<GROOVETEMPLATE name="Basic 16th Swing" numberOfNotes="2" notesPerBeat="4" parameterized="1"><SHIFT delta="0.0"/><SHIFT delta="0.66"/></GROOVETEMPLATE>)",
};

/** @brief @p file as juce::PropertiesFile keeps it, or an empty one where it is not that. */
std::unique_ptr<juce::XmlElement> readSettings(const juce::File& file) {
    auto settings = file.existsAsFile() ? juce::parseXML(file) : nullptr;
    if (settings == nullptr || !settings->hasTagName(kSettingsTag))
        return std::make_unique<juce::XmlElement>(kSettingsTag);
    return settings;
}

GrooveTemplateData parseGroove(const juce::XmlElement& node) {
    GrooveTemplateData groove;
    groove.name = node.getStringAttribute("name", kUnnamed);
    groove.notesPerBeat = node.getIntAttribute("notesPerBeat", 2);
    groove.parameterized = node.getBoolAttribute("parameterized", false);

    // Saved with trailing zeros trimmed, so a note past the last shift is on the grid.
    const auto notes = std::max(0, node.getIntAttribute("numberOfNotes", 16));
    groove.latenessProportions.assign(static_cast<size_t>(notes), 0.0f);
    size_t note = 0;
    for (const auto* shift : node.getChildWithTagNameIterator("SHIFT")) {
        if (note == groove.latenessProportions.size())
            break;
        groove.latenessProportions[note++] =
            static_cast<float>(shift->getDoubleAttribute("delta", 0.0));
    }
    return groove;
}

std::vector<GrooveTemplateData> parseList(const juce::XmlElement* list) {
    std::vector<GrooveTemplateData> grooves;
    if (list == nullptr || !list->hasTagName(kListTag))
        return grooves;

    for (const auto* node : list->getChildWithTagNameIterator(kGrooveTag))
        grooves.push_back(parseGroove(*node));
    return grooves;
}

std::vector<GrooveTemplateData> parseAsset(const char* data, int size) {
    return parseList(juce::parseXML(juce::String::fromUTF8(data, size)).get());
}

std::unique_ptr<juce::XmlElement> grooveXml(const GrooveTemplateData& groove) {
    const auto& latenesses = groove.latenessProportions;
    auto node = std::make_unique<juce::XmlElement>(kGrooveTag);
    node->setAttribute("name", groove.name);
    node->setAttribute("numberOfNotes", static_cast<int>(latenesses.size()));
    node->setAttribute("notesPerBeat", groove.notesPerBeat);
    node->setAttribute("parameterized", groove.parameterized ? 1 : 0);

    // Trailing zeros are left off, and read back as zero.
    const auto lastShift = std::find_if(latenesses.rbegin(), latenesses.rend(),
                                        [](float lateness) { return lateness != 0.0f; });
    for (auto shift = latenesses.begin(); shift != lastShift.base(); ++shift)
        node->createNewChildElement("SHIFT")->setAttribute(
            "delta", 0.001 * juce::roundToInt(1000.0 * *shift));
    return node;
}

bool hasGroove(const std::vector<GrooveTemplateData>& grooves, const juce::String& name) {
    return std::ranges::find(grooves, name, &GrooveTemplateData::name) != grooves.end();
}

/** @brief @p name less a trailing " (N)", which a dedupe numbers afresh. */
juce::String unnumbered(const juce::String& name) {
    if (!name.trim().endsWithChar(')'))
        return name;

    const auto open = name.lastIndexOfChar('(');
    const auto close = name.lastIndexOfChar(')');
    if (open > 0 && close > open && name.substring(open + 1, close).containsOnly("0123456789"))
        return name.substring(0, open).trim();
    return name;
}

}  // namespace

GrooveStore::GrooveStore(juce::File settingsFile) : settingsFile_(std::move(settingsFile)) {
    // Read without a juce::PropertiesFile, which is a Timer and so wants a message manager.
    const auto settings = readSettings(settingsFile_);
    if (const auto* stored = settings->getChildByAttribute("name", kSettingKey))
        grooves_ = parseList(stored->getChildByName(kListTag));

    if (grooves_.empty())
        grooves_ =
            parseAsset(BinaryData::groove_templates_xml, BinaryData::groove_templates_xmlSize);

    if (std::ranges::none_of(grooves_, &GrooveTemplateData::parameterized))
        std::ranges::move(parseAsset(BinaryData::groove_templates_parameterized_xml,
                                     BinaryData::groove_templates_parameterized_xmlSize),
                          std::back_inserter(grooves_));

    for (const auto* swingXml : kBasicSwings) {
        auto swing = parseGroove(*juce::parseXML(juce::String(swingXml)));
        if (!hasGroove(grooves_, swing.name))
            grooves_.push_back(std::move(swing));
    }
}

bool GrooveStore::upsert(const GrooveTemplateData& groove) {
    const auto& asked = groove.latenessProportions;
    if (groove.name.isEmpty() || asked.empty())
        return false;

    const auto notes = std::clamp(static_cast<int>(asked.size()), kMinNotes, kMaxNotes);
    GrooveTemplateData kept;
    kept.notesPerBeat = std::clamp(groove.notesPerBeat, 1, kMaxNotesPerBeat);
    kept.parameterized = true;
    kept.latenessProportions.assign(static_cast<size_t>(notes), 0.0f);
    for (size_t i = 0; i < std::min(asked.size(), kept.latenessProportions.size()); ++i)
        kept.latenessProportions[i] = std::clamp(asked[i], -1.0f, 1.0f);

    // Out before the dedupe, so a groove written again keeps its own name.
    auto at = std::ranges::find(grooves_, groove.name, &GrooveTemplateData::name);
    if (at != grooves_.end())
        at = grooves_.erase(at);

    auto name = groove.name.trim();
    name = name.isEmpty() ? juce::String(kUnnamed) : name.substring(0, kMaxNameLength);
    const auto base = unnumbered(name);
    for (int suffix = 2; hasGroove(grooves_, name); ++suffix)
        name = base + " (" + juce::String(suffix) + ")";
    kept.name = name;

    grooves_.insert(at, std::move(kept));
    save();
    return true;
}

void GrooveStore::save() const {
    auto list = std::make_unique<juce::XmlElement>(kListTag);
    for (const auto& groove : grooves_)
        list->addChildElement(grooveXml(groove).release());

    // The other keys kept, and the list as the one child of its VALUE, as PropertiesFile saves.
    auto settings = readSettings(settingsFile_);
    auto* value = settings->getChildByAttribute("name", kSettingKey);
    if (value == nullptr) {
        value = settings->createNewChildElement("VALUE");
        value->setAttribute("name", kSettingKey);
    }
    value->removeAttribute("val");
    value->deleteAllChildElements();
    value->addChildElement(list.release());

    settingsFile_.getParentDirectory().createDirectory();
    settings->writeTo(settingsFile_);
}

}  // namespace magda
