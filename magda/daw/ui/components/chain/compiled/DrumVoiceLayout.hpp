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
    /// Under the plot instead of a column of its own (the Output section).
    bool underPlot = false;
};

/// The layers of @p pluginId, then an Output section under the plot for the trailing Gain.
std::vector<DrumVoiceSection> drumVoiceSections(const juce::String& pluginId);

/**
 * @brief The voice's body: the rendered hit on the left, a titled column of cells per layer
 *        on the right, sized for the control style.
 */
struct DrumVoiceGeometry {
    juce::Rectangle<int> plot;
    std::vector<juce::Rectangle<int>> titles;  ///< One per section; empty under the plot.
    std::vector<juce::Rectangle<int>> cells;   ///< One per slot, sections in order.

    static constexpr int kSpace = 8;
    static constexpr int kTitleHeight = 22;
    static constexpr int kCellWidth = 64;
    static constexpr int kCellGap = 4;
    static constexpr int kMinPlotWidth = 240;

    static int cellHeight(ParamControlStyle style);
    /// Width of the cell columns, before the plot takes the rest.
    static int columnsWidth(const std::vector<DrumVoiceSection>& sections);
    static DrumVoiceGeometry of(juce::Rectangle<int> bounds, ParamControlStyle style,
                                const std::vector<DrumVoiceSection>& sections);
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
    int totalPages(const magda::DeviceInfo&) const override {
        return 1;
    }
    bool placesOwnCells() const override {
        return true;
    }
    juce::Rectangle<int> cellBounds(int cell, juce::Rectangle<int> area, bool) const override;
    void setControlStyle(ParamControlStyle style) override {
        style_ = style;
    }
    ParamCell cellFor(const magda::DeviceInfo& device, int cellIndex, int) const override;

  private:
    std::vector<DrumVoiceSection> sections_;
    std::vector<int> slots_;
    ParamControlStyle style_ = ParamControlStyle::Text;
};

}  // namespace magda::daw::ui
