#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "MomentaryParamButton.hpp"
#include "core/ParameterInfo.hpp"
#include "ui/components/common/TextSlider.hpp"

namespace magda::daw::ui {

/**
 * @brief Configure a TextSlider's value formatter and parser for a continuous
 *        parameter (frequency, dB, percentage, etc.).
 */
void configureSliderFormatting(TextSlider& slider, const magda::ParameterInfo& info);

/**
 * @brief Create/configure a toggle button for a boolean parameter.
 *
 * If the toggle does not yet exist it is created. The callback is wired to
 * fire @p onValueChanged with 1.0 or 0.0.
 */
void configureBoolToggle(juce::ToggleButton& toggle, const magda::ParameterInfo& info,
                         std::function<void(double)> onValueChanged);

void configureMomentaryButton(MomentaryParamButton& button,
                              std::function<void(double)> onValueChanged);

/**
 * @brief Create/configure a combo box for a discrete parameter with named choices.
 *
 * Populates the combo with the choices in @p info and sets the current
 * selection. The callback is wired to fire @p onValueChanged with a
 * normalized 0–1 value.
 */
void configureDiscreteCombo(juce::ComboBox& combo, const magda::ParameterInfo& info,
                            std::function<void(double)> onValueChanged);

/**
 * @brief Most choices that still get the visible-buttons treatment.
 *
 * Past this a row of segments in a grid cell is narrower than its own labels,
 * so a dropdown reads better even when the author asked for buttons.
 */
constexpr int kMaxSegmentedChoices = 4;

/**
 * @brief True iff @p info should render as a segmented button row.
 *
 * Requires the author to have asked (`radioChoices`) and the choice list to be
 * short enough to stay legible. Everything else falls back to the dropdown.
 */
bool wantsSegmentedChoices(const magda::ParameterInfo& info);

/**
 * @brief Apply theme colours to one segment of a choice row.
 *
 * Split out from configureChoiceButton so lookAndFeelChanged() can re-theme a
 * row without knowing which segment is selected.
 */
void applyChoiceButtonColours(juce::TextButton& button);

/**
 * @brief Configure one segment of a discrete parameter's button row.
 *
 * @p index / @p count drive the connected edges so the row reads as a single
 * control. Selection is set explicitly rather than via a JUCE radio group,
 * which leaves two segments lit at once. The caller owns the button and wires
 * its onClick.
 */
void configureChoiceButton(juce::TextButton& button, const juce::String& text, int index, int count,
                           bool selected);

}  // namespace magda::daw::ui
