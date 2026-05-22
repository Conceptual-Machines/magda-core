#include "PluginPreferences.hpp"

#include "AppPaths.hpp"
#include "version.hpp"

namespace magda {

namespace {
constexpr const char* kKind = "plugin_preferences";
constexpr const char* kDrumGridBuiltinId = "drumgrid";

// Plugins whose per-instance kit should NOT be mirrored to a user-global
// default. Internal DrumGrid is the canonical case: its kit is built
// dynamically per instance from whatever samples the user drops on each
// chain — no transferable default exists between instances.
bool hasGlobalKitDefault(const juce::String& pluginIdentifier) {
    return pluginIdentifier != kDrumGridBuiltinId;
}
}  // namespace

PluginPreferences& PluginPreferences::getInstance() {
    static PluginPreferences instance;
    return instance;
}

PluginPreferences::PluginPreferences() {
    load();
}

bool PluginPreferences::prefersDrumGrid(const juce::String& pluginIdentifier) const {
    if (pluginIdentifier.isEmpty())
        return false;
    if (drumGridPlugins_.find(pluginIdentifier) != drumGridPlugins_.end())
        return true;
    // Built-in default: MAGDA's DrumGrid plugin opens in the drum-grid view
    // even when nothing is recorded.
    return pluginIdentifier == kDrumGridBuiltinId;
}

void PluginPreferences::setPrefersDrumGrid(const juce::String& pluginIdentifier, bool prefer) {
    if (pluginIdentifier.isEmpty())
        return;
    if (prefer)
        drumGridPlugins_.insert(pluginIdentifier);
    else
        drumGridPlugins_.erase(pluginIdentifier);
    save();
}

std::vector<magda::KitRow> PluginPreferences::defaultKitRows(
    const juce::String& pluginIdentifier) const {
    if (pluginIdentifier.isEmpty() || !hasGlobalKitDefault(pluginIdentifier))
        return {};
    auto it = defaultKits_.find(pluginIdentifier);
    if (it == defaultKits_.end())
        return {};
    return it->second;
}

void PluginPreferences::setDefaultKitRows(const juce::String& pluginIdentifier,
                                          const std::vector<KitRow>& rows) {
    if (pluginIdentifier.isEmpty() || !hasGlobalKitDefault(pluginIdentifier))
        return;
    if (rows.empty())
        defaultKits_.erase(pluginIdentifier);
    else
        defaultKits_[pluginIdentifier] = rows;
    save();
}

void PluginPreferences::load() {
    drumGridPlugins_.clear();
    defaultKits_.clear();
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

    auto prefersVar = payload->getProperty("prefersDrumGrid");
    if (prefersVar.isArray()) {
        for (const auto& entry : *prefersVar.getArray()) {
            auto id = entry.toString();
            if (id.isNotEmpty())
                drumGridPlugins_.insert(id);
        }
    }

    auto kitsVar = payload->getProperty("defaultKits");
    if (kitsVar.isArray()) {
        for (const auto& kitEntry : *kitsVar.getArray()) {
            auto* kitObj = kitEntry.getDynamicObject();
            if (kitObj == nullptr)
                continue;
            auto pluginId = kitObj->getProperty("plugin").toString();
            if (pluginId.isEmpty())
                continue;
            auto rowsVar = kitObj->getProperty("rows");
            if (!rowsVar.isArray())
                continue;
            std::vector<KitRow> rows;
            for (const auto& rowVar : *rowsVar.getArray()) {
                auto* rowObj = rowVar.getDynamicObject();
                if (rowObj == nullptr || !rowObj->hasProperty("note"))
                    continue;
                KitRow r;
                r.noteNumber = juce::jlimit(0, 127, static_cast<int>(rowObj->getProperty("note")));
                r.label = rowObj->getProperty("label").toString();
                r.role = rowObj->getProperty("role").toString();
                rows.push_back(std::move(r));
            }
            if (!rows.empty())
                defaultKits_[pluginId] = std::move(rows);
        }
    }
}

void PluginPreferences::save() const {
    juce::Array<juce::var> prefersList;
    for (const auto& id : drumGridPlugins_)
        prefersList.add(juce::var(id));

    juce::Array<juce::var> kitsList;
    for (const auto& [pluginId, rows] : defaultKits_) {
        if (rows.empty())
            continue;
        juce::Array<juce::var> rowArray;
        for (const auto& r : rows) {
            auto* rowObj = new juce::DynamicObject();
            rowObj->setProperty("note", r.noteNumber);
            if (r.label.isNotEmpty())
                rowObj->setProperty("label", r.label);
            if (r.role.isNotEmpty())
                rowObj->setProperty("role", r.role);
            rowArray.add(juce::var(rowObj));
        }
        auto* kitObj = new juce::DynamicObject();
        kitObj->setProperty("plugin", pluginId);
        kitObj->setProperty("rows", rowArray);
        kitsList.add(juce::var(kitObj));
    }

    auto* payload = new juce::DynamicObject();
    payload->setProperty("prefersDrumGrid", prefersList);
    payload->setProperty("defaultKits", kitsList);

    auto* envelope = new juce::DynamicObject();
    envelope->setProperty("magdaVersion", juce::String(MAGDA_VERSION));
    envelope->setProperty("kind", juce::String(kKind));
    envelope->setProperty("payload", juce::var(payload));

    auto file = magda::paths::pluginPreferencesFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(juce::var(envelope), false));
}

}  // namespace magda
