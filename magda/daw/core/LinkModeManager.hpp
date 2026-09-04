#pragma once

#include <algorithm>
#include <vector>

#include "SelectionManager.hpp"

namespace magda {

// Link mode (Bitwig-style): exactly one modulator, mod or macro, can be in
// link mode at a time. While active, clicking any parameter creates or edits
// a link to that modulator, with an overlay slider to set the amount. It ends
// when the link button is clicked again, ESC is pressed, or another modulator
// enters link mode.
//
// Components implement LinkModeManagerListener and register with
// LinkModeManager::getInstance() to react to changes:
//
//   class MyComponent : public LinkModeManagerListener {
//       void modLinkModeChanged(bool active, const ModSelection& sel) override;
//       void macroLinkModeChanged(bool active, const MacroSelection& sel) override;
//   };
//
// Register in constructor: LinkModeManager::getInstance().addListener(this);
// Unregister in destructor: LinkModeManager::getInstance().removeListener(this);

/**
 * @brief Type of modulator in link mode
 */
enum class LinkModeType {
    None,  // No link mode active
    Mod,   // Mod is in link mode
    Macro  // Macro is in link mode
};

/**
 * @brief Listener interface for link mode changes
 */
class LinkModeManagerListener {
  public:
    virtual ~LinkModeManagerListener() = default;

    /**
     * @brief Called when mod link mode is activated or deactivated
     * @param active True if entering link mode, false if exiting
     * @param selection The mod that is in link mode (only valid if active is true)
     */
    virtual void modLinkModeChanged([[maybe_unused]] bool active,
                                    [[maybe_unused]] const ModSelection& selection) {}

    /**
     * @brief Called when macro link mode is activated or deactivated
     * @param active True if entering link mode, false if exiting
     * @param selection The macro that is in link mode (only valid if active is true)
     */
    virtual void macroLinkModeChanged([[maybe_unused]] bool active,
                                      [[maybe_unused]] const MacroSelection& selection) {}
};

/**
 * @brief Singleton manager that coordinates link mode state
 *
 * Ensures only one modulator can be in link mode at a time and notifies
 * listeners of changes.
 */
class LinkModeManager {
  public:
    static LinkModeManager& getInstance();

    // Prevent copying
    LinkModeManager(const LinkModeManager&) = delete;
    LinkModeManager& operator=(const LinkModeManager&) = delete;

    // ========================================================================
    // Link Mode State
    // ========================================================================

    LinkModeType getLinkModeType() const {
        return linkModeType_;
    }

    bool isInLinkMode() const {
        return linkModeType_ != LinkModeType::None;
    }

    // ========================================================================
    // Mod Link Mode
    // ========================================================================

    /**
     * @brief Enter link mode for a mod
     * @param parentPath Path to the device/rack/chain containing the mod
     * @param modIndex Index of the mod in the mods array
     */
    void enterModLinkMode(const ChainNodePath& parentPath, int modIndex);

    /**
     * @brief Exit mod link mode
     */
    void exitModLinkMode();

    /**
     * @brief Toggle mod link mode (enter if not active, exit if active)
     */
    void toggleModLinkMode(const ChainNodePath& parentPath, int modIndex);

    /**
     * @brief Get the current mod in link mode
     * @return Valid ModSelection only if in mod link mode
     */
    const ModSelection& getModInLinkMode() const {
        return modSelection_;
    }

    /**
     * @brief Check if a specific mod is in link mode
     */
    bool isModInLinkMode(const ChainNodePath& parentPath, int modIndex) const;

    // ========================================================================
    // Macro Link Mode
    // ========================================================================

    /**
     * @brief Enter link mode for a macro
     * @param parentPath Path to the device/rack/chain containing the macro
     * @param macroIndex Index of the macro in the macros array
     */
    void enterMacroLinkMode(const ChainNodePath& parentPath, int macroIndex);

    /**
     * @brief Exit macro link mode
     */
    void exitMacroLinkMode();

    /**
     * @brief Toggle macro link mode (enter if not active, exit if active)
     */
    void toggleMacroLinkMode(const ChainNodePath& parentPath, int macroIndex);

    /**
     * @brief Get the current macro in link mode
     * @return Valid MacroSelection only if in macro link mode
     */
    const MacroSelection& getMacroInLinkMode() const {
        return macroSelection_;
    }

    /**
     * @brief Check if a specific macro is in link mode
     */
    bool isMacroInLinkMode(const ChainNodePath& parentPath, int macroIndex) const;

    // ========================================================================
    // Exit All
    // ========================================================================

    /**
     * @brief Exit all link modes (useful for ESC key handler)
     */
    void exitAllLinkModes();

    // ========================================================================
    // Listeners
    // ========================================================================

    void addListener(LinkModeManagerListener* listener);
    void removeListener(LinkModeManagerListener* listener);

  private:
    LinkModeManager();
    ~LinkModeManager() = default;

    LinkModeType linkModeType_ = LinkModeType::None;
    ModSelection modSelection_;
    MacroSelection macroSelection_;

    std::vector<LinkModeManagerListener*> listeners_;

    void notifyModLinkModeChanged(bool active, const ModSelection& selection);
    void notifyMacroLinkModeChanged(bool active, const MacroSelection& selection);
};

}  // namespace magda
