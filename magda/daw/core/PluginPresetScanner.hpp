#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <unordered_map>
#include <vector>

#include "DeviceInfo.hpp"

namespace magda {

/**
 * @brief Scans disk-based plugin presets (.vstpreset, .aupreset) per plugin.
 *
 * Modern VST3 / AU plugins store their factory and user presets as files on
 * disk rather than exposing them through the legacy Programs API. This class
 * walks the OS-defined preset locations for a given plugin and returns a tree
 * suitable for building a PopupMenu.
 *
 * Results are cached per (format, vendor, plugin) and stay valid until
 * `rescan()` is called. The user-writable directory (returned from
 * `getUserPresetDirectory()`) is where new presets saved from MAGDA land.
 */
class PluginPresetScanner {
  public:
    static PluginPresetScanner& getInstance();

    /** Single preset entry (directory or file). */
    struct Entry {
        juce::String name;  // Display name (file basename or folder name)
        juce::File file;    // Empty for folders
        bool isFolder = false;
        std::vector<Entry> children;  // Populated when isFolder
    };

    /** Tree of presets. Top-level entries are direct children of all scan roots
     *  merged together — folders coming from any root with the same name are
     *  collapsed into one entry. */
    struct PresetTree {
        std::vector<Entry> roots;
        bool empty() const {
            return roots.empty();
        }
    };

    /**
     * @brief Returns a cached scan result for the given device.
     *
     * @param device A loaded external plugin device. Internal devices return
     *        an empty tree.
     * @return Tree of presets discovered on disk. Empty if no presets exist.
     */
    const PresetTree& getPresets(const DeviceInfo& device);

    /**
     * @brief Force a rescan for the given plugin identity. Cheap (a few
     *        directory listings); safe to call from the message thread.
     */
    void rescan(const DeviceInfo& device);

    /**
     * @brief The user-writable directory where MAGDA writes new presets for
     *        this plugin. Created on demand (only when actually written to).
     */
    juce::File getUserPresetDirectory(const DeviceInfo& device) const;

    /**
     * @brief Filesystem extension expected for the device's plugin format
     *        (".vstpreset", ".aupreset", or empty for unsupported formats).
     */
    juce::String getPresetExtension(const DeviceInfo& device) const;

    /**
     * @brief All directories (user + system) scanned for the device. Useful
     *        for exposing a "Reveal in Finder" action for the user dir.
     */
    std::vector<juce::File> getScanRoots(const DeviceInfo& device) const;

  private:
    PluginPresetScanner() = default;

    juce::String makeCacheKey(const DeviceInfo& device) const;
    PresetTree scanPlugin(const DeviceInfo& device) const;

    std::unordered_map<std::string, PresetTree> cache_;
};

}  // namespace magda
