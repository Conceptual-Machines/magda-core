#pragma once

#include <map>

#include "ViewModeState.hpp"

namespace magda {

/**
 * @brief View-specific settings for a track
 *
 * Each track can have different visibility, lock state, and display
 * properties in each view mode.
 */
struct TrackViewSettings {
    bool visible = true;     // Show in this view
    bool locked = false;     // Prevent editing in this view
    bool collapsed = false;  // For groups: collapse children
    int height = 80;         // Track height in arrangement view (pixels)

    bool operator==(const TrackViewSettings& other) const {
        return visible == other.visible && locked == other.locked && collapsed == other.collapsed &&
               height == other.height;
    }
};

/**
 * @brief Default view settings for each view mode
 */
inline TrackViewSettings getDefaultViewSettings(ViewMode mode) {
    TrackViewSettings settings;

    switch (mode) {
        case ViewMode::Live:
            settings.height = 60;
            break;
        case ViewMode::Arrange:
            settings.height = 83;
            break;
        case ViewMode::Mix:
            settings.visible = true;  // Always show in mix
            break;
        case ViewMode::Master:
            settings.visible = false;  // Usually hide individual tracks
            break;
    }

    return settings;
}

/**
 * @brief Collection of view settings for all view modes
 */
class TrackViewSettingsMap {
  public:
    TrackViewSettingsMap() {
        // Initialize with defaults for each view mode
        settings_[ViewMode::Live] = getDefaultViewSettings(ViewMode::Live);
        settings_[ViewMode::Arrange] = getDefaultViewSettings(ViewMode::Arrange);
        settings_[ViewMode::Mix] = getDefaultViewSettings(ViewMode::Mix);
        settings_[ViewMode::Master] = getDefaultViewSettings(ViewMode::Master);
    }

    TrackViewSettings& get(ViewMode mode) {
        return settings_[mode];
    }

    const TrackViewSettings& get(ViewMode mode) const {
        auto it = settings_.find(mode);
        if (it != settings_.end()) {
            return it->second;
        }
        static TrackViewSettings defaultSettings;
        return defaultSettings;
    }

    void set(ViewMode mode, const TrackViewSettings& settings) {
        settings_[mode] = settings;
    }

    // Convenience accessors
    bool isVisible(ViewMode mode) const {
        return get(mode).visible;
    }
    bool isLocked(ViewMode mode) const {
        return get(mode).locked;
    }
    bool isCollapsed(ViewMode mode) const {
        return get(mode).collapsed;
    }
    int getHeight(ViewMode mode) const {
        return get(mode).height;
    }

    void setVisible(ViewMode mode, bool visible) {
        settings_[mode].visible = visible;
    }
    void setLocked(ViewMode mode, bool locked) {
        settings_[mode].locked = locked;
    }
    void setCollapsed(ViewMode mode, bool collapsed) {
        settings_[mode].collapsed = collapsed;
    }
    void setHeight(ViewMode mode, int height) {
        settings_[mode].height = height;
    }

  private:
    std::map<ViewMode, TrackViewSettings> settings_;
};

}  // namespace magda
