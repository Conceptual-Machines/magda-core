#include "custom_ui/DrumVoiceUI.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kSectionTitleH = 15;
constexpr int kCellLabelH = 12;
constexpr int kCellPad = 4;
constexpr int kCellW = 76;  // per-knob column width (preferred sizing)

struct DrumVoice {
    const char* pluginId;
    const char* title;
};
constexpr DrumVoice kDrumVoices[] = {
    {"magda_kick", "Kick"}, {"magda_snare", "Snare"}, {"magda_clap", "Clap"},
    {"magda_hat", "Hat"},   {"magda_tom", "Tom"},
};
}  // namespace

bool DrumVoiceUI::handles(const juce::String& pluginId) {
    return !titleFor(pluginId).isEmpty();
}

juce::String DrumVoiceUI::titleFor(const juce::String& pluginId) {
    for (const auto& v : kDrumVoices)
        if (pluginId.equalsIgnoreCase(v.pluginId))
            return v.title;
    return {};
}

DrumVoiceUI::DrumVoiceUI(juce::String title) : title_(std::move(title)) {}
DrumVoiceUI::~DrumVoiceUI() = default;

void DrumVoiceUI::ensureControls(int count) {
    while (static_cast<int>(controls_.size()) < count) {
        const int idx = static_cast<int>(controls_.size());
        Control c;

        c.label = std::make_unique<juce::Label>();
        c.label->setFont(FontManager::getInstance().getUIFont(10.0f));
        c.label->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
        c.label->setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(*c.label);

        c.slider = std::make_unique<LinkableTextSlider>();
        c.slider->setParamIndex(idx);
        c.slider->onValueChanged = [this, idx](double v) {
            if (onParameterChanged)
                onParameterChanged(idx, static_cast<float>(v));
        };
        addAndMakeVisible(*c.slider);

        controls_.push_back(std::move(c));
    }
}

void DrumVoiceUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    ensureControls(static_cast<int>(params.size()));

    for (const auto& info : params) {
        if (info.paramIndex < 0 || info.paramIndex >= static_cast<int>(controls_.size()))
            continue;
        auto& c = controls_[static_cast<size_t>(info.paramIndex)];
        c.label->setText(info.name, juce::dontSendNotification);
        c.slider->setParameterInfo(info);
        c.slider->setValue(info.currentValue, juce::dontSendNotification);
    }
    resized();
}

std::vector<LinkableTextSlider*> DrumVoiceUI::getLinkableSliders() {
    std::vector<LinkableTextSlider*> sliders;
    sliders.reserve(controls_.size());
    for (auto& c : controls_)
        sliders.push_back(c.slider.get());
    return sliders;
}

int DrumVoiceUI::preferredContentWidth() const {
    const int n = juce::jmax(1, static_cast<int>(controls_.size()));
    return n * kCellW + 2 * kCellPad;
}

void DrumVoiceUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds());

    auto titleArea = getLocalBounds().removeFromTop(kSectionTitleH).reduced(kCellPad, 0);
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.setFont(FontManager::getInstance().getUIFont(11.0f));
    g.drawText(title_, titleArea, juce::Justification::centredLeft, false);
}

void DrumVoiceUI::resized() {
    auto area = getLocalBounds().reduced(kCellPad);
    area.removeFromTop(kSectionTitleH);
    if (controls_.empty())
        return;

    const int cellW = area.getWidth() / static_cast<int>(controls_.size());
    for (auto& c : controls_) {
        auto cell = area.removeFromLeft(cellW).reduced(2, 0);
        c.label->setBounds(cell.removeFromTop(kCellLabelH));
        c.slider->setBounds(cell.removeFromTop(juce::jmin(22, cell.getHeight())));
    }
}

}  // namespace magda::daw::ui
