#include "ParamWidgetSetup.hpp"

#include "ui/themes/DarkTheme.hpp"

namespace magda::daw::ui {

void configureSliderFormatting(TextSlider& slider, const magda::ParameterInfo& info) {
    if (info.scale == magda::ParameterScale::Logarithmic && info.unit == "Hz") {
        // Frequency — show as Hz / kHz
        slider.setValueFormatter([info](double normalized) {
            float hz = info.minValue *
                       std::pow(info.maxValue / info.minValue, static_cast<float>(normalized));
            if (hz >= 1000.0f) {
                return juce::String(hz / 1000.0f, 2) + " kHz";
            }
            return juce::String(static_cast<int>(hz)) + " Hz";
        });
        slider.setValueParser([info](const juce::String& text) {
            auto trimmed = text.trim();
            float hz = 0.0f;
            if (trimmed.endsWithIgnoreCase("khz")) {
                hz = trimmed.dropLastCharacters(3).trim().getFloatValue() * 1000.0f;
            } else if (trimmed.endsWithIgnoreCase("hz")) {
                hz = trimmed.dropLastCharacters(2).trim().getFloatValue();
            } else {
                hz = trimmed.getFloatValue();
            }
            hz = juce::jlimit(info.minValue, info.maxValue, hz);
            return std::log(hz / info.minValue) / std::log(info.maxValue / info.minValue);
        });
    } else if (info.unit == "dB") {
        slider.setValueFormatter([info](double normalized) {
            float db =
                info.minValue + static_cast<float>(normalized) * (info.maxValue - info.minValue);
            if (db <= -60.0f) {
                return juce::String("-inf");
            }
            return juce::String(db, 1) + " dB";
        });
        slider.setValueParser([info](const juce::String& text) {
            auto trimmed = text.trim();
            if (trimmed.endsWithIgnoreCase("db")) {
                trimmed = trimmed.dropLastCharacters(2).trim();
            }
            float db = trimmed.getFloatValue();
            db = juce::jlimit(info.minValue, info.maxValue, db);
            return (db - info.minValue) / (info.maxValue - info.minValue);
        });
    } else if (info.unit == "%" ||
               (info.unit.isEmpty() && info.minValue == 0.0f && info.maxValue == 1.0f)) {
        // Percentage (explicit or generic 0–1 linear)
        slider.setValueFormatter([](double normalized) {
            return juce::String(static_cast<int>(normalized * 100)) + "%";
        });
        slider.setValueParser([](const juce::String& text) {
            auto trimmed = text.trim();
            if (trimmed.endsWith("%")) {
                trimmed = trimmed.dropLastCharacters(1).trim();
            }
            return juce::jlimit(0.0, 1.0, trimmed.getDoubleValue() / 100.0);
        });
    } else {
        // Default — raw normalized value
        slider.setValueFormatter(nullptr);
        slider.setValueParser(nullptr);
    }
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
    toggle.setButtonText(info.currentValue >= 0.5 ? "On" : "Off");
}

void configureDiscreteCombo(juce::ComboBox& combo, const magda::ParameterInfo& info,
                            std::function<void(double)> onValueChanged) {
    combo.setColour(juce::ComboBox::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    combo.setColour(juce::ComboBox::textColourId, juce::Colours::white);
    combo.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));

    int numChoices = static_cast<int>(info.choices.size());
    combo.onChange = [&combo, numChoices, cb = std::move(onValueChanged)]() {
        if (cb) {
            int selected = combo.getSelectedItemIndex();
            double normalized =
                numChoices > 1 ? static_cast<double>(selected) / (numChoices - 1) : 0.0;
            cb(normalized);
        }
    };

    combo.clear();
    int id = 1;
    for (const auto& choice : info.choices) {
        combo.addItem(choice, id++);
    }

    int selectedIndex =
        static_cast<int>(std::round(info.currentValue * (numChoices > 1 ? numChoices - 1 : 0)));
    combo.setSelectedItemIndex(juce::jlimit(0, numChoices - 1, selectedIndex),
                               juce::dontSendNotification);
}

}  // namespace magda::daw::ui
