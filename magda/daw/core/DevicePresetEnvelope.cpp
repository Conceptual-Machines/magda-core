#include "DevicePresetEnvelope.hpp"

#include <juce_cryptography/juce_cryptography.h>

#include <cmath>
#include <ctime>
#include <functional>
#include <magda/sdk/preset/Preset.hpp>
#include <map>
#include <set>

#include "../audio/plugins/DeviceCatalogParameters.hpp"
#include "../audio/plugins/DeviceStateDocument.hpp"
#include "../audio/plugins/DeviceStateHydration.hpp"
#include "../audio/plugins/MagdaDevice.hpp"
#include "../project/serialization/ProjectSerializer.hpp"
#include "ControlTarget.hpp"
#include "LegacyDeviceAliases.hpp"
#include "version.hpp"

namespace magda::device_preset {

namespace {

constexpr int kHostVersion = 1;
constexpr const char* kHostKey = "magda";
constexpr const char* kSamplerId = "magdasampler";
constexpr const char* kSamplePathProperty = "source";
constexpr const char* kSampleAssetKey = "sample";

juce::String utf8(const std::string& text) {
    return juce::String::fromUTF8(text.c_str(), static_cast<int>(text.size()));
}

juce::String derivedId(const juce::String& pluginId, int index) {
    return pluginId + "_param_" + juce::String(index);
}

/// The id a parameter is keyed by: its own stable id, or the derived one
/// (docs/parameter-manifest.md).
juce::String stableIdOf(const juce::String& pluginId, const ParameterInfo& parameter,
                        int position) {
    if (parameter.stableId.isNotEmpty())
        return parameter.stableId;
    return derivedId(pluginId, parameter.paramIndex >= 0 ? parameter.paramIndex : position);
}

juce::String nowUtc() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if JUCE_WINDOWS
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

const char* formatName(PluginFormat format) {
    switch (format) {
        case PluginFormat::VST3:
            return "VST3";
        case PluginFormat::AU:
            return "AU";
        case PluginFormat::LV2:
            return "LV2";
        case PluginFormat::Internal:
            break;
    }
    return nullptr;
}

std::optional<PluginFormat> formatFromName(const std::string& name) {
    if (name == "VST3")
        return PluginFormat::VST3;
    if (name == "AU")
        return PluginFormat::AU;
    if (name == "LV2")
        return PluginFormat::LV2;
    return std::nullopt;
}

juce::String sha256Of(const juce::File& file) {
    return juce::SHA256(file).toHexString();
}

/// A sample path on its way out: the state names the asset, and the asset names the file.
bool moveSampleToAsset(sdk::StateDocument& doc, const juce::File& presetFile,
                       std::vector<sdk::PresetAsset>& assets) {
    const auto* value = doc.root.find(kSamplePathProperty);
    if (value == nullptr || value->toString().empty())
        return true;

    const auto path = utf8(value->toString());
    if (!juce::File::isAbsolutePath(path))
        return false;

    const juce::File file(path);
    const auto relative =
        file.getRelativePathFrom(presetFile.getParentDirectory()).replaceCharacter('\\', '/');
    if (!sdk::isValidAssetPath(relative.toStdString()))
        return false;

    sdk::PresetAsset asset{kSampleAssetKey, relative.toStdString(), std::nullopt};
    if (file.existsAsFile())
        asset.sha256 = sha256Of(file).toStdString();
    assets.push_back(std::move(asset));
    doc.root.setString(kSamplePathProperty, kSampleAssetKey);
    return true;
}

/// Macro and mod links that address a parameter by slot, rewritten to address it by id.
void keyLinksById(const juce::var& owners, const std::function<juce::String(int)>& idOf) {
    const auto* array = owners.getArray();
    if (array == nullptr)
        return;

    for (const auto& owner : *array) {
        auto* ownerObject = owner.getDynamicObject();
        const auto links = ownerObject != nullptr ? ownerObject->getProperty("links") : juce::var();
        for (const auto& link : links.isArray() ? *links.getArray() : juce::Array<juce::var>{}) {
            auto* linkObject = link.getDynamicObject();
            auto* target = linkObject != nullptr
                               ? linkObject->getProperty("target").getDynamicObject()
                               : nullptr;
            if (target == nullptr || static_cast<int>(target->getProperty("kind")) !=
                                         static_cast<int>(ControlTarget::Kind::PluginParam))
                continue;

            const int index = target->getProperty("paramIndex");
            if (index < 0)
                continue;
            target->removeProperty("paramIndex");
            target->setProperty("stableId", idOf(index));
        }
    }
}

/// Which slot a stable id names, from what the device declares and what the preset recorded.
class SlotResolver {
  public:
    SlotResolver(const juce::String& pluginId, bool internal, const juce::String& savedState)
        : pluginId_(pluginId) {
        if (!internal)
            return;
        const auto declared = daw::audio::createDetachedDevice(pluginId, savedState);
        if (declared == nullptr)
            return;

        haveDeclaration_ = true;
        for (int slot = 0; slot < declared->parameterCount(); ++slot) {
            const auto info = declared->parameterInfo(slot);
            declaredIds_[stableIdOf(pluginId, info, slot)] =
                info.paramIndex >= 0 ? info.paramIndex : slot;
        }
    }

    void hint(const juce::String& id, int index) {
        if (index >= 0)
            hints_[id] = index;
    }

    std::optional<int> indexOf(const juce::String& id) const {
        if (const auto it = declaredIds_.find(id); it != declaredIds_.end())
            return it->second;
        if (haveDeclaration_)
            return std::nullopt;

        const auto prefix = pluginId_ + "_param_";
        if (id.startsWith(prefix)) {
            const auto digits = id.substring(prefix.length());
            if (digits.isNotEmpty() && digits.containsOnly("0123456789") && digits.length() < 9)
                return digits.getIntValue();
        }
        if (const auto it = hints_.find(id); it != hints_.end())
            return it->second;
        return std::nullopt;
    }

  private:
    juce::String pluginId_;
    bool haveDeclaration_ = false;
    std::map<juce::String, int> declaredIds_;
    std::map<juce::String, int> hints_;
};

juce::File assetFile(const juce::File& presetFile, const std::string& path) {
    return presetFile.getParentDirectory().getChildFile(utf8(path));
}

}  // namespace

std::optional<Metadata> readMetadata(const juce::File& presetFile) {
    if (!presetFile.existsAsFile())
        return std::nullopt;

    const auto read = sdk::readPreset(presetFile.loadFileAsString().toStdString());
    if (!read.ok())
        return std::nullopt;

    Metadata metadata;
    metadata.id = utf8(read.preset->id);
    metadata.name = utf8(read.preset->name);
    metadata.author = utf8(read.preset->author);
    for (const auto& tag : read.preset->tags)
        metadata.tags.add(utf8(tag));
    metadata.created = utf8(read.preset->created);
    return metadata;
}

std::optional<juce::String> writeEnvelope(const DeviceInfo& device, const juce::File& presetFile,
                                          const Metadata& metadata) {
    const bool internal = device.format == PluginFormat::Internal;
    const auto convention =
        internal ? ParameterValueConvention::Real : ParameterValueConvention::Normalized;

    sdk::Preset preset;
    preset.id = metadata.id.toStdString();
    preset.writer = "MAGDA " + std::string(MAGDA_VERSION);
    preset.name = metadata.name.toStdString();
    preset.author = metadata.author.toStdString();
    for (const auto& tag : metadata.tags)
        preset.tags.push_back(tag.toStdString());
    preset.created = (metadata.created.isNotEmpty() ? metadata.created : nowUtc()).toStdString();
    preset.valueDomain =
        internal ? sdk::PresetValueDomain::Display : sdk::PresetValueDomain::Normalized;

    if (internal) {
        sdk::StateDocument doc;
        if (device.hasPluginState()) {
            auto normalised = daw::audio::normaliseDeviceState(device.pluginState);
            if (!normalised || !normalised->dropped.empty())
                return std::nullopt;
            doc = std::move(normalised->document);
        } else {
            doc.deviceType = device.pluginId.toStdString();
        }
        if (doc.deviceType.empty())
            return std::nullopt;
        if (device.pluginId == kSamplerId && !moveSampleToAsset(doc, presetFile, preset.assets))
            return std::nullopt;

        preset.deviceType = doc.deviceType;
        if (const auto declared = daw::audio::createDetachedDevice(device.pluginId))
            preset.deviceVersion = declared->properties().deviceVersion;
        preset.device = sdk::PresetMagdaDevice{std::move(doc)};
    } else {
        const auto* name = formatName(device.format);
        if (name == nullptr)
            return std::nullopt;
        preset.deviceType =
            (device.pluginId.isNotEmpty() ? device.pluginId : device.uniqueId).toStdString();

        sdk::PresetPluginDevice plugin;
        plugin.format = name;
        plugin.uniqueId = device.uniqueId.toStdString();
        plugin.fileOrIdentifier = device.fileOrIdentifier.toStdString();
        if (device.vst3ClassId.isNotEmpty())
            plugin.vst3ClassId = device.vst3ClassId.toStdString();

        // Re-encoding must give the text back, or loading would not return the same device.
        if (device.hasPluginState()) {
            juce::MemoryBlock chunk;
            if (!chunk.fromBase64Encoding(device.pluginState) ||
                chunk.toBase64Encoding() != device.pluginState)
                return std::nullopt;
            const auto* bytes = static_cast<const std::uint8_t*>(chunk.getData());
            plugin.chunk.assign(bytes, bytes + chunk.getSize());
        }
        if (device.vst3Preset.isNotEmpty()) {
            juce::MemoryOutputStream decoded;
            if (!juce::Base64::convertFromBase64(decoded, device.vst3Preset) ||
                juce::Base64::toBase64(decoded.getData(), decoded.getDataSize()) !=
                    device.vst3Preset)
                return std::nullopt;
            const auto* bytes = static_cast<const std::uint8_t*>(decoded.getData());
            plugin.vst3Preset = sdk::Binary(bytes, bytes + decoded.getDataSize());
        }
        preset.device = std::move(plugin);
    }

    auto payloadVar = ProjectSerializer::serializeDeviceInfo(device);
    auto* payload = payloadVar.getDynamicObject();
    const auto parameterObjects = payload->getProperty("parameters");
    if (!parameterObjects.isArray() ||
        parameterObjects.getArray()->size() != static_cast<int>(device.parameters.size()))
        return std::nullopt;

    // Each parameter's value goes in the portable member; the rest of its record is host data.
    juce::Array<juce::var> parameterConfig;
    std::map<int, juce::String> idByIndex;
    std::set<juce::String> seen;
    for (int position = 0; position < static_cast<int>(device.parameters.size()); ++position) {
        const auto& parameter = device.parameters[static_cast<std::size_t>(position)];
        const auto id = stableIdOf(device.pluginId, parameter, position);
        const int index = parameter.paramIndex >= 0 ? parameter.paramIndex : position;
        if (parameter.valueConvention != convention || !std::isfinite(parameter.currentValue) ||
            !seen.insert(id).second)
            return std::nullopt;

        preset.parameters.push_back(
            {id.toStdString(), static_cast<double>(parameter.currentValue)});
        idByIndex[index] = id;

        auto* record = new juce::DynamicObject();
        record->setProperty("id", id);
        record->setProperty("index", index);
        const auto* serialised = (*parameterObjects.getArray())[position].getDynamicObject();
        for (const auto& property : serialised->getProperties())
            if (property.name.toString() != "paramIndex" &&
                property.name.toString() != "currentValue")
                record->setProperty(property.name, property.value);
        parameterConfig.add(juce::var(record));
    }

    const auto idOf = [&](int index) {
        const auto it = idByIndex.find(index);
        return it != idByIndex.end() ? it->second : derivedId(device.pluginId, index);
    };
    for (const char* owners : {"macros", "mods"})
        keyLinksById(payload->getProperty(owners), idOf);

    for (const char* moved : {"parameters", "pluginState", "format"})
        payload->removeProperty(moved);
    if (!internal)
        for (const char* moved : {"uniqueId", "fileOrIdentifier"})
            payload->removeProperty(moved);

    auto* section = new juce::DynamicObject();
    section->setProperty("version", kHostVersion);
    section->setProperty("device", payloadVar);
    section->setProperty("parameters", juce::var(parameterConfig));
    auto* host = new juce::DynamicObject();
    host->setProperty(kHostKey, juce::var(section));
    preset.host = juce::JSON::toString(juce::var(host), true).toStdString();

    std::string error;
    const auto text = sdk::writePreset(preset, error);
    if (!text)
        return std::nullopt;
    return utf8(*text);
}

ReadResult readEnvelope(const juce::String& text, const juce::File& presetFile,
                        DeviceInfo& device) {
    ReadResult result;
    const auto read = sdk::readPreset(text.toStdString());
    switch (read.status) {
        case sdk::PresetStatus::NotJson:
        case sdk::PresetStatus::NotAPreset:
            return result;
        case sdk::PresetStatus::FutureVersion:
            result.status = ReadStatus::FromNewerVersion;
            result.error = utf8(read.message);
            return result;
        case sdk::PresetStatus::UnsupportedVersion:
        case sdk::PresetStatus::Invalid:
            result.status = ReadStatus::Invalid;
            result.error = utf8(read.message);
            return result;
        case sdk::PresetStatus::Ok:
            break;
    }

    const auto& preset = *read.preset;
    const auto fail = [&result](const juce::String& message) {
        result.status = ReadStatus::Invalid;
        result.error = message;
        return result;
    };

    juce::var hostVar;
    if (preset.host)
        hostVar = juce::JSON::parse(utf8(*preset.host));
    const auto section = hostVar[kHostKey];
    if (section.isObject() && static_cast<int>(section["version"]) > kHostVersion)
        result.warnings.add("The host data is from a newer version and was read as far as known");

    auto* payload = new juce::DynamicObject();
    juce::var payloadVar(payload);
    if (const auto* saved = section["device"].getDynamicObject()) {
        for (const auto& property : saved->getProperties())
            payload->setProperty(property.name, property.value);
    } else {
        payload->setProperty("pluginId", utf8(preset.deviceType));
        payload->setProperty("name", utf8(preset.deviceType));
    }

    const bool internal = std::holds_alternative<sdk::PresetMagdaDevice>(preset.device);
    const auto pluginId = payload->getProperty("pluginId").toString();
    juce::String pluginState;
    std::optional<sdk::PresetPluginDevice> plugin;
    if (internal) {
        auto doc = std::get<sdk::PresetMagdaDevice>(preset.device).state;

        std::map<std::string, juce::String> assetPaths;
        const auto resolution =
            sdk::resolveAssets(preset, [&](const sdk::PresetAsset& asset) -> sdk::AssetLookup {
                const auto file = assetFile(presetFile, asset.path);
                assetPaths[asset.key] = file.getFullPathName();
                if (!file.existsAsFile())
                    return {sdk::AssetLookup::Status::Missing, {}};
                const auto location = file.getFullPathName().toStdString();
                if (asset.sha256 && sha256Of(file).toStdString() != *asset.sha256)
                    return {sdk::AssetLookup::Status::Mismatch, location};
                return {sdk::AssetLookup::Status::Found, location};
            });
        for (const auto& asset : resolution.assets) {
            if (asset.lookup.status == sdk::AssetLookup::Status::Missing)
                result.warnings.add("Asset '" + utf8(asset.key) + "' was not found at " +
                                    assetPaths[asset.key]);
            else if (asset.lookup.status == sdk::AssetLookup::Status::Mismatch)
                result.warnings.add("Asset '" + utf8(asset.key) + "' differs from the saved copy");
        }

        if (pluginId == kSamplerId)
            if (const auto* source = doc.root.find(kSamplePathProperty))
                if (const auto it = assetPaths.find(source->toString()); it != assetPaths.end())
                    doc.root.setString(kSamplePathProperty, it->second.toStdString());

        // An empty document is a device that saved nothing.
        if (!doc.root.isEmpty())
            pluginState = utf8(sdk::encodeDocument(doc).value_or(std::string()));
        payload->setProperty("format", static_cast<int>(PluginFormat::Internal));
    } else {
        plugin = std::get<sdk::PresetPluginDevice>(preset.device);
        const auto format = formatFromName(plugin->format);
        if (!format)
            return fail("Unknown plugin format '" + utf8(plugin->format) + "'");
        payload->setProperty("format", static_cast<int>(*format));
        payload->setProperty("uniqueId", utf8(plugin->uniqueId));
        payload->setProperty("fileOrIdentifier", utf8(plugin->fileOrIdentifier));
        if (!plugin->chunk.empty())
            pluginState =
                juce::MemoryBlock(plugin->chunk.data(), plugin->chunk.size()).toBase64Encoding();
    }
    if (pluginState.isNotEmpty())
        payload->setProperty("pluginState", pluginState);

    // Parameters: ids back to slots, values from the portable member.
    SlotResolver resolver(pluginId, internal, pluginState);
    std::map<juce::String, double> values;
    for (const auto& parameter : preset.parameters)
        values[utf8(parameter.id)] = parameter.value;

    juce::Array<juce::var> parameterRecords;
    std::set<juce::String> recorded;
    const auto config = section["parameters"];
    for (const auto& entry : config.isArray() ? *config.getArray() : juce::Array<juce::var>{}) {
        const auto* saved = entry.getDynamicObject();
        const auto id = saved != nullptr ? saved->getProperty("id").toString() : juce::String();
        if (id.isEmpty())
            continue;
        resolver.hint(id, saved->getProperty("index"));
        const auto index = resolver.indexOf(id);
        if (!index) {
            result.warnings.add("Parameter '" + id + "' is not one of this device's parameters");
            continue;
        }

        auto* record = new juce::DynamicObject();
        for (const auto& property : saved->getProperties())
            if (property.name.toString() != "id" && property.name.toString() != "index")
                record->setProperty(property.name, property.value);
        record->setProperty("paramIndex", *index);
        const auto value = values.find(id);
        record->setProperty("currentValue",
                            value != values.end()
                                ? value->second
                                : static_cast<double>(saved->getProperty("defaultValue")));
        parameterRecords.add(juce::var(record));
        recorded.insert(id);
    }
    payload->setProperty("parameters", juce::var(parameterRecords));

    struct Loose {
        juce::String id;
        int index;
        double value;
    };
    std::vector<Loose> loose;
    for (const auto& [id, value] : values) {
        if (recorded.count(id) != 0)
            continue;
        if (const auto index = resolver.indexOf(id))
            loose.push_back({id, *index, value});
        else
            result.warnings.add("Parameter '" + id + "' is not one of this device's parameters");
    }

    // Links name a parameter by id; a link to one this device lacks is dropped.
    for (const char* owners : {"macros", "mods"}) {
        auto ownerArray = payload->getProperty(owners);
        for (auto& owner :
             ownerArray.isArray() ? *ownerArray.getArray() : juce::Array<juce::var>{}) {
            auto* ownerObject = owner.getDynamicObject();
            auto links = ownerObject != nullptr ? ownerObject->getProperty("links") : juce::var();
            if (!links.isArray())
                continue;

            auto* linkArray = links.getArray();
            for (int i = linkArray->size(); --i >= 0;) {
                auto* target = (*linkArray)[i]["target"].getDynamicObject();
                if (target == nullptr || !target->hasProperty("stableId"))
                    continue;
                const auto id = target->getProperty("stableId").toString();
                if (const auto index = resolver.indexOf(id)) {
                    target->removeProperty("stableId");
                    target->setProperty("paramIndex", *index);
                } else {
                    result.warnings.add("A link to parameter '" + id + "' was dropped");
                    linkArray->remove(i);
                }
            }
            ownerObject->setProperty("links", links);
        }
        payload->setProperty(owners, ownerArray);
    }

    if (!ProjectSerializer::deserializeDeviceInfo(payloadVar, device))
        return fail("Failed to deserialize device: " + ProjectSerializer::getLastError());

    if (plugin) {
        device.vst3ClassId = plugin->vst3ClassId ? utf8(*plugin->vst3ClassId) : juce::String();
        if (plugin->vst3Preset)
            device.vst3Preset =
                juce::Base64::toBase64(plugin->vst3Preset->data(), plugin->vst3Preset->size());
    }

    legacy_devices::migrateRetiredDevice(device);
    daw::audio::device_state_hydration::completeDeviceParameters(device);

    // A parameter the preset has a value for but no record of: the declaration supplied the entry.
    for (const auto& value : loose) {
        if (auto* parameter = device.findParameterByIndex(value.index)) {
            parameter->currentValue = static_cast<float>(value.value);
        } else if (!internal) {
            ParameterInfo parameter;
            parameter.paramIndex = value.index;
            parameter.name = value.id;
            parameter.valueConvention = ParameterValueConvention::Normalized;
            parameter.defaultValue = parameter.currentValue = static_cast<float>(value.value);
            if (value.id != derivedId(device.pluginId, value.index))
                parameter.stableId = value.id;
            device.parameters.push_back(std::move(parameter));
        } else {
            result.warnings.add("Parameter '" + value.id +
                                "' is not one of this device's parameters");
        }
    }

    result.status = ReadStatus::Loaded;
    return result;
}

std::optional<juce::String> rebaseAssets(const juce::String& text, const juce::File& from,
                                         const juce::File& to) {
    auto read = sdk::readPreset(text.toStdString());
    if (!read.ok())
        return std::nullopt;

    for (auto& asset : read.preset->assets) {
        const auto relative = assetFile(from, asset.path)
                                  .getRelativePathFrom(to.getParentDirectory())
                                  .replaceCharacter('\\', '/')
                                  .toStdString();
        if (!sdk::isValidAssetPath(relative))
            return std::nullopt;
        asset.path = relative;
    }

    std::string error;
    const auto rewritten = sdk::writePreset(*read.preset, error);
    if (!rewritten)
        return std::nullopt;
    return utf8(*rewritten);
}

}  // namespace magda::device_preset
