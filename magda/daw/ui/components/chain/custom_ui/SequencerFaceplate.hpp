// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/TimeBendEditor.hpp"
#include "ui/themes/ActiveTheme.hpp"

namespace magda::daw::ui::sequencer_faceplate {

/// Room for the row names left of the step columns.
constexpr int kGutterWidth = 58;
constexpr int kRulerHeight = 16;
constexpr int kToggleRowHeight = 20;
constexpr int kRowGap = 5;
/// The step rows at their usual size: the gutter and sixteen 32px columns.
constexpr int kStepsWidth = kGutterWidth + 16 * 32;
/// Around the faceplate, and between the steps and the controls.
constexpr int kInsetX = 10;
constexpr int kInsetY = 8;
constexpr int kStepsGap = 16;

/// Piano keys keep their colours in every theme.
inline const juce::Colour kWhiteKey{0xFFDDE1E5};
inline const juce::Colour kBlackKey{0xFF15181C};

juce::String noteName(int noteNumber);

/// The step under @p x across @p area's columns, or -1.
int stepAt(int x, juce::Rectangle<int> area, int numSteps);

/// One step's cell in @p area: the column less the gap between cells.
juce::Rectangle<float> cellBounds(juce::Rectangle<int> area, int step, int numSteps);

/// A value box inside a painted tile: no frame or fill of its own.
void styleTileValue(LinkableTextSlider& slider);

/// A row's name, right-aligned in the gutter.
void drawRowLabel(juce::Graphics& g, juce::Rectangle<int> gutter, const juce::String& text);

/// Step numbers every four steps over @p columns, the playing step's lit.
void drawRuler(juce::Graphics& g, juce::Rectangle<int> columns, int numSteps, int playStep);

/// A toggle per step: a lit bar in @p role where @p isOn, an empty field elsewhere.
void drawToggleRow(juce::Graphics& g, juce::Rectangle<int> columns, int numSteps,
                   magda::ColourRole role, const std::function<bool(int)>& isOn);

/// A 0-1 value per step as a bar from the floor with a lit top edge.
void drawBarRow(juce::Graphics& g, juce::Rectangle<int> columns, int numSteps,
                magda::ColourRole role, const std::function<float(int)>& valueOf);

/**
 * @brief The sequencers' controls right of the steps: their slots and settings as tiles, and
 * beside them the time bend, which can be hidden.
 *
 * Holds no model. The owning faceplate wires each control's callback to its own slot indices and
 * settings, and reads the values back in.
 */
class SequencerControlPanel : public juce::Component {
  public:
    static constexpr int kTilesWidth = 212;
    /// The bend editor's header row needs this much.
    static constexpr int kBendWidth = 292;
    static constexpr int kColumnGap = 16;

    SequencerControlPanel();
    ~SequencerControlPanel() override;

    /// Tiles alone, or tiles and the time bend.
    int preferredWidth() const;
    void setShowsTimeBend(bool shows);
    bool showsTimeBend() const {
        return showsTimeBend_;
    }

    LinkableTextSlider rate, steps, swing, gate, quantize, subdivision, bendAmount, bendCycles;
    juce::ComboBox direction;
    TimeBendEditor timeBend;

    /// A tile beside Direction for a control only one sequencer has, as wide as @p span tiles.
    void addExtraTile(const juce::String& caption, juce::Component& control, int span = 1);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

  private:
    struct Tile {
        juce::String caption;
        juce::Component* control = nullptr;
        int span = 1;
        juce::Rectangle<int> bounds;
    };

    std::vector<Tile> tiles_;
    std::vector<Tile> bendTiles_;
    juce::Rectangle<int> bendHeader_;
    bool showsTimeBend_ = true;
    int dividerX_ = 0;

    void addTile(const juce::String& caption, juce::Component& control, int span = 1);
    void styleControls();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerControlPanel)
};

}  // namespace magda::daw::ui::sequencer_faceplate
