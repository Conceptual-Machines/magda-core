// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

#include "magda/sdk/sequencer/TimeBend.hpp"
#include "ui/components/common/LinkableTextSlider.hpp"
#include "ui/components/common/SvgButton.hpp"

namespace magda::daw::ui {

/**
 * @brief The time bend editor: the read-head curve, its shape menu, span, AMOUNT and CYCLES
 * (#3044).
 *
 * The curve is a JUCE shell over sdk::CurveEditor; over it go the pattern as written, the curve
 * heard when AMOUNT is below 1, a line per pattern step, and the playhead. DYNAMICS swaps the curve
 * for a bar per step of the figure (#3225). The editor holds no model: the host loads a bend and
 * persists what comes out.
 */
class TimeBendEditor : public juce::Component {
  public:
    TimeBendEditor();
    ~TimeBendEditor() override;

    /** @brief Shows @p bend; a curve with the same point count updates in place. */
    void setBend(const sdk::sequencer::TimeBend& bend);
    const sdk::sequencer::TimeBend& getBend() const {
        return bend_;
    }

    /** @brief The cycle and a bar in beats, for the grid's beat and bar lines. */
    void setCycle(double cycleBeats, double beatsPerBar);

    /** @brief A device's step in beats: the grid's division until the GRID menu picks one, and the
     * dynamics' step. */
    void setDefaultDivision(double beats);

    /** @brief Where the playhead is in the cycle, 0 to 1; negative hides it. */
    void setPlayheadPhase(float phase);

    /** @brief Hides the AMOUNT and CYCLES sliders, for a host that shows them as parameters. */
    void setShowsAmountAndCycles(bool shows);

    /** @brief Shows a GROOVE menu that loads a groove's curve, for a host with no menu of its own.
     */
    void setShowsGrooves(bool shows);

    /// Places @p accessory at the end of the header, for a host's own button such as expand.
    void setHeaderAccessory(juce::Component* accessory);

    /// The groove library by family under @p target, "None" first; @p onPick gets the name.
    static void showGrooveMenu(juce::Component& target,
                               std::function<void(const juce::String& name)> onPick);

    /// The curve as a gesture moves it; nothing is committed yet.
    std::function<void(const sdk::sequencer::TimeBend&)> onPreview;
    /// One edit, done: the bend before it and after.
    std::function<void(const sdk::sequencer::TimeBend& before,
                       const sdk::sequencer::TimeBend& after)>
        onCommit;

    LinkableTextSlider& amountSlider() {
        return amount_;
    }
    LinkableTextSlider& cyclesSlider() {
        return cycles_;
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

  private:
    class Canvas;
    class DynamicsView;

    void commit(const sdk::sequencer::TimeBend& after);
    /// Two header rows when the editor is too narrow for one.
    bool headerWraps() const;
    /// TIME and DYN on the second row, shortened, when even the curve's controls barely fit.
    bool modeInSecondRow() const;
    void showShapeMenu();
    void showGridMenu();
    void applyGrid();
    void syncControls();
    void showDynamics(bool shows);
    void updateVisibility();

    sdk::sequencer::TimeBend bend_;
    bool showsAmountAndCycles_ = true;
    bool showsGrooves_ = false;
    bool showsDynamics_ = false;
    juce::Component* accessory_ = nullptr;

    magda::SvgButton powerButton_;
    juce::TextButton timeButton_{"TIME"};
    juce::TextButton dynamicsButton_{"DYNAMICS"};
    juce::TextButton shapeButton_{"SHAPE"};
    std::array<juce::TextButton, 3> spanButtons_;
    juce::Label spanLabel_;
    juce::TextButton gridButton_{"1/16"};
    juce::TextButton snapButton_{"SNAP"};
    juce::TextButton grooveButton_{"GROOVE"};
    bool snap_ = true;
    double cycleBeats_ = 4.0;
    double beatsPerBar_ = 4.0;
    /// Sixteenths, the grid most parts are written on, until a host or the menu says.
    double division_ = 0.25;
    bool divisionPicked_ = false;
    /// The dynamics' step: a device's, else sixteenths, as a clip plays them.
    double dynamicsStep_ = 0.25;
    juce::Label amountLabel_;
    LinkableTextSlider amount_;
    juce::Label cyclesLabel_;
    LinkableTextSlider cycles_;
    juce::Label dynamicsAmountLabel_;
    LinkableTextSlider dynamicsAmount_;
    std::unique_ptr<Canvas> canvas_;
    std::unique_ptr<DynamicsView> dynamicsView_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimeBendEditor)
};

}  // namespace magda::daw::ui
