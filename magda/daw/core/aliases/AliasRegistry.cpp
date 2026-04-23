#include "AliasRegistry.hpp"

#include <algorithm>

namespace magda {

// ============================================================================
// Singleton
// ============================================================================

AliasRegistry& AliasRegistry::getInstance() {
    static AliasRegistry instance;
    return instance;
}

// ============================================================================
// Internal layer access helpers
// ============================================================================

std::map<juce::String, StoredAlias>& AliasRegistry::layerMap(AliasLayer layer) {
    switch (layer) {
        case AliasLayer::UserProject:
            return userProjectLayer_;
        case AliasLayer::UserGlobal:
            return userGlobalLayer_;
        case AliasLayer::Curated:
            return curatedLayer_;
        case AliasLayer::AutoGen:
            return autoGenLayer_;
    }
    return userProjectLayer_;  // unreachable
}

const std::map<juce::String, StoredAlias>& AliasRegistry::layerMap(AliasLayer layer) const {
    switch (layer) {
        case AliasLayer::UserProject:
            return userProjectLayer_;
        case AliasLayer::UserGlobal:
            return userGlobalLayer_;
        case AliasLayer::Curated:
            return curatedLayer_;
        case AliasLayer::AutoGen:
            return autoGenLayer_;
    }
    return userProjectLayer_;  // unreachable
}

const std::map<juce::String, StoredAlias>& AliasRegistry::layerEntries(AliasLayer layer) const {
    return layerMap(layer);
}

// ============================================================================
// Layer walk helper
// ============================================================================

namespace {

// Walk layers in priority order, return first hit for canonicalName.
// pluginTypeHint disambiguates when empty -> accepts any pluginTypeKey.
const StoredAlias* walkLayers(const std::map<juce::String, StoredAlias>& userProject,
                              const std::map<juce::String, StoredAlias>& userGlobal,
                              const std::map<juce::String, StoredAlias>& curated,
                              const std::map<juce::String, StoredAlias>& autoGen,
                              const juce::String& canonicalName,
                              const juce::String& pluginTypeHint) {
    const std::map<juce::String, StoredAlias>* layers[] = {&userProject, &userGlobal, &curated,
                                                           &autoGen};

    for (const auto* layer : layers) {
        auto it = layer->find(canonicalName);
        if (it == layer->end())
            continue;

        const auto& sa = it->second;
        // If hint given, only accept matching pluginTypeKey
        if (pluginTypeHint.isNotEmpty() && sa.pluginTypeKey != pluginTypeHint)
            continue;

        return &sa;
    }
    return nullptr;
}

}  // namespace

// ============================================================================
// lookup
// ============================================================================

std::optional<StaticTarget> AliasRegistry::lookup(const juce::String& canonicalName,
                                                  const juce::String& pluginTypeHint) const {
    const auto* sa = walkLayers(userProjectLayer_, userGlobalLayer_, curatedLayer_, autoGenLayer_,
                                canonicalName, pluginTypeHint);
    if (sa == nullptr)
        return std::nullopt;

    // Only materialise when path is concrete
    if (!sa->path.has_value())
        return std::nullopt;

    // Apply drift fallback if path is present but we can't verify now
    // (device param list not accessible here without TrackManager — see note in header)
    StaticTarget t;
    t.devicePath = *sa->path;
    t.paramIndex = sa->paramIndex;
    return t;
}

// ============================================================================
// lookupStored
// ============================================================================

std::optional<StoredAlias> AliasRegistry::lookupStored(const juce::String& canonicalName,
                                                       const juce::String& pluginTypeHint) const {
    const auto* sa = walkLayers(userProjectLayer_, userGlobalLayer_, curatedLayer_, autoGenLayer_,
                                canonicalName, pluginTypeHint);
    if (sa == nullptr)
        return std::nullopt;

    return *sa;
}

// ============================================================================
// Mutation
// ============================================================================

void AliasRegistry::set(AliasLayer layer, const juce::String& canonicalName,
                        const StoredAlias& alias) {
    layerMap(layer)[canonicalName] = alias;
    notifyListeners(layer);
}

void AliasRegistry::clear(AliasLayer layer, const juce::String& canonicalName) {
    layerMap(layer).erase(canonicalName);
    notifyListeners(layer);
}

void AliasRegistry::clearLayer(AliasLayer layer) {
    layerMap(layer).clear();
    notifyListeners(layer);
}

void AliasRegistry::replaceLayer(AliasLayer layer,
                                 const std::map<juce::String, StoredAlias>& entries) {
    layerMap(layer) = entries;
    notifyListeners(layer);
}

void AliasRegistry::replaceAutoForDevice(const ChainNodePath& devicePath,
                                         std::map<juce::String, StoredAlias> newEntries) {
    // Remove existing AutoGen entries for this device path.
    auto it = autoGenLayer_.begin();
    while (it != autoGenLayer_.end()) {
        if (it->second.path.has_value() && *it->second.path == devicePath)
            it = autoGenLayer_.erase(it);
        else
            ++it;
    }

    // Insert new entries.
    for (auto& [name, alias] : newEntries)
        autoGenLayer_[name] = std::move(alias);

    notifyListeners(AliasLayer::AutoGen);
}

// ============================================================================
// Reverse lookup
// ============================================================================

std::vector<ReverseMatch> AliasRegistry::findByPath(const ChainNodePath& devicePath, int paramIndex,
                                                    bool autoGenOnly) const {
    std::vector<ReverseMatch> results;

    auto searchLayer = [&](const std::map<juce::String, StoredAlias>& layerMap, AliasLayer layer) {
        for (const auto& [name, alias] : layerMap) {
            if (!alias.path.has_value())
                continue;
            if (*alias.path != devicePath)
                continue;
            if (alias.paramIndex != paramIndex)
                continue;
            results.push_back({name, alias, layer});
        }
    };

    if (autoGenOnly) {
        searchLayer(autoGenLayer_, AliasLayer::AutoGen);
    } else {
        searchLayer(userProjectLayer_, AliasLayer::UserProject);
        searchLayer(userGlobalLayer_, AliasLayer::UserGlobal);
        searchLayer(curatedLayer_, AliasLayer::Curated);
        searchLayer(autoGenLayer_, AliasLayer::AutoGen);
    }

    return results;
}

// ============================================================================
// Drift fallback helper
// ============================================================================

std::optional<int> AliasRegistry::findParamIndexByName(const std::vector<ParameterInfo>& params,
                                                       const juce::String& name) {
    for (const auto& p : params) {
        if (p.name == name)
            return p.paramIndex;
    }
    return std::nullopt;
}

// ============================================================================
// Persistence -- UserGlobal (Config)
// ============================================================================

void AliasRegistry::loadUserGlobal(const juce::var& json) {
    userGlobalLayer_.clear();

    if (!json.isObject())
        return;

    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
        return;

    for (const auto& prop : obj->getProperties()) {
        StoredAlias alias;
        if (deserializeStoredAlias(prop.value, alias))
            userGlobalLayer_[prop.name.toString()] = alias;
    }

    notifyListeners(AliasLayer::UserGlobal);
}

juce::var AliasRegistry::saveUserGlobal() const {
    auto* obj = new juce::DynamicObject();
    for (const auto& [name, alias] : userGlobalLayer_) {
        obj->setProperty(name, serializeStoredAlias(alias));
    }
    return juce::var(obj);
}

// ============================================================================
// Persistence -- UserProject (ProjectSerializer)
// ============================================================================

void AliasRegistry::loadFromProjectJson(const juce::var& json) {
    userProjectLayer_.clear();

    if (!json.isObject())
        return;

    auto* obj = json.getDynamicObject();
    if (obj == nullptr)
        return;

    for (const auto& prop : obj->getProperties()) {
        StoredAlias alias;
        if (deserializeStoredAlias(prop.value, alias))
            userProjectLayer_[prop.name.toString()] = alias;
    }

    notifyListeners(AliasLayer::UserProject);
}

juce::var AliasRegistry::toProjectJson() const {
    auto* obj = new juce::DynamicObject();
    for (const auto& [name, alias] : userProjectLayer_) {
        obj->setProperty(name, serializeStoredAlias(alias));
    }
    return juce::var(obj);
}

// ============================================================================
// Listener management
// ============================================================================

void AliasRegistry::addListener(AliasRegistryListener* l) {
    listeners_.push_back(l);
}

void AliasRegistry::removeListener(AliasRegistryListener* l) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), l), listeners_.end());
}

void AliasRegistry::notifyListeners(AliasLayer layer) {
    auto copy = listeners_;
    for (auto* l : copy) {
        if (l != nullptr)
            l->aliasRegistryChanged(layer);
    }
}

// ============================================================================
// JSON helpers
// ============================================================================

juce::var serializeStoredAlias(const StoredAlias& alias) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("pluginTypeKey", alias.pluginTypeKey);
    obj->setProperty("paramIndex", alias.paramIndex);
    obj->setProperty("paramNameAtSetTime", alias.paramNameAtSetTime);

    if (alias.path.has_value()) {
        // Inline ChainNodePath serialization
        const auto& path = *alias.path;
        auto* pathObj = new juce::DynamicObject();
        pathObj->setProperty("trackId", path.trackId);
        pathObj->setProperty("topLevelDeviceId", path.topLevelDeviceId);
        pathObj->setProperty("isTrackLevel", path.isTrackLevel);

        juce::Array<juce::var> stepsArray;
        for (const auto& step : path.steps) {
            auto* stepObj = new juce::DynamicObject();
            stepObj->setProperty("type", static_cast<int>(step.type));
            stepObj->setProperty("id", step.id);
            stepsArray.add(juce::var(stepObj));
        }
        pathObj->setProperty("steps", juce::var(stepsArray));
        obj->setProperty("path", juce::var(pathObj));
    }

    return juce::var(obj);
}

bool deserializeStoredAlias(const juce::var& v, StoredAlias& out) {
    if (!v.isObject())
        return false;

    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return false;

    out.pluginTypeKey = obj->getProperty("pluginTypeKey").toString();
    out.paramIndex = static_cast<int>(obj->getProperty("paramIndex"));
    out.paramNameAtSetTime = obj->getProperty("paramNameAtSetTime").toString();
    out.path = std::nullopt;

    if (obj->hasProperty("path")) {
        auto pathVar = obj->getProperty("path");
        if (pathVar.isObject()) {
            auto* pathObj = pathVar.getDynamicObject();
            ChainNodePath path;
            path.trackId = static_cast<int>(pathObj->getProperty("trackId"));
            path.topLevelDeviceId = static_cast<int>(pathObj->getProperty("topLevelDeviceId"));
            if (pathObj->hasProperty("isTrackLevel"))
                path.isTrackLevel = static_cast<bool>(pathObj->getProperty("isTrackLevel"));

            auto stepsVar = pathObj->getProperty("steps");
            if (stepsVar.isArray()) {
                for (const auto& stepVar : *stepsVar.getArray()) {
                    if (!stepVar.isObject())
                        continue;
                    auto* stepObj = stepVar.getDynamicObject();
                    ChainPathStep step;
                    step.type =
                        static_cast<ChainStepType>(static_cast<int>(stepObj->getProperty("type")));
                    step.id = static_cast<int>(stepObj->getProperty("id"));
                    path.steps.push_back(step);
                }
            }
            out.path = path;
        }
    }

    return out.isValid();
}

}  // namespace magda
