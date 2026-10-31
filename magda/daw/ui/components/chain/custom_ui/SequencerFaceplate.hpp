// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/SegmentedChoice.hpp"
#include "ui/components/common/SvgButton.hpp"
#include "ui/components/common/TimeBendEditor.hpp"
#include "ui/themes/ActiveTheme.hpp"

namespace magda::daw::ui::sequencer_faceplate {

/// Room for the row names left of the step columns.
constexpr int kGutterWidth = 58;
constexpr int kRulerHeight = 16;
constexpr int kToggleRowHeight = 18;
constexpr int kRowGap = 4;
/// The step rows at their usual size: the gutter and sixteen 32px columns.
constexpr int kStepsWidth = kGutterWidth + 16 * 32;
/// Around the faceplate, and between the steps and the controls.
constexpr int kInsetX = 10;
constexpr int kInsetY = 4;
constexpr int kStepsGap = 16;
/// A row of step boxes naming each step's notes.
constexpr int kNoteRowHeight = 28;

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

/// How a step box draws: its text, and whether it is playing, selected or a copy's target.
struct NoteCell {
    juce::String text;
    bool sounds = true;
    bool playing = false;
    bool selected = false;
    bool dropTarget = false;
};

bool isBlackKey(int noteNumber);

enum class Glyph { Up, Down, Plus, Minus };

/// Two stacked buttons in one well, as the grids' octave, scroll and zoom controls.
void drawButtonPair(juce::Graphics& g, juce::Rectangle<int> box, Glyph top, bool topEnabled,
                    Glyph bottom, bool bottomEnabled);

/// A grid's floor and lines: a heavier line every four steps, a dimmed column for a rest.
void drawGridFloor(juce::Graphics& g, juce::Rectangle<int> cells, int count, int rows,
                   const std::function<bool(int row)>& isDarkRow,
                   const std::function<bool(int step)>& isRest);

/// The playing step's left edge, a lit line down the grid.
void drawPlayhead(juce::Graphics& g, juce::Rectangle<int> cells, int count, int playStep);

/// A hit as a bar across its steps, lit by its velocity; a held tie's own notes only outlined.
void drawNoteBar(juce::Graphics& g, juce::Rectangle<float> bar, int velocity, bool sounds);

/// Expands the keyboard or grid into the arrangement, from the gutter beside the steps (#3205).
std::unique_ptr<magda::SvgButton> makeExpandButton();
/// Where it sits: the gutter left of the ruler.
juce::Rectangle<int> expandButtonBounds(juce::Rectangle<int> ruler);

/// One step box in a note row.
void drawNoteCell(juce::Graphics& g, juce::Rectangle<float> cell, const NoteCell& note);

/// @p octaves of piano keys across @p area from @p baseNote, the notes @p isLit holds in green.
void drawKeyboard(juce::Graphics& g, juce::Rectangle<float> area, int baseNote, int octaves,
                  const std::function<bool(int)>& isLit);

/// The key under @p point in a keyboard drawn by drawKeyboard(), or -1.
int keyboardNoteAt(juce::Rectangle<float> area, int baseNote, int octaves,
                   juce::Point<float> point);

/**
 * @brief A step's notes on a keyboard, opened from its box when the faceplate is too short for
 * its own (#3205).
 *
 * Reads what is lit through @p isLit on every frame, since the edits come back through the model.
 */
class NotePicker : public juce::Component, private juce::Timer {
  public:
    NotePicker(int firstNote, std::function<bool(int)> isLit, std::function<void(int)> onNote);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    /// Opens it in a call-out beside @p box, which is on @p owner.
    static void show(juce::Component& owner, juce::Rectangle<int> box,
                     std::unique_ptr<NotePicker> picker);

  private:
    static constexpr int kOctaves = 2;
    std::function<bool(int)> isLit_;
    std::function<void(int)> onNote_;
    int baseNote_;
    juce::Rectangle<int> labelArea_, downArea_, upArea_, keysArea_;

    void timerCallback() override {
        repaint();
    }
};

/**
 * @brief A step's drums as a list of chips, the Drums view's counterpart to NotePicker.
 */
class DrumPicker : public juce::Component, private juce::Timer {
  public:
    struct Drum {
        int note = 0;
        juce::String name;
    };

    DrumPicker(std::vector<Drum> drums, std::function<bool(int)> isLit,
               std::function<void(int)> onNote);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

  private:
    static constexpr int kColumns = 4;
    static constexpr int kChipW = 76;
    static constexpr int kChipH = 24;
    static constexpr int kGap = 4;
    std::vector<Drum> drums_;
    std::function<bool(int)> isLit_;
    std::function<void(int)> onNote_;

    juce::Rectangle<int> chipBounds(int index) const;
    void timerCallback() override {
        repaint();
    }
};

/**
 * @brief The sequencers' settings as one row of small value boxes above the steps (#3205).
 *
 * Holds no model. The owning faceplate wires each control to its own slots and settings, and
 * reads the values back in.
 */
class SettingsStrip : public juce::Component {
  public:
    static constexpr int kHeight = 18;

    SettingsStrip();

    LinkableTextSlider rate, steps, gate;
    magda::SegmentedChoice direction;

    /// A control only one sequencer has, after the shared ones, @p width wide.
    void addExtra(const juce::String& caption, juce::Component& control, int width);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

  private:
    struct Item {
        juce::String caption;
        juce::Component* control = nullptr;
        int width = 0;
        juce::Rectangle<int> captionBounds;
    };
    std::vector<Item> items_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsStrip)
};

/**
 * @brief A popup-menu row with a caption and a value box, for settings that need no room of
 * their own. @p setUp gives the box its range, format, value and callback.
 */
class ValueMenuItem : public juce::PopupMenu::CustomComponent {
  public:
    ValueMenuItem(const juce::String& caption,
                  const std::function<void(LinkableTextSlider&)>& setUp);

    void getIdealSize(int& width, int& height) override;
    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    juce::String caption_;
    LinkableTextSlider value_;
};

/// The time bend beside the steps, its own header carrying amount and cycles.
constexpr int kTimeBendWidth = 292;

}  // namespace magda::daw::ui::sequencer_faceplate
