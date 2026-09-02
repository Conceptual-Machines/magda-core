#include "plugins/tracktion/TracktionDeviceStateBridge.hpp"

#include <algorithm>
#include <array>

#include "TracktionHelpers.hpp"
#include "core/DeviceState.hpp"
#include "core/LegacyDeviceAliases.hpp"
#include "plugins/InternalPluginRegistry.hpp"
#include "plugins/compiled/CompiledFaustInterface.hpp"
#include "plugins/compiled/tracktion/CompiledFaustTracktionAdapter.hpp"
#include "plugins/tracktion/TracktionMagdaDevicePlugin.hpp"

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
    // `parameters` is the engine's own pre-v2 blob of parameter state. The
    // engine deletes it on the next flush, so a capture that recorded it wrote
    // a document that could not reproduce itself: the property was there the
    // first time and gone the second. It is also exactly the engine-shaped
    // parameter state v2 exists to keep out of a MAGDA file, so it is dropped
    // here with the rest (#2192).
    static const std::array<juce::Identifier, 13> engineOwned{
        te::IDs::id,           te::IDs::type,           te::IDs::enabled,
        te::IDs::process,      te::IDs::frozen,         te::IDs::quickParamName,
        te::IDs::windowPos,    te::IDs::windowX,        te::IDs::windowY,
        te::IDs::windowLocked, te::IDs::masterPluginID, te::IDs::sidechainSourceID,
        te::IDs::parameters,
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

/**
 * Capture a tree as a document node.
 *
 * `parameterProperties` are the root properties the engine uses to hold the
 * device's automatable parameters. In Tracktion a parameter IS a property on
 * the plugin's tree, so a plugin that has had its parameters set - which is
 * every plugin restored from a v2 document - carries the whole parameter list
 * twice: once under the engine's own property names, once in the document's
 * `params`. Keeping both would put engine-shaped parameter state back into a
 * MAGDA file, which is what v2 exists to remove, and would make the document
 * grow the first time a project was opened and saved. The document keys
 * parameters by frozen index; the properties are dropped.
 */
ds::Node captureNode(const juce::ValueTree& tree, bool isRoot,
                     const juce::StringArray& parameterProperties) {
    ds::Node node;
    node.type = tree.getType().toString();

    for (int i = 0; i < tree.getNumProperties(); ++i) {
        const auto name = tree.getPropertyName(i);
        if (isRoot ? isRootEngineOwnedProperty(name) : isNestedEngineOwnedProperty(name))
            continue;
        if (isRoot && parameterProperties.contains(name.toString()))
            continue;
        node.props.set(name, tree.getProperty(name));
    }

    for (int i = 0; i < tree.getNumChildren(); ++i) {
        const auto child = tree.getChild(i);
        if (isEngineOwnedChild(child))
            continue;
        node.children.push_back(captureNode(child, false, parameterProperties));
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

juce::String captureInternalDeviceState(te::Plugin& plugin, const juce::String& existingState) {
    // State from a newer schema was refused at load, so the live plugin holds
    // defaults rather than what the project actually says. Capturing over it
    // would destroy the newer document; hand it back untouched instead.
    if (ds::isFutureDeviceState(existingState))
        return existingState;

    plugin.flushPluginStateToValueTree();

    if (!plugin.state.isValid())
        return {};

    ds::Doc doc;
    doc.deviceType = canonicalDeviceType(plugin.state.getProperty(te::IDs::type).toString());

    const auto& params = plugin.getAutomatableParameters();

    // Three Tracktion parameters store under a property spelled differently
    // from their paramID; the spellings are frozen because released TE projects
    // carry them. Without the aliases those properties survive the filter below
    // and re-enter the document on the first capture after a restore.
    static const std::pair<const char*, const char*> kParamPropertySpellings[] = {
        {"chorusSpeed", "chosusSpeed"},  // 4OSC
        {"chorusWidth", "chrousWidth"},  // 4OSC
        {"damping", "damp"},             // Reverb
    };

    // The document carries NO parameter values (#2317): `DeviceInfo::parameters`
    // is the sole persisted authority for automatable parameters, in the
    // device's display domain. The live parameter list is still walked so the
    // engine's per-parameter properties are filtered out of the root below -
    // they are exactly the engine-shaped state the document exists to exclude.
    juce::StringArray parameterProperties;
    for (auto* param : params) {
        if (param == nullptr)
            continue;
        parameterProperties.add(param->paramID);
        for (const auto& [paramID, property] : kParamPropertySpellings)
            if (param->paramID == paramID)
                parameterProperties.add(property);
    }

    doc.root = captureNode(plugin.state, true, parameterProperties);
    doc.root.type = {};  // the root element name is the engine's, not the device's

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

std::vector<magda::legacy_devices::RetiredSlotValue> adoptRetiredNestedPluginTree(
    const juce::ValueTree& state) {
    if (!state.isValid())
        return {};

    const auto retiredType = state[te::IDs::type].toString();
    const auto successor = magda::legacy_devices::retiredDeviceSuccessor(retiredType);
    if (successor.isEmpty())
        return {};

    auto converted = magda::legacy_devices::convertRetiredDeviceState(
        retiredType, [&state](const char* property) { return state[juce::Identifier(property)]; });

    juce::ValueTree writable = state;
    for (const auto* property : magda::legacy_devices::retiredDeviceProperties(retiredType))
        writable.removeProperty(juce::Identifier(property), nullptr);
    writable.setProperty(te::IDs::type, successor, nullptr);

    return converted;
}

void applyRetiredSlotValues(te::Plugin& plugin,
                            const std::vector<magda::legacy_devices::RetiredSlotValue>& values) {
    if (values.empty())
        return;

    // Values arrive in the successor's real units. A compiled Faust device owns
    // the conversion into the normalized value its parameter actually stores; a
    // native successor's parameters already hold real units, so the value is
    // seated straight onto the parameter at that index.
    if (auto* compiled = deviceFromPlugin<compiled::ICompiledFaustPlugin>(&plugin)) {
        for (const auto& slot : values)
            if (auto* param = compiled::tracktionParameterForSlot(&plugin, slot.slot))
                param->setParameterFromHost(compiled->displayToNormalized(slot.slot, slot.value),
                                            juce::sendNotificationSync);
        return;
    }

    const auto& params = plugin.getAutomatableParameters();
    for (const auto& slot : values)
        if (juce::isPositiveAndBelow(slot.slot, params.size()))
            if (auto* param = params[slot.slot])
                param->setParameterFromHost(slot.value, juce::sendNotificationSync);
}

}  // namespace magda::daw::audio::tracktion_adapter
