#include "plugins/InternalPluginRegistry.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

#include "plugins/MagdaDevice.hpp"

namespace magda::daw::audio {

namespace {

bool typeMatchesAlias(const juce::String& type, const InternalPluginSpec& spec) {
    if (spec.pluginId != nullptr && type.equalsIgnoreCase(spec.pluginId))
        return true;

    for (int i = 0; i < spec.loadAliasCount; ++i)
        if (spec.loadAliases != nullptr && spec.loadAliases[i] != nullptr &&
            type.equalsIgnoreCase(spec.loadAliases[i]))
            return true;

    return false;
}

const juce::Identifier& typeProperty() {
    static const juce::Identifier type("type");
    return type;
}

struct RegistryState {
    std::mutex mutex;
    std::vector<DevicePackRegistration> packRegistrations;
    bool initialized = false;
};

RegistryState& registryState() {
    static RegistryState state;
    return state;
}

}  // namespace

bool InternalPluginRegistry::registerPlugin(InternalPluginSpec spec) {
    if (spec.pluginId == nullptr || juce::String(spec.pluginId).isEmpty() ||
        findForLoadType(spec.pluginId) != nullptr)
        return false;

    for (int i = 0; i < spec.loadAliasCount; ++i) {
        if (spec.loadAliases == nullptr || spec.loadAliases[i] == nullptr)
            return false;
        if (findForLoadType(spec.loadAliases[i]) != nullptr)
            return false;
        for (int previous = 0; previous < i; ++previous)
            if (juce::String(spec.loadAliases[i]).equalsIgnoreCase(spec.loadAliases[previous]))
                return false;
    }

    auto owned = std::make_unique<InternalPluginSpec>(std::move(spec));
    pointers_.push_back(owned.get());
    specs_.push_back(std::move(owned));
    return true;
}

bool InternalPluginRegistry::registerParameterAlias(InternalParameterAliasSpec alias) {
    if (alias.pluginKey == nullptr || alias.alias == nullptr || alias.paramIndex < 0)
        return false;

    const auto duplicate = std::find_if(
        parameterAliases_.begin(), parameterAliases_.end(), [&alias](const auto& existing) {
            return juce::String(existing.pluginKey).equalsIgnoreCase(alias.pluginKey) &&
                   juce::String(existing.alias).equalsIgnoreCase(alias.alias);
        });
    if (duplicate != parameterAliases_.end())
        return false;

    parameterAliases_.push_back(alias);
    return true;
}

std::span<const InternalPluginSpec* const> InternalPluginRegistry::getAll() const {
    return {pointers_.data(), pointers_.size()};
}

std::span<const InternalParameterAliasSpec> InternalPluginRegistry::getParameterAliases() const {
    return {parameterAliases_.data(), parameterAliases_.size()};
}

const InternalPluginSpec* InternalPluginRegistry::find(const juce::String& pluginId) const {
    return findForLoadType(pluginId);
}

const InternalPluginSpec* InternalPluginRegistry::findForLoadType(const juce::String& type) const {
    for (const auto& spec : specs_)
        if (typeMatchesAlias(type, *spec))
            return spec.get();
    return nullptr;
}

bool registerDevicePack(DevicePackRegistration registerDevices) {
    if (registerDevices == nullptr)
        return false;

    auto& state = registryState();
    const std::lock_guard lock(state.mutex);
    if (state.initialized ||
        std::find(state.packRegistrations.begin(), state.packRegistrations.end(),
                  registerDevices) != state.packRegistrations.end())
        return false;

    state.packRegistrations.push_back(registerDevices);
    return true;
}

InternalPluginRegistry& getInternalPluginRegistry() {
    static InternalPluginRegistry registry;
    static const bool initialized = [] {
        auto& state = registryState();
        const std::lock_guard lock(state.mutex);
        for (const auto hook : state.packRegistrations)
            hook(registry);
        state.initialized = true;
        return true;
    }();
    (void)initialized;
    return registry;
}

std::span<const InternalPluginSpec* const> getAllInternalPluginSpecs() {
    return getInternalPluginRegistry().getAll();
}

std::span<const InternalParameterAliasSpec> getAllInternalParameterAliases() {
    return getInternalPluginRegistry().getParameterAliases();
}

const InternalPluginSpec* findInternalPluginSpec(const juce::String& pluginId) {
    return getInternalPluginRegistry().find(pluginId);
}

const InternalPluginSpec* findInternalPluginSpecForLoadType(const juce::String& type) {
    return getInternalPluginRegistry().findForLoadType(type);
}

bool internalPluginHasTag(const InternalPluginSpec& spec, const char* tag) {
    if (tag == nullptr)
        return false;
    for (int i = 0; i < spec.tagCount; ++i)
        if (spec.tags != nullptr && spec.tags[i] != nullptr &&
            juce::String(spec.tags[i]).equalsIgnoreCase(tag))
            return true;
    return false;
}

bool internalPluginHasTag(const juce::String& pluginId, const char* tag) {
    if (const auto* spec = findInternalPluginSpec(pluginId))
        return internalPluginHasTag(*spec, tag);
    return false;
}

const InternalPluginSpec* findInternalPluginSpecWithTag(const char* tag) {
    for (const auto* spec : getAllInternalPluginSpecs())
        if (internalPluginHasTag(*spec, tag))
            return spec;
    return nullptr;
}

bool isInternalAnalysisPlugin(const juce::String& pluginId) {
    return internalPluginHasTag(pluginId, "analysis");
}

bool isInternalMidiGeneratorPlugin(const juce::String& pluginId) {
    return internalPluginHasTag(pluginId, "midi-generator");
}

int internalPostFxAnalysisOrder(const juce::String& pluginId) {
    if (internalPluginHasTag(pluginId, "post-fx-analysis-0"))
        return 0;
    if (internalPluginHasTag(pluginId, "post-fx-analysis-1"))
        return 1;
    if (internalPluginHasTag(pluginId, "post-fx-analysis-2"))
        return 2;
    return -1;
}

DevicePluginPtr createInternalPluginFromSpec(const InternalPluginSpec& spec,
                                             DeviceSessionKey sessionKey,
                                             const juce::String& savedPluginState) {
    if (spec.createInSession == nullptr)
        return {};
    return spec.createInSession(spec, sessionKey, savedPluginState);
}

bool adoptCanonicalPluginType(const juce::ValueTree& state) {
    if (!state.isValid())
        return false;

    const auto type = state[typeProperty()].toString();
    const auto* spec = findInternalPluginSpecForLoadType(type);
    if (spec == nullptr || spec->pluginId == nullptr || type == spec->pluginId)
        return false;

    // ValueTree is a refcounted handle, so writing through the copy edits the
    // same shared data the caller (and the Edit) holds. MAGDA's dirty flag is
    // driven by explicit markDirty() calls rather than ValueTree listeners, so
    // this does not make merely opening a project look edited.
    juce::ValueTree writable = state;
    writable.setProperty(typeProperty(), spec->pluginId, nullptr);
    return true;
}

DevicePluginPtr createRegisteredPlugin(const DevicePluginCreationContext& context) {
    const auto type = context.state[typeProperty()].toString();
    const auto* spec = findInternalPluginSpecForLoadType(type);
    if (spec == nullptr || spec->createPlugin == nullptr)
        return {};
    adoptCanonicalPluginType(context.state);
    return spec->createPlugin(context);
}

std::unique_ptr<MagdaDevice> createRegisteredDevice(const DevicePluginCreationContext& context) {
    const auto type = context.state[typeProperty()].toString();
    const auto* spec = findInternalPluginSpecForLoadType(type);
    if (spec == nullptr || spec->createDevice == nullptr)
        return {};
    adoptCanonicalPluginType(context.state);
    return spec->createDevice(context);
}

}  // namespace magda::daw::audio
