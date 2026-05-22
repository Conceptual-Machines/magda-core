#include "PluginPreferences.hpp"

#include "AppPaths.hpp"
#include "version.hpp"

namespace magda {

namespace {
constexpr const char* kKind = "plugin_preferences";
constexpr const char* kPianoRoll = "piano-roll";
constexpr const char* kDrumGrid = "drum-grid";

const char* editorToString(PluginPreferences::PreferredClipEditor e) {
    switch (e) {
        case PluginPreferences::PreferredClipEditor::PianoRoll:
            return kPianoRoll;
        case PluginPreferences::PreferredClipEditor::DrumGrid:
            return kDrumGrid;
        case PluginPreferences::PreferredClipEditor::Unset:
            break;
    }
    return "";
}

PluginPreferences::PreferredClipEditor editorFromString(const juce::String& s) {
    if (s == kPianoRoll)
        return PluginPreferences::PreferredClipEditor::PianoRoll;
    if (s == kDrumGrid)
        return PluginPreferences::PreferredClipEditor::DrumGrid;
    return PluginPreferences::PreferredClipEditor::Unset;
}
}  // namespace

PluginPreferences& PluginPreferences::getInstance() {
    static PluginPreferences instance;
    return instance;
}

PluginPreferences::PluginPreferences() {
    load();
}

PluginPreferences::PreferredClipEditor PluginPreferences::preferredClipEditorFor(
    const juce::String& pluginIdentifier) const {
    auto it = preferredEditor_.find(pluginIdentifier);
    if (it != preferredEditor_.end())
        return it->second;

    // Implicit defaults for MAGDA built-ins. Overridden by any explicit user
    // choice (which would land in the map above).
    if (pluginIdentifier == "drumgrid")
        return PreferredClipEditor::DrumGrid;

    return PreferredClipEditor::Unset;
}

void PluginPreferences::setPreferredClipEditor(const juce::String& pluginIdentifier,
                                               PreferredClipEditor editor) {
    if (pluginIdentifier.isEmpty())
        return;
    if (editor == PreferredClipEditor::Unset)
        preferredEditor_.erase(pluginIdentifier);
    else
        preferredEditor_[pluginIdentifier] = editor;
    save();
}

void PluginPreferences::load() {
    preferredEditor_.clear();
    auto file = magda::paths::pluginPreferencesFile();
    if (!file.existsAsFile())
        return;

    auto root = juce::JSON::parse(file.loadFileAsString());
    auto* obj = root.getDynamicObject();
    if (obj == nullptr || obj->getProperty("kind").toString() != kKind)
        return;

    auto* payload = obj->getProperty("payload").getDynamicObject();
    if (payload == nullptr)
        return;

    auto editorsVar = payload->getProperty("preferredEditor");
    if (!editorsVar.isArray())
        return;

    for (const auto& entry : *editorsVar.getArray()) {
        auto* entryObj = entry.getDynamicObject();
        if (entryObj == nullptr)
            continue;
        auto pluginId = entryObj->getProperty("plugin").toString();
        auto editor = editorFromString(entryObj->getProperty("editor").toString());
        if (pluginId.isNotEmpty() && editor != PreferredClipEditor::Unset)
            preferredEditor_[pluginId] = editor;
    }
}

void PluginPreferences::save() const {
    juce::Array<juce::var> editorArray;
    for (const auto& [identifier, editor] : preferredEditor_) {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("plugin", identifier);
        entry->setProperty("editor", juce::String(editorToString(editor)));
        editorArray.add(juce::var(entry));
    }

    auto* payload = new juce::DynamicObject();
    payload->setProperty("preferredEditor", editorArray);

    auto* envelope = new juce::DynamicObject();
    envelope->setProperty("magdaVersion", juce::String(MAGDA_VERSION));
    envelope->setProperty("kind", juce::String(kKind));
    envelope->setProperty("payload", juce::var(payload));

    auto file = magda::paths::pluginPreferencesFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(juce::var(envelope), false));
}

}  // namespace magda
