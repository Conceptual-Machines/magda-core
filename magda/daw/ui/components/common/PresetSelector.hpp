// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

#include "SvgButton.hpp"

namespace magda::daw::ui {

/**
 * @brief A box naming the current preset, beside a floppy disk (#3234).
 *
 * The box opens the owner's preset menu and the floppy disk asks the owner to save.
 * The owner styles the floppy disk to match the header it sits in.
 */
class PresetSelector : public juce::Component {
  public:
    PresetSelector(int boxHeight, int saveWidth);
    ~PresetSelector() override;

    std::function<void()> onOpenMenu;
    std::function<void()> onSave;

    /** @brief Name the preset by its path, shown without folders; empty shows "Presets". */
    void setPresetName(const juce::String& presetName);

    /** @brief What the box currently reads. */
    juce::String getLabel() const;

    /** @brief Keep the box lit while its menu is open. */
    void setMenuOpen(bool open);

    /** @brief The box, which the owner opens its menu against. */
    juce::Button& getBox();

    magda::SvgButton& getSaveButton();

    /** @brief Stands @p button after the floppy disk, at its size. The caller owns it. */
    void setTrailingButton(juce::Component* button);

    int getPreferredWidth() const;

    void resized() override;

  private:
    class Box;

    std::unique_ptr<Box> box_;
    magda::SvgButton save_;
    juce::Component* trailing_ = nullptr;
    int boxHeight_;
    int saveWidth_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetSelector)
};

}  // namespace magda::daw::ui
