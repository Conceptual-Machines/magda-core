#include "compiled/CompiledUtilityView.hpp"

#include "audio/plugins/compiled/MagdaUtilityCompiledPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kPollMs = 100;

float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

void styleSlider(juce::Slider& s) {
    s.setColour(juce::Slider::trackColourId, DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    s.setColour(juce::Slider::backgroundColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
    s.setColour(juce::Slider::rotarySliderFillColourId,
                DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    s.setColour(juce::Slider::rotarySliderOutlineColourId,
                DarkTheme::getColour(DarkTheme::SURFACE));
}

}  // namespace

juce::Font CompiledUtilityView::ButtonLaf::getTextButtonFont(juce::TextButton&, int) {
    return FontManager::getInstance().getUIFont(10.0f);
}

CompiledUtilityView::CompiledUtilityView(juce::String /*pluginId*/) {
    using Util = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;

    gainSlider_.setSliderStyle(juce::Slider::LinearVertical);
    gainSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    gainSlider_.setRange(-60.0, 12.0, 0.1);
    gainSlider_.setValue(0.0, juce::dontSendNotification);
    gainSlider_.setDoubleClickReturnValue(true, 0.0);
    styleSlider(gainSlider_);
    gainSlider_.onValueChange = [this]() {
        if (onParameterChanged)
            onParameterChanged(Util::kGainSlot, static_cast<float>(gainSlider_.getValue()));
        repaint();
    };
    addAndMakeVisible(gainSlider_);

    panSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(0.0, juce::dontSendNotification);
    panSlider_.setDoubleClickReturnValue(true, 0.0);
    styleSlider(panSlider_);
    panSlider_.onValueChange = [this]() {
        if (onParameterChanged)
            onParameterChanged(Util::kPanSlot, static_cast<float>(panSlider_.getValue()));
        repaint();
    };
    addAndMakeVisible(panSlider_);

    widthSlider_.setSliderStyle(juce::Slider::Rotary);
    widthSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    widthSlider_.setRange(0.0, 2.0, 0.01);
    widthSlider_.setValue(1.0, juce::dontSendNotification);
    widthSlider_.setDoubleClickReturnValue(true, 1.0);
    styleSlider(widthSlider_);
    widthSlider_.onValueChange = [this]() {
        if (onParameterChanged)
            onParameterChanged(Util::kWidthSlot, static_cast<float>(widthSlider_.getValue()));
        repaint();
    };
    addAndMakeVisible(widthSlider_);

    lowXoverSlider_.setSliderStyle(juce::Slider::Rotary);
    lowXoverSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    lowXoverSlider_.setRange(20.0, 500.0, 1.0);
    lowXoverSlider_.setSkewFactorFromMidPoint(80.0);
    lowXoverSlider_.setValue(120.0, juce::dontSendNotification);
    lowXoverSlider_.setDoubleClickReturnValue(true, 120.0);
    styleSlider(lowXoverSlider_);
    lowXoverSlider_.onValueChange = [this]() {
        if (onParameterChanged)
            onParameterChanged(Util::kLowMonoFreqSlot,
                               static_cast<float>(lowXoverSlider_.getValue()));
        repaint();
    };
    addAndMakeVisible(lowXoverSlider_);

    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);
    const auto accentBg = accent.withAlpha(0.25f);
    const auto inactive = DarkTheme::getSecondaryTextColour();
    const auto bg = DarkTheme::getColour(DarkTheme::BACKGROUND);

    for (int i = 0; i < 4; ++i) {
        auto& btn = btns_[static_cast<size_t>(i)];
        btn.setLookAndFeel(&buttonLaf_);
        btn.setButtonText(kLabels[static_cast<size_t>(i)]);
        btn.setClickingTogglesState(true);
        btn.setColour(juce::TextButton::buttonColourId, bg);
        btn.setColour(juce::TextButton::buttonOnColourId, accentBg);
        btn.setColour(juce::TextButton::textColourOffId, inactive);
        btn.setColour(juce::TextButton::textColourOnId, accent);
        const int slot = Util::kMonoSlot + i;
        btn.onClick = [this, slot, i]() {
            const bool on = btns_[static_cast<size_t>(i)].getToggleState();
            if (onParameterChanged)
                onParameterChanged(slot, on ? 1.0f : 0.0f);
        };
        addAndMakeVisible(btn);
    }

    startTimer(kPollMs);
}

CompiledUtilityView::~CompiledUtilityView() {
    for (auto& btn : btns_)
        btn.setLookAndFeel(nullptr);
}

void CompiledUtilityView::setCompiledPlugin(
    magda::daw::audio::compiled::MagdaUtilityCompiledPlugin* plugin) {
    compiledPlugin_ = plugin;
}

void CompiledUtilityView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    syncFromDevice();
}

void CompiledUtilityView::syncFromDevice() {
    using Util = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;
    gainSlider_.setValue(valueForSlot(deviceSnapshot_, Util::kGainSlot, 0.0f),
                         juce::dontSendNotification);
    panSlider_.setValue(valueForSlot(deviceSnapshot_, Util::kPanSlot, 0.0f),
                        juce::dontSendNotification);
    widthSlider_.setValue(valueForSlot(deviceSnapshot_, Util::kWidthSlot, 1.0f),
                          juce::dontSendNotification);
    lowXoverSlider_.setValue(valueForSlot(deviceSnapshot_, Util::kLowMonoFreqSlot, 120.0f),
                             juce::dontSendNotification);
    for (int i = 0; i < 4; ++i) {
        const bool on = valueForSlot(deviceSnapshot_, Util::kMonoSlot + i, 0.0f) >= 0.5f;
        btns_[static_cast<size_t>(i)].setToggleState(on, juce::dontSendNotification);
    }
    repaint();
}

void CompiledUtilityView::timerCallback() {
    using Util = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;
    if (compiledPlugin_ == nullptr)
        return;

    auto readFloat = [this](int slot, float fallback) -> float {
        if (auto* p = compiledPlugin_->getSlotParameter(slot))
            return compiledPlugin_->nativeValueToDisplayValue(slot, p->getCurrentValue());
        return fallback;
    };

    gainSlider_.setValue(readFloat(Util::kGainSlot, 0.0f), juce::dontSendNotification);
    panSlider_.setValue(readFloat(Util::kPanSlot, 0.0f), juce::dontSendNotification);
    widthSlider_.setValue(readFloat(Util::kWidthSlot, 1.0f), juce::dontSendNotification);
    lowXoverSlider_.setValue(readFloat(Util::kLowMonoFreqSlot, 120.0f), juce::dontSendNotification);
    for (int i = 0; i < 4; ++i) {
        const bool on = readFloat(Util::kMonoSlot + i, 0.0f) >= 0.5f;
        auto& btn = btns_[static_cast<size_t>(i)];
        if (btn.getToggleState() != on)
            btn.setToggleState(on, juce::dontSendNotification);
    }
    repaint();
}

void CompiledUtilityView::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

    const auto labelCol = DarkTheme::getSecondaryTextColour();
    const auto font = FontManager::getInstance().getUIFont(9.0f);
    g.setFont(font);
    g.setColour(labelCol);

    // GAIN label above slider, value below
    {
        auto b = gainSlider_.getBounds();
        g.drawText("GAIN", 0, b.getY() - 13, getWidth(), 12, juce::Justification::centred);
        const juce::String val = juce::String(gainSlider_.getValue(), 1) + " dB";
        g.drawText(val, 0, b.getBottom() + 1, getWidth(), 12, juce::Justification::centred);
    }

    // PAN label and value above pan slider
    {
        auto b = panSlider_.getBounds();
        const int labelY = b.getY() - 13;
        g.drawText("PAN", b.getX(), labelY, b.getWidth(), 12, juce::Justification::centredLeft);
        const float v = static_cast<float>(panSlider_.getValue());
        juce::String val;
        if (std::fabs(v) < 0.005f)
            val = "C";
        else if (v < 0)
            val = "L " + juce::String(-v * 100.0f, 0) + "%";
        else
            val = "R " + juce::String(v * 100.0f, 0) + "%";
        g.drawText(val, b.getX(), labelY, b.getWidth(), 12, juce::Justification::centredRight);
    }

    // WIDTH label and value above width knob
    {
        auto b = widthSlider_.getBounds();
        const int labelY = b.getY() - 13;
        g.drawText("WIDTH", b.getX(), labelY, b.getWidth(), 12, juce::Justification::centredLeft);
        g.drawText(juce::String(widthSlider_.getValue(), 2), b.getX(), labelY, b.getWidth(), 12,
                   juce::Justification::centredRight);
    }

    // XOVER label and value above low xover knob
    {
        auto b = lowXoverSlider_.getBounds();
        const int labelY = b.getY() - 13;
        g.drawText("XOVER", b.getX(), labelY, b.getWidth(), 12, juce::Justification::centredLeft);
        g.drawText(juce::String(static_cast<int>(lowXoverSlider_.getValue())) + " Hz", b.getX(),
                   labelY, b.getWidth(), 12, juce::Justification::centredRight);
    }
}

void CompiledUtilityView::resized() {
    // Layout (top to bottom), total target height = 220px
    // [5px pad]
    // [12px GAIN label]
    // [72px gain fader]
    // [12px gain value text]
    // [12px PAN label]
    // [20px pan slider]
    // [12px WIDTH/XOVER labels]
    // [42px knob row]
    // [28px button row]
    // [5px pad]
    constexpr int kPad = 5;
    constexpr int kGainLabelH = 12;
    constexpr int kGainH = 72;
    constexpr int kGainValH = 12;
    constexpr int kPanLabelH = 12;
    constexpr int kPanH = 20;
    constexpr int kKnobLabelH = 12;
    constexpr int kKnobH = 42;
    constexpr int kBtnH = 28;
    constexpr int kGainW = 20;

    auto b = getLocalBounds().reduced(kPad);

    b.removeFromTop(kGainLabelH);
    auto gainArea = b.removeFromTop(kGainH);
    gainSlider_.setBounds(gainArea.withWidth(kGainW).withX(gainArea.getCentreX() - kGainW / 2));
    b.removeFromTop(kGainValH);

    b.removeFromTop(kPanLabelH);
    panSlider_.setBounds(b.removeFromTop(kPanH));

    b.removeFromTop(kKnobLabelH);
    auto knobRow = b.removeFromTop(kKnobH);
    const int halfW = knobRow.getWidth() / 2;
    widthSlider_.setBounds(knobRow.removeFromLeft(halfW));
    lowXoverSlider_.setBounds(knobRow);

    auto btnRow = b.removeFromTop(kBtnH);
    const int btnW = btnRow.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
        btns_[static_cast<size_t>(i)].setBounds(
            btnRow.removeFromLeft(i < 3 ? btnW : btnRow.getWidth()).reduced(1, 0));
}

void CompiledUtilityView::bindPlugin(te::Plugin* plugin) {
    setCompiledPlugin(
        dynamic_cast<magda::daw::audio::compiled::MagdaUtilityCompiledPlugin*>(plugin));
}

const CompiledPresentationSpec& getMagdaUtilityPresentation() {
    static const CompiledPresentationSpec kSpec{
        .pluginId = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin::xmlTypeName,
        .layoutCellCount = 0,
        .layoutCellsPerRow = 0,
        .createPanel = [](juce::String pluginId) -> std::unique_ptr<CompiledDevicePanel> {
            return std::make_unique<CompiledUtilityView>(pluginId);
        },
        .suppressLegacyUis = {},
        .visualMinFractionNumerator = 1,
        .visualMinFractionDenominator = 1,
    };
    return kSpec;
}

}  // namespace magda::daw::ui
