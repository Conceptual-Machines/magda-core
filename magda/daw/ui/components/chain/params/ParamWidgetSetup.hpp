#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

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

/**
 * @brief Create/configure a combo box for a discrete parameter with named choices.
 *
 * Populates the combo with the choices in @p info and sets the current
 * selection. The callback is wired to fire @p onValueChanged with a
 * normalized 0–1 value.
 */
void configureDiscreteCombo(juce::ComboBox& combo, const magda::ParameterInfo& info,
                            std::function<void(double)> onValueChanged);

}  // namespace magda::daw::ui
