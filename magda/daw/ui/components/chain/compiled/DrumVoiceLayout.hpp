// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_graphics/juce_graphics.h>

#include <vector>

#include "layout/DeviceParamLayout.hpp"
#include "params/ParamControlStyle.hpp"

namespace magda::daw::ui {

/// The drum-machine voices that share this faceplate.
inline constexpr const char* kDrumVoicePluginIds[] = {"magda_kick", "magda_snare", "magda_clap",
                                                      "magda_hat", "magda_tom"};

/**
 * @brief One layer of a drum voice (Transient, Body, Rattle...) and the slots that shape it.
 *
 * Slot indices match the [idx:N] pins in the voice's .dsp. A layer with a decay slot draws its
 * envelope over the rendered hit; the attack and curve slots shape that envelope.
 */
struct DrumVoiceSection {
    const char* title = "";
    std::vector<int> slots;
    int attackSlot = -1;
    int decaySlot = -1;
    int curveSlot = -1;
    int columns = 2;
};

/// The layers of @p pluginId.
std::vector<DrumVoiceSection> drumVoiceSections(const juce::String& pluginId);

/**
 * @brief The voice's body: the layers as tabs over the selected one's cells, sized for the
 *        control style, and the rendered hit beside them in what width is left (#3201).
 */
struct DrumVoiceGeometry {
    juce::Rectangle<int> plot;                 ///< Empty when there is no width beside the cells.
    std::vector<juce::Rectangle<int>> titles;  ///< One tab per section, in a row.
    /// One per slot, sections in order, each section's in the same area under the tabs; a curve
    /// slot's is the chip in its decay cell.
    std::vector<juce::Rectangle<int>> cells;

    static constexpr int kSpace = 8;
    static constexpr int kTitleHeight = 22;
    static constexpr int kCellWidth = 88;
    /// The curve's chip, nested at the top right of its decay cell.
    static constexpr int kChipWidth = 42;
    static constexpr int kChipHeight = 16;
    static constexpr int kCellGap = 4;
    /// The plot's width when shown: the slot widens by this for it.
    static constexpr int kPlotWidth = 240;
    static constexpr int kMinPlotWidth = 60;

    static int cellHeight(ParamControlStyle style);
    /// Width of the widest section's cells, with the space before them.
    static int columnsWidth(const std::vector<DrumVoiceSection>& sections);
    /// Without @p cellsShown the plot takes the body.
    static DrumVoiceGeometry of(juce::Rectangle<int> bounds, ParamControlStyle style,
                                const std::vector<DrumVoiceSection>& sections,
                                bool cellsShown = true);
};

/// The voice's cells, placed around the plot: each layer's slots under its title.
class DrumVoiceLayout final : public DeviceParamLayout {
  public:
    explicit DrumVoiceLayout(const juce::String& pluginId);

    int cellCount() const override;
    int cellsPerRow() const override {
        return cellCount();
    }
    bool wantsPagination() const override {
        return false;
    }
    /// A page per layer: the faceplate's tabs turn them.
    int totalPages(const magda::DeviceInfo&) const override {
        return static_cast<int>(sections_.size());
    }
    bool placesOwnCells() const override {
        return true;
    }
    juce::Rectangle<int> cellBounds(int cell, juce::Rectangle<int> area, bool) const override;
    void setControlStyle(ParamControlStyle style) override {
        style_ = style;
    }
    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex, int) const override;
    bool compactText() const override {
        return true;
    }

  private:
    std::vector<DrumVoiceSection> sections_;
    std::vector<int> slots_;
    std::vector<int> curveSlots_;
    std::vector<int> sectionOfCell_;
    ParamControlStyle style_ = ParamControlStyle::Text;
};

}  // namespace magda::daw::ui
