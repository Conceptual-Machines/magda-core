// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "core/ChainNodePath.hpp"
#include "core/SelectionManager.hpp"
#include "ui/components/common/CollapsibleSectionHeader.hpp"

namespace magda::daw::ui {

class ModsPanelComponent;
class MacroPanelComponent;
class ModulatorEditorPanel;
class MacroEditorPanel;

/**
 * @brief The inspector's Mods and Macros sections: a device's, rack's or track's own
 * modulators and macros, on the chain's cards and editors.
 *
 * Edits go to TrackManager by the owner's path, so the chain's side panels show them too. The
 * host forwards TrackManager changes to refresh() and gives the section getPreferredHeight().
 */
class ModulationSection : public juce::Component {
  public:
    ModulationSection();
    ~ModulationSection() override;

    /// A device, rack or track-level path; anything without mods or macros shows nothing.
    void setOwner(const magda::ChainNodePath& owner);

    /// Re-read the owner's mods and macros.
    void refresh();
    /// Only the macro knobs' values, for high-rate writes such as a controller's.
    void refreshMacroValues();

    /// Show what a selection of @p type points at: a mod or macro opens in its editor, a
    /// panel selection unfolds its section. Scrolls the panel to it.
    void focus(magda::SelectionType type, int index);

    bool hasContent() const {
        return showsMods_ || showsMacros_;
    }
    int getPreferredHeight() const;

    /// Fires when folding or an editor changes getPreferredHeight().
    std::function<void()> onPreferredHeightChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    magda::ChainNodePath owner_;
    bool showsMods_ = false;
    bool showsMacros_ = false;
    int openModIndex_ = -1;
    int openMacroIndex_ = -1;

    CollapsibleSectionHeader modsHeader_;
    CollapsibleSectionHeader macrosHeader_;
    std::unique_ptr<ModsPanelComponent> modsPanel_;
    std::unique_ptr<ModulatorEditorPanel> modEditor_;
    std::unique_ptr<MacroPanelComponent> macroPanel_;
    std::unique_ptr<MacroEditorPanel> macroEditor_;

    // Painted chrome placed by resized().
    juce::Rectangle<int> modsBackdrop_;
    juce::Rectangle<int> macrosBackdrop_;
    int separatorY_ = -1;

    void wireModsPanel();
    void wireModEditor();
    void wireMacroPanel();
    void wireMacroEditor();

    void openMod(int modIndex);
    void openMacro(int macroIndex);
    void revealMods();
    void revealMacros();
    void scrollIntoView(const juce::Component& target);

    void showModEditor(int modIndex);
    void hideModEditor();
    void showMacroEditor(int macroIndex);
    void hideMacroEditor();
    void updateModEditor();
    void updateMacroEditor();

    void layoutChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationSection)
};

}  // namespace magda::daw::ui
