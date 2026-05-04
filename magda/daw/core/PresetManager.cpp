#include "PresetManager.hpp"

#include "../project/serialization/ProjectSerializer.hpp"
#include "AppPaths.hpp"
#include "version.hpp"

namespace magda {

namespace {
constexpr const char* kPresetExtension = ".mps";
constexpr const char* kKindChain = "chain";
constexpr const char* kKindRack = "rack";
constexpr const char* kKindDevice = "device";

// Sanitize a user-supplied preset name into something filesystem-safe.
juce::String sanitizeName(const juce::String& name) {
    auto sanitized = juce::File::createLegalFileName(name.trim());
    if (sanitized.isEmpty())
        sanitized = "Untitled";
    return sanitized;
}

// Sanitize each segment of a slash-separated relative path so the user can
// nest presets in subfolders ("Bass/Reese 808" → "Bass/Reese 808.mps").
juce::String sanitizeRelativePath(const juce::String& relativePath) {
    juce::StringArray parts;
    parts.addTokens(relativePath, "/", "");
    juce::StringArray cleaned;
    for (const auto& part : parts) {
        auto trimmed = part.trim();
        if (trimmed.isEmpty())
            continue;
        cleaned.add(juce::File::createLegalFileName(trimmed));
    }
    if (cleaned.isEmpty())
        cleaned.add("Untitled");
    return cleaned.joinIntoString("/");
}

// Walk a preset directory tree, appending forward-slash relative paths
// (no extension) for every .mps file encountered. Directories first then
// files, alphabetical within each, so the menu order is predictable.
void collectPresetsRecursive(const juce::File& root, const juce::String& prefix,
                             juce::StringArray& out) {
    if (!root.isDirectory())
        return;
    auto subdirs = root.findChildFiles(juce::File::findDirectories, false);
    auto files = root.findChildFiles(juce::File::findFiles, false, "*.mps");
    subdirs.sort();
    files.sort();
    for (const auto& sub : subdirs)
        collectPresetsRecursive(sub, prefix + sub.getFileName() + "/", out);
    for (const auto& f : files)
        out.add(prefix + f.getFileNameWithoutExtension());
}

// Wrap payload in the standard envelope and write pretty JSON.
bool writePresetFile(const juce::File& target, const juce::String& kind, const juce::var& payload,
                     juce::String& outError) {
    auto* envelope = new juce::DynamicObject();
    envelope->setProperty("magdaVersion", juce::String(MAGDA_VERSION));
    envelope->setProperty("kind", kind);
    envelope->setProperty("payload", payload);

    juce::var root(envelope);
    auto json = juce::JSON::toString(root, /*allOnOneLine*/ false);

    target.getParentDirectory().createDirectory();
    if (!target.replaceWithText(json)) {
        outError = "Failed to write preset file: " + target.getFullPathName();
        return false;
    }
    return true;
}

// Parse a preset file, validate the envelope, and return the payload.
bool readPresetFile(const juce::File& source, const juce::String& expectedKind,
                    juce::var& outPayload, juce::String& outError) {
    if (!source.existsAsFile()) {
        outError = "Preset file not found: " + source.getFullPathName();
        return false;
    }

    auto text = source.loadFileAsString();
    auto root = juce::JSON::parse(text);
    if (!root.isObject()) {
        outError = "Preset file is not a JSON object: " + source.getFullPathName();
        return false;
    }

    auto* obj = root.getDynamicObject();
    auto kind = obj->getProperty("kind").toString();
    if (kind != expectedKind) {
        outError = "Preset kind mismatch (expected '" + expectedKind + "', got '" + kind + "')";
        return false;
    }

    outPayload = obj->getProperty("payload");
    return true;
}

}  // namespace

PresetManager& PresetManager::getInstance() {
    static PresetManager instance;
    return instance;
}

PresetManager::PresetManager() {
    // Ensure preset directories exist
    ensureDirectoryExists(getChainsDirectory());
    ensureDirectoryExists(getRacksDirectory());
    ensureDirectoryExists(getDevicesDirectory());
}

// ============================================================================
// Preset Directories
// ============================================================================

juce::File PresetManager::getPresetsDirectory() const {
    return magda::paths::presetsDir();
}

juce::File PresetManager::getChainsDirectory() const {
    return getPresetsDirectory().getChildFile("Chains");
}

juce::File PresetManager::getRacksDirectory() const {
    return getPresetsDirectory().getChildFile("Racks");
}

juce::File PresetManager::getDevicesDirectory() const {
    return getPresetsDirectory().getChildFile("Devices");
}

// ============================================================================
// Chain Presets
// ============================================================================

bool PresetManager::saveChainPreset(const std::vector<ChainElement>& chainElements,
                                    const juce::String& presetName) {
    juce::Array<juce::var> elementsArray;
    for (const auto& element : chainElements)
        elementsArray.add(ProjectSerializer::serializeChainElement(element));

    auto* payload = new juce::DynamicObject();
    payload->setProperty("elements", juce::var(elementsArray));

    auto target =
        getChainsDirectory().getChildFile(sanitizeRelativePath(presetName) + kPresetExtension);
    return writePresetFile(target, kKindChain, juce::var(payload), lastError_);
}

bool PresetManager::loadChainPreset(const juce::String& presetName,
                                    std::vector<ChainElement>& outChainElements) {
    auto source =
        getChainsDirectory().getChildFile(sanitizeRelativePath(presetName) + kPresetExtension);
    juce::var payload;
    if (!readPresetFile(source, kKindChain, payload, lastError_))
        return false;

    if (!payload.isObject()) {
        lastError_ = "Chain preset payload is not an object";
        return false;
    }
    auto elementsVar = payload.getDynamicObject()->getProperty("elements");
    if (!elementsVar.isArray()) {
        lastError_ = "Chain preset 'elements' is not an array";
        return false;
    }

    outChainElements.clear();
    for (const auto& elementVar : *elementsVar.getArray()) {
        ChainElement element;
        if (!ProjectSerializer::deserializeChainElement(elementVar, element)) {
            lastError_ =
                "Failed to deserialize chain element: " + ProjectSerializer::getLastError();
            outChainElements.clear();
            return false;
        }
        outChainElements.push_back(std::move(element));
    }
    return true;
}

juce::StringArray PresetManager::getChainPresets() const {
    juce::StringArray out;
    collectPresetsRecursive(getChainsDirectory(), "", out);
    return out;
}

// ============================================================================
// Rack Presets
// ============================================================================

bool PresetManager::saveRackPreset(const RackInfo& rack, const juce::String& presetName) {
    auto payload = ProjectSerializer::serializeRackInfo(rack);
    // sanitizeRelativePath lets forward slashes in `presetName` nest the
    // preset under subdirectories (e.g. "Drums/808 Stack").
    auto target =
        getRacksDirectory().getChildFile(sanitizeRelativePath(presetName) + kPresetExtension);
    return writePresetFile(target, kKindRack, payload, lastError_);
}

bool PresetManager::loadRackPreset(const juce::String& presetName, RackInfo& outRack) {
    auto source =
        getRacksDirectory().getChildFile(sanitizeRelativePath(presetName) + kPresetExtension);
    juce::var payload;
    if (!readPresetFile(source, kKindRack, payload, lastError_))
        return false;

    if (!ProjectSerializer::deserializeRackInfo(payload, outRack)) {
        lastError_ = "Failed to deserialize rack: " + ProjectSerializer::getLastError();
        return false;
    }
    return true;
}

juce::StringArray PresetManager::getRackPresets() const {
    juce::StringArray out;
    collectPresetsRecursive(getRacksDirectory(), "", out);
    return out;
}

// ============================================================================
// Device Presets
// ============================================================================

juce::File PresetManager::getDevicePluginDirectory(const juce::String& pluginFolder) const {
    return getDevicesDirectory().getChildFile(sanitizeName(pluginFolder));
}

bool PresetManager::saveDevicePreset(const DeviceInfo& device, const juce::String& presetName) {
    auto payload = ProjectSerializer::serializeDeviceInfo(device);
    auto pluginDir = getDevicePluginDirectory(device.name);
    auto target = pluginDir.getChildFile(sanitizeRelativePath(presetName) + kPresetExtension);
    return writePresetFile(target, kKindDevice, payload, lastError_);
}

bool PresetManager::loadDevicePreset(const juce::String& pluginFolder,
                                     const juce::String& presetRelativePath,
                                     DeviceInfo& outDevice) {
    auto source = getDevicePluginDirectory(pluginFolder)
                      .getChildFile(sanitizeRelativePath(presetRelativePath) + kPresetExtension);
    juce::var payload;
    if (!readPresetFile(source, kKindDevice, payload, lastError_))
        return false;

    if (!ProjectSerializer::deserializeDeviceInfo(payload, outDevice)) {
        lastError_ = "Failed to deserialize device: " + ProjectSerializer::getLastError();
        return false;
    }
    return true;
}

juce::StringArray PresetManager::getDevicePresets(const juce::String& pluginFolder) const {
    juce::StringArray out;
    collectPresetsRecursive(getDevicePluginDirectory(pluginFolder), "", out);
    return out;
}

// ============================================================================
// Private Helpers
// ============================================================================

bool PresetManager::ensureDirectoryExists(const juce::File& directory) {
    if (!directory.exists()) {
        auto result = directory.createDirectory();
        if (!result.wasOk()) {
            lastError_ = "Failed to create preset directory: " + directory.getFullPathName();
            DBG("Failed to create preset directory: " << directory.getFullPathName());
            return false;
        }
    }
    return true;
}

juce::StringArray PresetManager::getPresetList(const juce::File& directory) const {
    juce::StringArray presets;

    if (!directory.exists())
        return presets;

    auto files = directory.findChildFiles(juce::File::findFiles, false,
                                          juce::String("*") + kPresetExtension);

    for (const auto& file : files)
        presets.add(file.getFileNameWithoutExtension());

    presets.sort(true);  // Case-insensitive alphabetical sort
    return presets;
}

void PresetManager::setSuggestedPresetName(DeviceId deviceId, const juce::String& name) {
    if (name.isEmpty())
        suggestedNames_.erase(deviceId);
    else
        suggestedNames_[deviceId] = name;
}

juce::String PresetManager::getSuggestedPresetName(DeviceId deviceId) const {
    auto it = suggestedNames_.find(deviceId);
    return it != suggestedNames_.end() ? it->second : juce::String();
}

void PresetManager::clearSuggestedPresetName(DeviceId deviceId) {
    suggestedNames_.erase(deviceId);
}

}  // namespace magda
