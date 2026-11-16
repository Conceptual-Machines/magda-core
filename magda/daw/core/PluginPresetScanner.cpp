// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PluginPresetScanner.hpp"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <utility>

#include "audio/Vst3Preset.hpp"

namespace magda {

namespace {

juce::String sanitiseFolderName(const juce::String& name) {
    return juce::File::createLegalFileName(name.trim());
}

/// An AU's subtype and manufacturer codes, e.g. {"Smpl", "appl"}.
using AuIdentity = std::pair<juce::String, juce::String>;

/** @brief A FourCC stored as a plist integer, as its four characters. */
juce::String fourCC(juce::int64 value) {
    const auto v = static_cast<juce::uint32>(value);
    const char chars[] = {static_cast<char>(v >> 24), static_cast<char>(v >> 16),
                          static_cast<char>(v >> 8), static_cast<char>(v)};
    return {chars, 4};
}

/** @brief The plugin an XML .aupreset names; none when it is binary or says nothing. */
std::optional<AuIdentity> auPresetIdentity(const juce::File& file) {
    const auto plist = juce::XmlDocument::parse(file);
    const auto* dict = plist != nullptr ? plist->getChildByName("dict") : nullptr;
    if (dict == nullptr)
        return std::nullopt;

    AuIdentity identity;
    for (const auto* key = dict->getFirstChildElement(); key != nullptr;
         key = key->getNextElement()) {
        const auto* value = key->getNextElement();
        if (!key->hasTagName("key") || value == nullptr || !value->hasTagName("integer"))
            continue;
        const auto code = fourCC(value->getAllSubText().getLargeIntValue());
        if (key->getAllSubText() == "subtype")
            identity.first = code;
        else if (key->getAllSubText() == "manufacturer")
            identity.second = code;
    }
    if (identity.first.isEmpty() || identity.second.isEmpty())
        return std::nullopt;
    return identity;
}

/** @brief An AU device's subtype and manufacturer, from JUCE's "AudioUnit:Synths/aumu,Smpl,appl".
 */
std::optional<AuIdentity> auDeviceIdentity(const DeviceInfo& device) {
    const auto codes = juce::StringArray::fromTokens(
        device.fileOrIdentifier.fromFirstOccurrenceOf("/", false, false), ",", "");
    if (codes.size() != 3)
        return std::nullopt;
    return AuIdentity{codes[1], codes[2]};
}

juce::String extensionFor(PluginFormat format) {
    switch (format) {
        case PluginFormat::VST3:
            return ".vstpreset";
        case PluginFormat::AU:
            return ".aupreset";
        default:
            return {};
    }
}

// User-writable preset roots, per plugin format and OS.
// Issue #1118 spec — these are the canonical locations that hosts and
// installers use when writing presets.
std::vector<juce::File> userRootsFor(PluginFormat format) {
    std::vector<juce::File> roots;
#if JUCE_MAC
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    if (format == PluginFormat::VST3 || format == PluginFormat::AU)
        roots.push_back(home.getChildFile("Library/Audio/Presets"));
#elif JUCE_WINDOWS
    if (format == PluginFormat::VST3) {
        auto local = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
        roots.push_back(local.getChildFile("VST3 Presets"));
    }
#elif JUCE_LINUX
    if (format == PluginFormat::VST3) {
        auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        roots.push_back(home.getChildFile(".vst3/presets"));
    }
#endif
    return roots;
}

// System / shared preset roots for installed factory content.
std::vector<juce::File> systemRootsFor(PluginFormat format) {
    std::vector<juce::File> roots;
#if JUCE_MAC
    if (format == PluginFormat::VST3 || format == PluginFormat::AU)
        roots.emplace_back("/Library/Audio/Presets");
#elif JUCE_WINDOWS
    if (format == PluginFormat::VST3) {
        auto common = juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory);
        roots.push_back(common.getChildFile("VST3 Presets"));
        // Some installers use Program Files\Common Files\VST3 Presets
        if (auto programFilesCommon = []() -> juce::File {
                auto* p = std::getenv("CommonProgramFiles");
                return p != nullptr ? juce::File(juce::String::fromUTF8(p)) : juce::File();
            }();
            programFilesCommon != juce::File()) {
            roots.push_back(programFilesCommon.getChildFile("VST3 Presets"));
        }
    }
#elif JUCE_LINUX
    if (format == PluginFormat::VST3) {
        roots.push_back(juce::File("/usr/share/vst3/presets"));
        roots.push_back(juce::File("/usr/local/share/vst3/presets"));
    }
#endif
    return roots;
}

// Scan a directory for preset files matching `extension`, recursing into
// subfolders. Files become leaf entries; folders become Entry with children.
// Sorted: folders first, then files, alphabetical within each group.
PluginPresetScanner::Entry scanDir(const juce::File& dir, const juce::String& extension) {
    PluginPresetScanner::Entry node;
    node.name = dir.getFileName();
    node.isFolder = true;

    auto subdirs = dir.findChildFiles(juce::File::findDirectories, false);
    auto files = dir.findChildFiles(juce::File::findFiles, false, "*" + extension);
    subdirs.sort();
    files.sort();

    for (const auto& sub : subdirs) {
        auto child = scanDir(sub, extension);
        if (!child.children.empty())
            node.children.push_back(std::move(child));
    }
    for (const auto& f : files) {
        PluginPresetScanner::Entry leaf;
        leaf.name = f.getFileNameWithoutExtension();
        leaf.file = f;
        node.children.push_back(std::move(leaf));
    }
    return node;
}

// Order siblings: folders first, files second, alphabetical within each
// group. mergeInto() appends across scan roots and so cannot preserve the
// per-directory sort scanDir() produces — sortEntries() restores it.
void sortEntries(std::vector<PluginPresetScanner::Entry>& entries) {
    std::sort(entries.begin(), entries.end(),
              [](const PluginPresetScanner::Entry& a, const PluginPresetScanner::Entry& b) {
                  if (a.isFolder != b.isFolder)
                      return a.isFolder;
                  return a.name.compareNatural(b.name) < 0;
              });
    for (auto& e : entries)
        if (e.isFolder)
            sortEntries(e.children);
}

// Merge another root's children into target, collapsing folders by name so
// presets in `~/Library/Audio/Presets/Vendor/Plugin/Bass` and
// `/Library/Audio/Presets/Vendor/Plugin/Bass` end up in one "Bass" submenu.
void mergeInto(std::vector<PluginPresetScanner::Entry>& target,
               std::vector<PluginPresetScanner::Entry>&& source) {
    for (auto& src : source) {
        if (src.isFolder) {
            auto it = std::find_if(target.begin(), target.end(),
                                   [&](const PluginPresetScanner::Entry& e) {
                                       return e.isFolder && e.name == src.name;
                                   });
            if (it != target.end()) {
                mergeInto(it->children, std::move(src.children));
                continue;
            }
        }
        target.push_back(std::move(src));
    }
}

}  // namespace

PluginPresetScanner& PluginPresetScanner::getInstance() {
    static PluginPresetScanner instance;
    return instance;
}

juce::String PluginPresetScanner::makeCacheKey(const DeviceInfo& device) {
    return device.getFormatString() + "|" + device.manufacturer + "|" + device.name;
}

juce::String PluginPresetScanner::getPresetExtension(const DeviceInfo& device) {
    return extensionFor(device.format);
}

bool PluginPresetScanner::presetFitsDevice(const juce::File& file, const DeviceInfo& device) {
    const auto extension = extensionFor(device.format);
    if (extension.isEmpty() || !file.hasFileExtension(extension) || !file.existsAsFile())
        return false;

    if (device.format == PluginFormat::VST3) {
        // Before the device has reported its class, the plugin's own loader checks it.
        if (device.vst3ClassId.isEmpty())
            return true;
        juce::FileInputStream in(file);
        juce::MemoryBlock header;
        if (!in.openedOk())
            return false;
        in.readIntoMemoryBlock(header,
                               vst3::kVst3PresetClassIdOffset + vst3::kVst3PresetClassIdLength);
        return vst3::classIdFromPreset(header).equalsIgnoreCase(device.vst3ClassId);
    }

    // An AU turns away another plugin's preset without saying so, and the slot would take its
    // name anyway. A binary plist goes to the plugin unchecked.
    const auto preset = auPresetIdentity(file);
    const auto plugin = auDeviceIdentity(device);
    return !preset.has_value() || !plugin.has_value() || *preset == *plugin;
}

std::vector<juce::File> PluginPresetScanner::getScanRoots(const DeviceInfo& device) {
    std::vector<juce::File> out;
    auto vendor = sanitiseFolderName(device.manufacturer);
    auto plugin = sanitiseFolderName(device.name);
    if (vendor.isEmpty() || plugin.isEmpty())
        return out;

    auto roots = userRootsFor(device.format);
    auto sysRoots = systemRootsFor(device.format);
    roots.insert(roots.end(), sysRoots.begin(), sysRoots.end());

    for (const auto& root : roots) {
        auto pluginDir = root.getChildFile(vendor).getChildFile(plugin);
        if (pluginDir.isDirectory())
            out.push_back(pluginDir);
    }
    return out;
}

juce::File PluginPresetScanner::getUserPresetDirectory(const DeviceInfo& device) {
    auto vendor = sanitiseFolderName(device.manufacturer);
    auto plugin = sanitiseFolderName(device.name);
    if (vendor.isEmpty() || plugin.isEmpty())
        return {};
    auto roots = userRootsFor(device.format);
    if (roots.empty())
        return {};
    return roots.front().getChildFile(vendor).getChildFile(plugin);
}

PluginPresetScanner::PresetTree PluginPresetScanner::scanPlugin(const DeviceInfo& device) const {
    PresetTree tree;
    auto extension = extensionFor(device.format);
    if (extension.isEmpty())
        return tree;

    for (const auto& dir : getScanRoots(device)) {
        auto rootEntry = scanDir(dir, extension);
        if (!rootEntry.children.empty())
            mergeInto(tree.roots, std::move(rootEntry.children));
    }
    sortEntries(tree.roots);
    return tree;
}

const PluginPresetScanner::PresetTree& PluginPresetScanner::getPresets(const DeviceInfo& device) {
    auto key = makeCacheKey(device).toStdString();
    auto it = cache_.find(key);
    if (it != cache_.end())
        return it->second;
    auto [inserted, _] = cache_.emplace(key, scanPlugin(device));
    return inserted->second;
}

void PluginPresetScanner::rescan(const DeviceInfo& device) {
    auto key = makeCacheKey(device).toStdString();
    cache_[key] = scanPlugin(device);
}

}  // namespace magda
