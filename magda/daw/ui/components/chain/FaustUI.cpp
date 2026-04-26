#include "FaustUI.hpp"

#include <tracktion_engine/tracktion_engine.h>

#include "audio/FaustPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace te = tracktion::engine;

FaustUI::FaustUI() {
    startTimerHz(30);
}

FaustUI::~FaustUI() {
    stopTimer();
}

void FaustUI::setPlugin(magda::daw::audio::FaustPlugin* plugin) {
    plugin_ = plugin;
    rebuildFromPlugin();
}

void FaustUI::rebuildFromPlugin() {
    slots_.clear();
    if (!plugin_)
        return;

    for (auto p : plugin_->getAutomatableParameters()) {
        auto slot = std::make_unique<ParamSlot>();
        slot->param = p;

        slot->label.setText(p->getParameterName(), juce::dontSendNotification);
        slot->label.setFont(FontManager::getInstance().getUIFont(9.0f));
        slot->label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        slot->label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(slot->label);

        const auto& range = p->valueRange;
        slot->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slot->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
        slot->slider.setRange(range.start, range.end,
                              range.interval > 0.0f ? range.interval : 0.0001);
        slot->slider.setValue(p->getCurrentValue(), juce::dontSendNotification);

        auto* paramPtr = p;
        slot->slider.onValueChange = [paramPtr, s = slot.get()]() {
            paramPtr->setParameter(static_cast<float>(s->slider.getValue()),
                                   juce::sendNotificationSync);
        };
        addAndMakeVisible(slot->slider);

        slots_.push_back(std::move(slot));
    }

    resized();
}

void FaustUI::timerCallback() {
    // Pull external changes (automation playback, project load) back into the
    // sliders. dontSendNotification avoids re-firing the onValueChange writer.
    for (auto& s : slots_) {
        if (!s->param || s->slider.isMouseButtonDown())
            continue;
        const float v = s->param->getCurrentValue();
        if (std::abs(v - static_cast<float>(s->slider.getValue())) > 1e-6f)
            s->slider.setValue(v, juce::dontSendNotification);
    }
}

void FaustUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void FaustUI::resized() {
    if (slots_.empty())
        return;
    auto area = getLocalBounds().reduced(6);
    const int labelHeight = 14;
    const int sliderHeight = 18;
    const int n = static_cast<int>(slots_.size());
    const int colWidth = area.getWidth() / n;

    for (int i = 0; i < n; ++i) {
        auto col = area.removeFromLeft(colWidth).reduced(2, 0);
        slots_[i]->label.setBounds(col.removeFromTop(labelHeight));
        slots_[i]->slider.setBounds(col.removeFromTop(sliderHeight + 16));
    }
}

}  // namespace magda::daw::ui
