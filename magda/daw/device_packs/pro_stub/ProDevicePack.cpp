#include "ProStubPlugin.hpp"
#include "plugins/InternalPluginRegistry.hpp"

namespace magda::pro_stub {

namespace {

namespace te = tracktion::engine;
using daw::audio::InternalPluginRegistry;
using daw::audio::InternalPluginSpec;

te::Plugin::Ptr createPlugin(const te::PluginCreationInfo& info) {
    return new ProStubPlugin(info);
}

te::Plugin::Ptr createInEdit(const InternalPluginSpec&, te::Edit& edit, const juce::String&) {
    juce::ValueTree state(te::IDs::PLUGIN);
    state.setProperty(te::IDs::type, ProStubPlugin::xmlTypeName, nullptr);
    return edit.getPluginCache().createNewPlugin(state);
}

bool matchesPlugin(te::Plugin* plugin) {
    return dynamic_cast<ProStubPlugin*>(plugin) != nullptr;
}

void registerProDevices(InternalPluginRegistry& registry) {
    const bool registered = registry.registerPlugin(
        {.pluginId = ProStubPlugin::xmlTypeName,
         .displayName = "Pro Pack Stub",
         .browserCategory = "Pro",
         .description = "Transparent proof device loaded from the optional static pro pack.",
         .createMode = daw::audio::InternalPluginCreateMode::FreshValueTree,
         .matchesPlugin = matchesPlugin,
         .showInBrowser = true,
         .createInEdit = createInEdit,
         .createCustomPlugin = createPlugin});
    jassert(registered);
    juce::ignoreUnused(registered);
}

[[maybe_unused]] const bool proDevicePackRegistered =
    daw::audio::registerDevicePack(registerProDevices);

}  // namespace

}  // namespace magda::pro_stub
