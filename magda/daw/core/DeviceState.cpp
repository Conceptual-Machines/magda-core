#include "DeviceState.hpp"

namespace magda::device_state {

namespace {

// Document keys. Short because a project file holds one document per internal
// device and these repeat for every parameter.
const juce::Identifier kKeySchema("schema");
const juce::Identifier kKeyDevice("device");
const juce::Identifier kKeyParams("params");
const juce::Identifier kKeyProps("props");
const juce::Identifier kKeyChildren("children");
const juce::Identifier kKeyType("type");
const juce::Identifier kParamIndex("i");
const juce::Identifier kParamId("id");
const juce::Identifier kParamValue("v");

// Tag for a property value JSON cannot express. JUCE's JSON writer has no
// binary case at all: it falls through to `var::toString()`, which emits raw
// base64 with no quotes, producing a document that will not parse back. A
// device that stores audio in a property (an impulse response, a wavetable)
// would lose ALL of its state, not just the blob. Binary values are therefore
// wrapped as {"$bin": "<base64>"}.
//
// Property values are otherwise always scalars, so an object here is
// unambiguously one of these wrappers.
const juce::Identifier kBinaryTag("$bin");

juce::var encodeValue(const juce::var& value) {
    if (value.isBinaryData()) {
        auto* obj = new juce::DynamicObject();
        if (const auto* block = value.getBinaryData())
            obj->setProperty(kBinaryTag, block->toBase64Encoding());
        else
            obj->setProperty(kBinaryTag, juce::String());
        return juce::var(obj);
    }

    if (const auto* array = value.getArray()) {
        juce::Array<juce::var> encoded;
        for (const auto& entry : *array)
            encoded.add(encodeValue(entry));
        return juce::var(encoded);
    }

    return value;
}

juce::var decodeValue(const juce::var& value) {
    if (auto* obj = value.getDynamicObject()) {
        if (obj->hasProperty(kBinaryTag)) {
            juce::MemoryBlock block;
            block.fromBase64Encoding(obj->getProperty(kBinaryTag).toString());
            return juce::var(block);
        }
        return value;
    }

    if (const auto* array = value.getArray()) {
        juce::Array<juce::var> decoded;
        for (const auto& entry : *array)
            decoded.add(decodeValue(entry));
        return juce::var(decoded);
    }

    return value;
}

juce::var encodeProps(const juce::NamedValueSet& props) {
    auto* obj = new juce::DynamicObject();
    for (int i = 0; i < props.size(); ++i)
        obj->setProperty(props.getName(i), encodeValue(props.getValueAt(i)));
    return juce::var(obj);
}

void decodeProps(const juce::var& value, juce::NamedValueSet& outProps) {
    auto* obj = value.getDynamicObject();
    if (obj == nullptr)
        return;
    for (const auto& entry : obj->getProperties())
        outProps.set(entry.name, decodeValue(entry.value));
}

juce::var encodeNode(const Node& node) {
    auto* obj = new juce::DynamicObject();
    if (node.type.isNotEmpty())
        obj->setProperty(kKeyType, node.type);
    if (node.props.size() > 0)
        obj->setProperty(kKeyProps, encodeProps(node.props));
    if (!node.children.empty()) {
        juce::Array<juce::var> children;
        for (const auto& child : node.children)
            children.add(encodeNode(child));
        obj->setProperty(kKeyChildren, juce::var(children));
    }
    return juce::var(obj);
}

Node decodeNode(const juce::var& value) {
    Node node;
    auto* obj = value.getDynamicObject();
    if (obj == nullptr)
        return node;

    node.type = obj->getProperty(kKeyType).toString();
    decodeProps(obj->getProperty(kKeyProps), node.props);

    if (auto* children = obj->getProperty(kKeyChildren).getArray())
        for (const auto& child : *children)
            node.children.push_back(decodeNode(child));

    return node;
}

}  // namespace

juce::String encode(const Doc& doc) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty(kKeySchema, doc.version);
    obj->setProperty(kKeyDevice, doc.deviceType);

    if (!doc.params.empty()) {
        juce::Array<juce::var> params;
        for (const auto& param : doc.params) {
            auto* paramObj = new juce::DynamicObject();
            paramObj->setProperty(kParamIndex, param.index);
            if (param.id.isNotEmpty())
                paramObj->setProperty(kParamId, param.id);
            paramObj->setProperty(kParamValue, static_cast<double>(param.value));
            params.add(juce::var(paramObj));
        }
        obj->setProperty(kKeyParams, juce::var(params));
    }

    if (doc.root.props.size() > 0)
        obj->setProperty(kKeyProps, encodeProps(doc.root.props));

    if (!doc.root.children.empty()) {
        juce::Array<juce::var> children;
        for (const auto& child : doc.root.children)
            children.add(encodeNode(child));
        obj->setProperty(kKeyChildren, juce::var(children));
    }

    return juce::JSON::toString(juce::var(obj), true);
}

std::optional<Doc> decode(const juce::String& text) {
    if (text.isEmpty() || looksLikeLegacyEngineState(text))
        return std::nullopt;

    juce::var parsed;
    if (juce::JSON::parse(text, parsed).failed())
        return std::nullopt;

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr || !obj->hasProperty(kKeySchema) || !obj->hasProperty(kKeyDevice))
        return std::nullopt;

    Doc doc;
    doc.version = static_cast<int>(obj->getProperty(kKeySchema));

    // Only versions this build can actually read. A schema bump exists precisely
    // because the meaning of the document changed, so reading a newer one "as
    // far as it goes" is a misread, not graceful degradation: an existing field
    // may have been redefined, and capture would then write the misread values
    // back as v2. Refuse instead, and let the device load its defaults.
    if (doc.version < 2 || doc.version > kSchemaVersion)
        return std::nullopt;

    doc.deviceType = obj->getProperty(kKeyDevice).toString();
    if (doc.deviceType.isEmpty())
        return std::nullopt;

    if (auto* params = obj->getProperty(kKeyParams).getArray()) {
        for (const auto& entry : *params) {
            auto* paramObj = entry.getDynamicObject();
            if (paramObj == nullptr)
                continue;
            ParamValue param;
            param.index = static_cast<int>(paramObj->getProperty(kParamIndex));
            param.id = paramObj->getProperty(kParamId).toString();
            param.value =
                static_cast<float>(static_cast<double>(paramObj->getProperty(kParamValue)));
            doc.params.push_back(std::move(param));
        }
    }

    decodeProps(obj->getProperty(kKeyProps), doc.root.props);
    if (auto* children = obj->getProperty(kKeyChildren).getArray())
        for (const auto& child : *children)
            doc.root.children.push_back(decodeNode(child));

    return doc;
}

bool looksLikeLegacyEngineState(const juce::String& text) {
    return text.trimStart().startsWithChar('<');
}

bool isDeviceStateV2(const juce::String& text) {
    return decode(text).has_value();
}

std::optional<int> schemaVersionOf(const juce::String& text) {
    if (text.isEmpty() || looksLikeLegacyEngineState(text))
        return std::nullopt;

    juce::var parsed;
    if (juce::JSON::parse(text, parsed).failed())
        return std::nullopt;

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr || !obj->hasProperty(kKeySchema) || !obj->hasProperty(kKeyDevice))
        return std::nullopt;

    return static_cast<int>(obj->getProperty(kKeySchema));
}

bool isFutureDeviceState(const juce::String& text) {
    const auto version = schemaVersionOf(text);
    return version.has_value() && *version > kSchemaVersion;
}

void forEachNode(const Node& root, const std::function<void(const Node&)>& visit) {
    visit(root);
    for (const auto& child : root.children)
        forEachNode(child, visit);
}

}  // namespace magda::device_state
