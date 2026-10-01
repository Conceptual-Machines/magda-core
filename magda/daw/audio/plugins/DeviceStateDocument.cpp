#include "plugins/DeviceStateDocument.hpp"

#include <cmath>
#include <limits>

#include "plugins/InternalPluginRegistry.hpp"

namespace magda::daw::audio {

namespace {

sdk::StateNode buildNode(const device_state::Node& node, std::vector<std::string>& dropped,
                         bool isRoot) {
    sdk::StateNode out(isRoot ? std::string() : node.type.toStdString());

    for (int i = 0; i < node.props.size(); ++i) {
        const auto name = node.props.getName(i).toString();
        const auto& value = node.props.getValueAt(i);
        const auto key = name.toStdString();

        if (key.empty()) {
            dropped.push_back(key);
        } else if (value.isBool()) {
            out.setBool(key, static_cast<bool>(value));
        } else if (value.isInt() || value.isInt64()) {
            out.setInt(key, static_cast<juce::int64>(value));
        } else if (value.isDouble()) {
            if (!out.setDouble(key, static_cast<double>(value)))
                dropped.push_back(key);
        } else if (value.isString()) {
            out.setString(key, value.toString().toStdString());
        } else if (const auto* block = value.getBinaryData()) {
            const auto* bytes = static_cast<const std::uint8_t*>(block->getData());
            out.setBinary(key, sdk::Binary(bytes, bytes + block->getSize()));
        } else {
            dropped.push_back(key);
        }
    }

    for (const auto& child : node.children)
        if (child.type.isNotEmpty())
            out.addChild(buildNode(child, dropped, false));

    return out;
}

/// A v1 root as the document's root: what is the device's own, and nothing of the engine's.
device_state::Node withoutEngineVocabulary(const device_state::Node& root) {
    device_state::Node own;
    own.type = {};
    for (int i = 0; i < root.props.size(); ++i)
        if (!device_state::isEngineOwnedRootProperty(root.props.getName(i)))
            own.props.set(root.props.getName(i), root.props.getValueAt(i));

    for (const auto& child : root.children)
        if (!device_state::isEngineOwnedChild(child))
            own.children.push_back(child);

    return own;
}

juce::ValueTree toTree(const sdk::StateNode& node, const juce::Identifier& type) {
    juce::ValueTree tree(type);
    for (const auto& property : node.properties())
        tree.setProperty(juce::Identifier(juce::String::fromUTF8(property.key.c_str())),
                         toJuceVar(property.value), nullptr);
    for (const auto& child : node.children())
        tree.appendChild(
            toTree(child, juce::Identifier(juce::String::fromUTF8(child.type().c_str()))), nullptr);
    return tree;
}

device_state::Node toLegacyNode(const sdk::StateNode& node) {
    device_state::Node out;
    out.type = juce::String::fromUTF8(node.type().c_str());
    for (const auto& property : node.properties())
        out.props.set(juce::Identifier(juce::String::fromUTF8(property.key.c_str())),
                      toJuceVar(property.value));
    for (const auto& child : node.children())
        out.children.push_back(toLegacyNode(child));
    return out;
}

}  // namespace

juce::var toJuceVar(const sdk::StateValue& value) {
    switch (value.kind()) {
        case sdk::StateValue::Kind::Int64: {
            const auto v = *value.int64();
            if (v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max())
                return juce::var(static_cast<int>(v));
            return juce::var(static_cast<juce::int64>(v));
        }
        case sdk::StateValue::Kind::Double:
            return juce::var(*value.real());
        case sdk::StateValue::Kind::Bool:
            return juce::var(*value.boolean());
        case sdk::StateValue::Kind::String:
            return juce::var(juce::String::fromUTF8(value.string()->c_str(),
                                                    static_cast<int>(value.string()->size())));
        case sdk::StateValue::Kind::Binary:
            return juce::var(juce::MemoryBlock(value.binary()->data(), value.binary()->size()));
    }
    return {};
}

void applyReportedState(device_state::Doc& doc, const sdk::StateNode& reported) {
    for (const auto& property : reported.properties())
        doc.root.props.set(juce::Identifier(juce::String::fromUTF8(property.key.c_str())),
                           toJuceVar(property.value));

    for (const auto& child : reported.children()) {
        const auto type = juce::String::fromUTF8(child.type().c_str());
        std::erase_if(doc.root.children, [&type](const device_state::Node& existing) {
            return existing.type == type;
        });
    }
    for (const auto& child : reported.children())
        doc.root.children.push_back(toLegacyNode(child));
}

sdk::StateNode toSdkNode(const device_state::Node& node, std::vector<std::string>* dropped) {
    std::vector<std::string> scratch;
    return buildNode(node, dropped != nullptr ? *dropped : scratch, node.type.isEmpty());
}

std::optional<NormalisedDeviceState> normaliseDeviceState(const juce::String& savedState) {
    if (savedState.isEmpty())
        return std::nullopt;

    if (device_state::isFutureDeviceState(savedState))
        return std::nullopt;

    auto doc = device_state::decodeSavedState(savedState);
    if (!doc || doc->deviceType.isEmpty())
        return std::nullopt;

    auto deviceType = doc->deviceType;
    if (const auto* spec = findInternalPluginSpecForLoadType(deviceType);
        spec != nullptr && spec->pluginId != nullptr)
        deviceType = spec->pluginId;

    NormalisedDeviceState result;
    result.document.deviceType = deviceType.toStdString();

    // v1 is the engine's own tree, so what is not the device's is dropped; a v2 document
    // is MAGDA's, and everything in it is the device's.
    const auto root = doc->version == 1 ? withoutEngineVocabulary(doc->root) : doc->root;
    result.document.root = buildNode(root, result.dropped, true);
    return result;
}

juce::ValueTree toLegacyTree(const sdk::StateDocument& document) {
    auto tree = toTree(document.root, juce::Identifier("PLUGIN"));
    tree.setProperty(juce::Identifier("type"), juce::String::fromUTF8(document.deviceType.c_str()),
                     nullptr);
    return tree;
}

}  // namespace magda::daw::audio
