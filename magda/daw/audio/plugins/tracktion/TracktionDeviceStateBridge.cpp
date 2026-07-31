#include "plugins/tracktion/TracktionDeviceStateBridge.hpp"

#include <algorithm>
#include <array>

#include "TracktionHelpers.hpp"
#include "core/DeviceState.hpp"
#include "plugins/InternalPluginRegistry.hpp"

namespace magda::daw::audio::tracktion_adapter {

namespace {

namespace ds = magda::device_state;

/// Properties on the DEVICE'S OWN plugin state that belong to the engine rather
/// than to the device. MAGDA either owns the same fact in `DeviceInfo`
/// (enabled -> bypassed, sidechainSourceID -> SidechainConfig) or does not want
/// it at all (editor window bounds). Dropping them is what makes the captured
/// document engine-neutral.
///
/// This applies at the ROOT ONLY. `type` in particular identifies the device,
/// and a nested plugin (a DrumGrid pad chain) has no DeviceInfo to carry it:
/// stripping it there would leave a tree that `getOrCreatePluginFor` cannot
/// instantiate, so the pad would come back empty.
bool isRootEngineOwnedProperty(const juce::Identifier& id) {
    static const std::array<juce::Identifier, 12> engineOwned{
        te::IDs::id,           te::IDs::type,           te::IDs::enabled,
        te::IDs::process,      te::IDs::frozen,         te::IDs::quickParamName,
        te::IDs::windowPos,    te::IDs::windowX,        te::IDs::windowY,
        te::IDs::windowLocked, te::IDs::masterPluginID, te::IDs::sidechainSourceID,
    };
    return std::find(engineOwned.begin(), engineOwned.end(), id) != engineOwned.end();
}

/// Nested trees keep everything except the engine's object id, matching what the
/// pre-v2 capture did (`stripTracktionIdsRecursive` stripped `id` and nothing
/// else). Their remaining properties are the nested device's own state.
bool isNestedEngineOwnedProperty(const juce::Identifier& id) {
    return id == te::IDs::id;
}

/// Child trees the engine attaches to every plugin. MAGDA rebuilds all three
/// from its own model after a device is created (modifier assignments via
/// syncModifiers, macros from the track/rack macro list, sidechain wiring from
/// SidechainConfig), so carrying them would double up rather than restore.
bool isEngineOwnedChild(const juce::ValueTree& child) {
    return child.hasType(te::IDs::MODIFIERASSIGNMENTS) || child.hasType(te::IDs::MACROPARAMETERS) ||
           child.hasType(te::IDs::SIDECHAINCONNECTIONS);
}

ds::Node captureNode(const juce::ValueTree& tree, bool isRoot) {
    ds::Node node;
    node.type = tree.getType().toString();

    for (int i = 0; i < tree.getNumProperties(); ++i) {
        const auto name = tree.getPropertyName(i);
        if (isRoot ? isRootEngineOwnedProperty(name) : isNestedEngineOwnedProperty(name))
            continue;
        node.props.set(name, tree.getProperty(name));
    }

    for (int i = 0; i < tree.getNumChildren(); ++i) {
        const auto child = tree.getChild(i);
        if (isEngineOwnedChild(child))
            continue;
        node.children.push_back(captureNode(child, false));
    }

    return node;
}

void applyNode(const ds::Node& node, juce::ValueTree& tree) {
    for (int i = 0; i < node.props.size(); ++i)
        tree.setProperty(node.props.getName(i), node.props.getValueAt(i), nullptr);

    for (const auto& child : node.children) {
        if (child.type.isEmpty())
            continue;
        juce::ValueTree childTree(child.type);
        applyNode(child, childTree);
        tree.appendChild(childTree, nullptr);
    }
}

/// MAGDA's canonical device id for a plugin type. Internal device ids are the
/// engine type names today, but a project may hold an older load alias, so the
/// registry has the final say.
juce::String canonicalDeviceType(const juce::String& pluginType) {
    if (const auto* spec = findInternalPluginSpecForLoadType(pluginType))
        if (spec->pluginId != nullptr)
            return spec->pluginId;
    return pluginType;
}

juce::ValueTree legacyPluginTree(const juce::String& savedState) {
    auto xml = juce::parseXML(savedState);
    if (!xml)
        return {};

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid())
        return {};

    stripTracktionIdsRecursive(tree);
    stripModifierAssignmentsRecursive(tree);
    adoptCanonicalPluginType(tree);
    return tree;
}

}  // namespace

juce::String captureInternalDeviceState(te::Plugin& plugin) {
    plugin.flushPluginStateToValueTree();

    if (!plugin.state.isValid())
        return {};

    ds::Doc doc;
    doc.deviceType = canonicalDeviceType(plugin.state.getProperty(te::IDs::type).toString());
    doc.root = captureNode(plugin.state, true);
    doc.root.type = {};  // the root element name is the engine's, not the device's

    const auto& params = plugin.getAutomatableParameters();
    doc.params.reserve(static_cast<size_t>(params.size()));
    for (int i = 0; i < params.size(); ++i) {
        auto* param = params[i];
        if (param == nullptr)
            continue;
        // Base value, never the modulated value: a modulated read would bake the
        // current LFO position into the saved patch.
        doc.params.push_back({i, param->paramID, param->getCurrentBaseValue()});
    }

    return ds::encode(doc);
}

juce::ValueTree devicePluginTreeFromState(const juce::String& savedState) {
    if (savedState.isEmpty())
        return {};

    if (ds::looksLikeLegacyEngineState(savedState))
        return legacyPluginTree(savedState);

    const auto doc = ds::decode(savedState);
    if (!doc)
        return {};

    juce::ValueTree tree(te::IDs::PLUGIN);
    tree.setProperty(te::IDs::type, doc->deviceType, nullptr);
    applyNode(doc->root, tree);
    adoptCanonicalPluginType(tree);
    return tree;
}

void applyDeviceStateParameters(te::Plugin& plugin, const juce::String& savedState) {
    const auto doc = ds::decode(savedState);
    if (!doc || doc->params.empty())
        return;

    const auto& params = plugin.getAutomatableParameters();

    for (const auto& saved : doc->params) {
        te::AutomatableParameter* param = nullptr;

        // Frozen index first; the stable id re-seats a value if a device ever
        // has to renumber (see DeviceParamSchema.hpp).
        if (saved.index >= 0 && saved.index < params.size())
            if (auto* candidate = params[saved.index];
                candidate != nullptr && (saved.id.isEmpty() || candidate->paramID == saved.id))
                param = candidate;

        if (param == nullptr && saved.id.isNotEmpty())
            for (auto* candidate : params)
                if (candidate != nullptr && candidate->paramID == saved.id) {
                    param = candidate;
                    break;
                }

        if (param != nullptr)
            param->setParameterFromHost(saved.value, juce::sendNotificationSync);
    }
}

}  // namespace magda::daw::audio::tracktion_adapter
