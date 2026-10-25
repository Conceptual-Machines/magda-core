#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "SettingsLink.hpp"

namespace magda {

class Config;

/**
 * @brief One sidebar entry of the Settings window: its tabs and how its edits apply.
 *
 * A buffered section keeps edits in its controls until Apply. writeTo() must only
 * set Config values so it can run on a scratch copy; the pending-change count and
 * Restore defaults are both built on it.
 */
class SettingsSection {
  public:
    struct Tab {
        juce::String title;
        juce::Component* component = nullptr;
        /// Height the tab needs at a width; empty fills the page without scrolling.
        std::function<int(int)> preferredHeight;
        /// The tab's actions, shown at the right of the tab row.
        juce::Component* toolbar = nullptr;
    };

    explicit SettingsSection(SettingsSectionId id);
    virtual ~SettingsSection();

    SettingsSectionId getId() const {
        return id_;
    }
    virtual juce::String getTitle() const = 0;
    /** @brief The section's tabs; the section owns their components. */
    virtual std::vector<Tab> getTabs() = 0;

    /** @brief True when every edit takes effect as it is made; Apply and Cancel skip it. */
    virtual bool appliesImmediately() const {
        return false;
    }

    /** @brief Show the values in @p config. */
    virtual void load(Config& config) {
        juce::ignoreUnused(config);
    }
    /** @brief Set the edited values on @p config, with no other side effect. */
    virtual void writeTo(Config& config) {
        juce::ignoreUnused(config);
    }
    /** @brief Act on the saved edits (reload a language, rescale the UI...). */
    virtual void commit(const Config& before) {
        juce::ignoreUnused(before);
    }

    /** @brief How many settings differ from what was loaded. */
    virtual int countChanges();
    /** @brief Show the default values as pending edits. */
    virtual void restoreDefaults();

    /** @brief The tab to open for a deep-link name that is not a tab title, or -1. */
    virtual int findTab(const juce::String& name) {
        juce::ignoreUnused(name);
        return -1;
    }
    /** @brief Tab @p index has just been shown. */
    virtual void tabShown(int index) {
        juce::ignoreUnused(index);
    }
    /** @brief The window is closing; save anything a live section defers. */

    virtual void windowClosing() {}

    /** @brief Remember the loaded state that countChanges() compares against. */
    void captureBaseline();

    /** @brief Leaf values that differ between two Config::toVar() trees. */
    static int countDifferences(const juce::var& a, const juce::var& b);

  private:
    SettingsSectionId id_;
    std::unique_ptr<Config> snapshot_;
    juce::var baseline_;
};

}  // namespace magda
