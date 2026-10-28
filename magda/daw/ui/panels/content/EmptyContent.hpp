// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "PanelContent.hpp"

namespace magda::daw::ui {

/**
 * @brief Empty content shown when no selection is active
 *
 * Displays a subtle message or just an empty panel background.
 */
class EmptyContent : public PanelContent {
  public:
    EmptyContent();
    ~EmptyContent() override = default;

    PanelContentType getContentType() const override {
        return PanelContentType::Empty;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::Empty, "", "No selection", ""};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EmptyContent)
};

}  // namespace magda::daw::ui
