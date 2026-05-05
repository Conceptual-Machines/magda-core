#include "ParamWidgetSetup.hpp"

#include <cmath>
#include <limits>

#include "core/ParameterUtils.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

void configureSliderFormatting(TextSlider& slider, const magda::ParameterInfo& info) {
    // Live plugin display text — exact values, no quantization.
    //
    // DisplayTextProvider::format is a thin wrapper around TE's
    // valueToString, so the argument MUST be a plugin-native (TE raw)
    // value. The generic slot slider operates in MAGDA-normalized 0..1
    // space; project back to the TE range, honouring any scaleAnchor /
    // log skew when info.min/max match the TE range (internal plugins
    // and VSTs without AI-Detect). For external VSTs with an AI-Detect
    // display range the info differs from TE, in which case
    // normalizedToReal would return a display-range value — fall back
    // to a linear projection onto TE there so the provider still sees
    // the native value. This mirrors
    // AutomationPlaybackEngine::convertMagdaNormalizedToTeRaw and
    // DeviceSlotComponent::automationValueChanged.
    if (info.displayText) {
        auto provider = info.displayText;
        const magda::ParameterInfo infoCopy = info;
        const float teMin = info.teMinValue;
        const float teSpan = info.teMaxValue - info.teMinValue;
        const bool infoMatchesTeRange = std::abs(info.minValue - info.teMinValue) < 1e-6f &&
                                        std::abs(info.maxValue - info.teMaxValue) < 1e-6f;
        auto projectToTe = [provider, infoCopy, teMin, teSpan,
                            infoMatchesTeRange](double normalized) -> float {
            if (infoMatchesTeRange && infoCopy.maxValue > infoCopy.minValue) {
                return magda::ParameterUtils::normalizedToReal(static_cast<float>(normalized),
                                                               infoCopy);
            }
            return teMin + static_cast<float>(normalized) * teSpan;
        };
        slider.setValueFormatter([provider, projectToTe](double normalized) {
            return provider->format(projectToTe(normalized));
        });
        // Reverse-lookup parser: strip unit suffix, parse number, find closest
        // normalized value by querying the plugin at sample points.
        slider.setValueParser([provider, projectToTe](const juce::String& text) -> double {
            auto stripped = text.trim().retainCharacters("0123456789.-+eE");
            double target = stripped.getDoubleValue();
            int bestIdx = 0;
            double bestDist = std::numeric_limits<double>::max();
            constexpr int kSteps = 128;
            for (int i = 0; i <= kSteps; ++i) {
                float norm = static_cast<float>(i) / kSteps;
                float teRaw = projectToTe(static_cast<double>(norm));
                auto numPart = provider->format(teRaw).trim().retainCharacters("0123456789.-+eE");
                if (numPart.isEmpty())
                    continue;
                double dist = std::abs(numPart.getDoubleValue() - target);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestIdx = i;
                }
            }
            return static_cast<double>(bestIdx) / kSteps;
        });
        return;
    }

    // If we have a full value table from the plugin, use it directly
    if (!info.valueTable.empty()) {
        slider.setValueFormatter([vt = info.valueTable](double normalized) {
            int idx = juce::jlimit(0, static_cast<int>(vt.size()) - 1,
                                   static_cast<int>(std::round(normalized * (vt.size() - 1))));
            return vt[static_cast<size_t>(idx)].trim();
        });
        // Reverse-lookup parser: strip any unit suffix, parse the number,
        // then find the closest value table entry by numeric distance.
        slider.setValueParser([vt = info.valueTable](const juce::String& text) -> double {
            auto stripped = text.trim().retainCharacters("0123456789.-+eE");
            double target = stripped.getDoubleValue();
            int bestIdx = 0;
            double bestDist = std::numeric_limits<double>::max();
            for (int i = 0; i < static_cast<int>(vt.size()); ++i) {
                auto numPart =
                    vt[static_cast<size_t>(i)].trim().retainCharacters("0123456789.-+eE");
                if (numPart.isEmpty())
                    continue;
                double dist = std::abs(numPart.getDoubleValue() - target);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestIdx = i;
                }
            }
            return static_cast<double>(bestIdx) / juce::jmax(1, static_cast<int>(vt.size()) - 1);
        });
        return;
    }

    slider.setParameterInfo(info);
}

void configureBoolToggle(juce::ToggleButton& toggle, const magda::ParameterInfo& info,
                         std::function<void(double)> onValueChanged) {
    toggle.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    toggle.setColour(juce::ToggleButton::tickColourId,
                     DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    toggle.onClick = [&toggle, cb = std::move(onValueChanged)]() {
        if (cb) {
            cb(toggle.getToggleState() ? 1.0 : 0.0);
        }
    };
    toggle.setToggleState(info.currentValue >= 0.5, juce::dontSendNotification);
    toggle.setButtonText("");
}

void configureDiscreteCombo(juce::ComboBox& combo, const magda::ParameterInfo& info,
                            std::function<void(double)> onValueChanged) {
    combo.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    combo.setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    combo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    combo.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    combo.setJustificationType(juce::Justification::centred);

    int numChoices = static_cast<int>(info.choices.size());
    combo.onChange = [&combo, cb = std::move(onValueChanged)]() {
        if (cb) {
            int selected = combo.getSelectedItemIndex();
            cb(static_cast<double>(selected));
        }
    };

    combo.clear();
    int id = 1;
    for (const auto& choice : info.choices) {
        combo.addItem(choice, id++);
    }

    int selectedIndex = static_cast<int>(std::round(info.currentValue));
    combo.setSelectedItemIndex(juce::jlimit(0, numChoices - 1, selectedIndex),
                               juce::dontSendNotification);
}

}  // namespace magda::daw::ui
