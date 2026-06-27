#include "PluginPreferences.hpp"

#include "AppPaths.hpp"
#include "version.hpp"

namespace magda {

namespace {
constexpr const char* kKind = "plugin_preferences";
constexpr const char* kDrumGridBuiltinId = "drumgrid";
constexpr const char* kInstrumentRackWrapperId = "rack";
constexpr const char* kMidiFxCategoryOverride = "MIDI FX";

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
    loadUnlocked();
}

bool PluginPreferences::prefersDrumGrid(const juce::String& pluginIdentifier) const {
    if (pluginIdentifier.isEmpty() || pluginIdentifier == kInstrumentRackWrapperId)
        return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (drumGridPlugins_.find(pluginIdentifier) != drumGridPlugins_.end())
        return true;
    // Built-in default: MAGDA's DrumGrid plugin opens in the drum-grid view
    // even when nothing is recorded.
    return pluginIdentifier == kDrumGridBuiltinId;
}

void PluginPreferences::setPrefersDrumGrid(const juce::String& pluginIdentifier, bool prefer) {
    if (pluginIdentifier.isEmpty() || pluginIdentifier == kInstrumentRackWrapperId)
        return;

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        changed = prefer ? drumGridPlugins_.insert(pluginIdentifier).second
                         : drumGridPlugins_.erase(pluginIdentifier) > 0;
        if (changed)
            saveUnlocked();
    }

    if (changed)
        notifyDrumGridPreferenceChanged(pluginIdentifier);
}

bool PluginPreferences::treatsAsMidiFx(const juce::String& pluginIdentifier) const {
    return browserCategoryOverride(pluginIdentifier) == kMidiFxCategoryOverride;
}

void PluginPreferences::setTreatsAsMidiFx(const juce::String& pluginIdentifier,
                                          bool treatAsMidiFx) {
    setBrowserCategoryOverride(
        pluginIdentifier, treatAsMidiFx ? juce::String(kMidiFxCategoryOverride) : juce::String());
}

juce::String PluginPreferences::browserCategoryOverride(
    const juce::String& pluginIdentifier) const {
    if (pluginIdentifier.isEmpty() || pluginIdentifier == kInstrumentRackWrapperId)
        return {};

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = categoryOverrides_.find(pluginIdentifier);
    return it == categoryOverrides_.end() ? juce::String() : it->second;
}

void PluginPreferences::setBrowserCategoryOverride(const juce::String& pluginIdentifier,
                                                   const juce::String& categoryOverride) {
    if (pluginIdentifier.isEmpty() || pluginIdentifier == kInstrumentRackWrapperId)
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    const auto normalized = categoryOverride.trim();
    bool changed = false;
    if (normalized.isEmpty()) {
        changed = categoryOverrides_.erase(pluginIdentifier) > 0;
    } else {
        auto it = categoryOverrides_.find(pluginIdentifier);
        changed = it == categoryOverrides_.end() || it->second != normalized;
        if (changed)
            categoryOverrides_[pluginIdentifier] = normalized;
    }
    if (changed)
        saveUnlocked();
}

juce::String PluginPreferences::identifierForDevice(const DeviceInfo& device) {
    return device.uniqueId.isNotEmpty() ? device.uniqueId : device.pluginId;
}

void PluginPreferences::addListener(Listener* listener) {
    listeners_.add(listener);
}

void PluginPreferences::removeListener(Listener* listener) {
    listeners_.remove(listener);
}

void PluginPreferences::notifyDrumGridPreferenceChanged(const juce::String& pluginIdentifier) {
    listeners_.call([&pluginIdentifier](Listener& listener) {
        listener.drumGridPreferenceChanged(pluginIdentifier);
    });
}

std::vector<magda::KitRow> PluginPreferences::defaultKitRows(
    const juce::String& pluginIdentifier) const {
    if (pluginIdentifier.isEmpty() || !hasGlobalKitDefault(pluginIdentifier))
        return {};
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = defaultKits_.find(pluginIdentifier);
    if (it == defaultKits_.end())
        return {};
    return it->second;
}

void PluginPreferences::setDefaultKitRows(const juce::String& pluginIdentifier,
                                          const std::vector<KitRow>& rows) {
    if (pluginIdentifier.isEmpty() || !hasGlobalKitDefault(pluginIdentifier))
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (rows.empty())
        defaultKits_.erase(pluginIdentifier);
    else
        defaultKits_[pluginIdentifier] = rows;
    saveUnlocked();
}

void PluginPreferences::loadUnlocked() {
    drumGridPlugins_.clear();
    categoryOverrides_.clear();
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
            if (id.isNotEmpty() && id != kInstrumentRackWrapperId)
                drumGridPlugins_.insert(id);
        }
    }

    auto midiFxVar = payload->getProperty("treatAsMidiFx");
    if (midiFxVar.isArray()) {
        for (const auto& entry : *midiFxVar.getArray()) {
            auto id = entry.toString();
            if (id.isNotEmpty() && id != kInstrumentRackWrapperId)
                categoryOverrides_[id] = kMidiFxCategoryOverride;
        }
    }

    auto categoryOverridesVar = payload->getProperty("categoryOverrides");
    if (categoryOverridesVar.isArray()) {
        for (const auto& entry : *categoryOverridesVar.getArray()) {
            auto* categoryObj = entry.getDynamicObject();
            if (categoryObj == nullptr)
                continue;
            auto id = categoryObj->getProperty("plugin").toString();
            auto category = categoryObj->getProperty("category").toString().trim();
            if (id.isNotEmpty() && id != kInstrumentRackWrapperId && category.isNotEmpty())
                categoryOverrides_[id] = category;
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

void PluginPreferences::saveUnlocked() const {
    juce::Array<juce::var> prefersList;
    for (const auto& id : drumGridPlugins_)
        prefersList.add(juce::var(id));

    juce::Array<juce::var> categoryOverrideList;
    for (const auto& [pluginId, category] : categoryOverrides_) {
        auto* categoryObj = new juce::DynamicObject();
        categoryObj->setProperty("plugin", pluginId);
        categoryObj->setProperty("category", category);
        categoryOverrideList.add(juce::var(categoryObj));
    }

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
    payload->setProperty("categoryOverrides", categoryOverrideList);
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
