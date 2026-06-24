#include "compiled/CompiledUtilityView.hpp"

#include <cmath>

#include "audio/plugins/compiled/MagdaUtilityCompiledPlugin.hpp"
#include "ui/components/mixer/LevelMeterScale.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {
float valueForSlot(const magda::DeviceInfo& device, int slotIndex, float fallback) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return param.currentValue;
    return fallback;
}

const magda::ParameterInfo* parameterForSlot(const magda::DeviceInfo& device, int slotIndex) {
    for (const auto& param : device.parameters)
        if (param.paramIndex == slotIndex)
            return &param;
    return nullptr;
}

juce::String formatGainDb(double value) {
    if (value <= -59.99)
        return "-inf";
    if (std::abs(value) < 0.05)
        return "0.0";
    return juce::String(value > 0.0 ? "+" : "") + juce::String(value, 1);
}

void styleNameLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions{9.0f}));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centred);
    label.setMinimumHorizontalScale(0.72f);
    label.setInterceptsMouseClicks(false, false);
}

void styleReadoutLabel(juce::Label& label) {
    label.setFont(juce::Font(juce::FontOptions{10.0f}));
    label.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    label.setJustificationType(juce::Justification::centred);
    label.setMinimumHorizontalScale(0.72f);
    label.setInterceptsMouseClicks(false, false);
}
}  // namespace

CompiledUtilityView::CompiledUtilityView(juce::String /*pluginId*/) {
    using Util = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;

    const auto fillColour = DarkTheme::getColour(DarkTheme::CONTROL_VALUE_FILL);

    gainFader_.setRange(-60.0, 12.0, 0.0);
    gainFader_.setValue(0.0, juce::dontSendNotification);
    gainFader_.setFillColour(fillColour);
    gainFader_.setFillProportionMapper(magda::level_meter_scale::dbFillProportion);
    gainFader_.setVertical(true);
    gainFader_.setShowText(false);
    gainFader_.onValueChange = [this]() {
        const auto v = gainFader_.getValue();
        gainValue_.setText(formatGainDb(v), juce::dontSendNotification);
        writeParameter(Util::kGainSlot, static_cast<float>(v));
    };
    // Right-click the gain fader gets the exact same menu as any other device
    // param (link to mod / macro, show automation lane, MIDI learn) by routing
    // to the overlay link slot that sits on top of the fader.
    gainFader_.onRightClick = [this]() { gainLinkSlot_.showContextMenu(); };
    addAndMakeVisible(gainFader_);

    styleReadoutLabel(gainValue_);
    gainValue_.setText(formatGainDb(0.0), juce::dontSendNotification);
    addAndMakeVisible(gainValue_);

    panLabel_.setRange(-1.0, 1.0, 0.0);
    panLabel_.setValue(0.0, juce::dontSendNotification);
    panLabel_.setFontSize(9.0f);
    panLabel_.setFillColour(fillColour);
    panLabel_.onValueChange = [this]() {
        writeParameter(Util::kPanSlot, static_cast<float>(panLabel_.getValue()));
    };
    addAndMakeVisible(panLabel_);

    widthLabel_.setRange(0.0, 2.0, 1.0);
    widthLabel_.setValue(1.0, juce::dontSendNotification);
    widthLabel_.setFontSize(9.0f);
    widthLabel_.setDecimalPlaces(2);
    widthLabel_.setFillColour(fillColour);
    widthLabel_.onValueChange = [this]() {
        writeParameter(Util::kWidthSlot, static_cast<float>(widthLabel_.getValue()));
    };
    addAndMakeVisible(widthLabel_);

    xoverLabel_.setRange(20.0, 500.0, 120.0);
    xoverLabel_.setValue(120.0, juce::dontSendNotification);
    xoverLabel_.setFontSize(9.0f);
    xoverLabel_.setSuffix(" Hz");
    xoverLabel_.setFillColour(fillColour);
    xoverLabel_.onValueChange = [this]() {
        writeParameter(Util::kLowMonoFreqSlot, static_cast<float>(xoverLabel_.getValue()));
    };
    addAndMakeVisible(xoverLabel_);

    styleNameLabel(gainName_, "GAIN");
    styleNameLabel(panName_, "PAN");
    styleNameLabel(widthName_, "WIDTH");
    styleNameLabel(xoverName_, "XOVER");
    addAndMakeVisible(gainName_);
    addAndMakeVisible(panName_);
    addAndMakeVisible(widthName_);
    addAndMakeVisible(xoverName_);

    const auto surface = DarkTheme::getColour(DarkTheme::SURFACE);
    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_ORANGE);
    const auto inactive = DarkTheme::getSecondaryTextColour();
    const auto bg = DarkTheme::getColour(DarkTheme::BACKGROUND);

    for (int i = 0; i < 4; ++i) {
        auto& btn = btns_[static_cast<size_t>(i)];
        btn.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
        btn.setButtonText(kBtnLabels[static_cast<size_t>(i)]);
        btn.setClickingTogglesState(true);
        btn.setColour(juce::TextButton::buttonColourId, surface);
        btn.setColour(juce::TextButton::buttonOnColourId, accent);
        btn.setColour(juce::TextButton::textColourOffId, inactive);
        btn.setColour(juce::TextButton::textColourOnId, bg);
        const int slot = Util::kMonoSlot + i;
        btn.onClick = [this, slot, i]() {
            const bool on = btns_[static_cast<size_t>(i)].getToggleState();
            writeParameter(slot, on ? 1.0f : 0.0f);
        };
        addAndMakeVisible(btn);
    }

    configureLinkSlots();
    addAndMakeVisible(gainLinkSlot_);
    addAndMakeVisible(panLinkSlot_);
}

CompiledUtilityView::~CompiledUtilityView() {
    for (auto& btn : btns_)
        btn.setLookAndFeel(nullptr);
}

void CompiledUtilityView::updateFromDevice(const magda::DeviceInfo& device) {
    deviceSnapshot_ = device;
    syncFromDevice();
}

void CompiledUtilityView::updateFromDevice(const magda::DeviceInfo& device,
                                           const ParamLinkContext* linkContext) {
    deviceSnapshot_ = device;
    if (linkContext != nullptr) {
        linkContext_ = *linkContext;
        hasLinkContext_ = true;
    } else {
        hasLinkContext_ = false;
    }
    syncFromDevice();
    refreshLinkSlotContext();
}

void CompiledUtilityView::writeParameter(int slotIndex, float displayValue) {
    if (compiledPlugin_ != nullptr) {
        if (auto* param = compiledPlugin_->getSlotParameter(slotIndex)) {
            const float normalized =
                compiledPlugin_->displayValueToNativeValue(slotIndex, displayValue);
            param->setParameterFromHost(normalized, juce::sendNotificationSync);
        }
    }

    if (onParameterChanged)
        onParameterChanged(slotIndex, displayValue);
}

void CompiledUtilityView::syncFromDevice() {
    using Util = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;
    const auto gain = valueForSlot(deviceSnapshot_, Util::kGainSlot, 0.0f);
    gainFader_.setValue(gain, juce::dontSendNotification);
    gainValue_.setText(formatGainDb(gain), juce::dontSendNotification);
    panLabel_.setValue(valueForSlot(deviceSnapshot_, Util::kPanSlot, 0.0f),
                       juce::dontSendNotification);
    widthLabel_.setValue(valueForSlot(deviceSnapshot_, Util::kWidthSlot, 1.0f),
                         juce::dontSendNotification);
    xoverLabel_.setValue(valueForSlot(deviceSnapshot_, Util::kLowMonoFreqSlot, 120.0f),
                         juce::dontSendNotification);
    for (int i = 0; i < 4; ++i) {
        const bool on = valueForSlot(deviceSnapshot_, Util::kMonoSlot + i, 0.0f) >= 0.5f;
        btns_[static_cast<size_t>(i)].setToggleState(on, juce::dontSendNotification);
    }
    updateLinkSlotValues();
}

void CompiledUtilityView::configureLinkSlots() {
    using Util = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;

    auto configure = [this](ParamSlotComponent& slot, int slotIndex, const char* name,
                            bool verticalOverlay) {
        slot.setOverlayOnly(true);
        slot.setLinkOverlayVertical(verticalOverlay);
        slot.setParamIndex(slotIndex);
        slot.setParamName(name);

        slot.onModLinkedWithAmount = [this](int, magda::ControlTarget target, float amount) {
            if (onLinkRequested)
                onLinkRequested(target.paramIndex, amount);
        };
        slot.onModAmountChanged = [this](int, magda::ControlTarget target, float amount) {
            if (onLinkAmountChanged)
                onLinkAmountChanged(target.paramIndex, amount);
        };
        slot.onMacroLinked = [this](int, magda::ControlTarget target) {
            if (onLinkRequested)
                onLinkRequested(target.paramIndex, 0.3f);
        };
        slot.onMacroLinkedWithAmount = [this](int, magda::ControlTarget target, float amount) {
            if (onLinkRequested)
                onLinkRequested(target.paramIndex, amount);
        };
        slot.onMacroAmountChanged = [this](int, magda::ControlTarget target, float amount) {
            if (onLinkAmountChanged)
                onLinkAmountChanged(target.paramIndex, amount);
        };
    };

    configure(gainLinkSlot_, Util::kGainSlot, "Gain", true);
    configure(panLinkSlot_, Util::kPanSlot, "Pan", false);

    gainLinkSlot_.onShowAutomationLane = [this]() {
        if (onShowAutomationLane)
            onShowAutomationLane(Util::kGainSlot);
    };
    panLinkSlot_.onShowAutomationLane = [this]() {
        if (onShowAutomationLane)
            onShowAutomationLane(Util::kPanSlot);
    };
}

void CompiledUtilityView::refreshLinkSlotContext() {
    auto apply = [this](ParamSlotComponent& slot) {
        slot.setDeviceId(hasLinkContext_ ? linkContext_.deviceId : magda::INVALID_DEVICE_ID);
        slot.setDevicePath(hasLinkContext_ ? linkContext_.devicePath : magda::ChainNodePath{});
        slot.setAvailableMods(hasLinkContext_ ? linkContext_.deviceMods : nullptr);
        slot.setAvailableRackMods(hasLinkContext_ ? linkContext_.rackMods : nullptr);
        slot.setAvailableTrackMods(hasLinkContext_ ? linkContext_.trackMods : nullptr);
        slot.setAvailableMacros(hasLinkContext_ ? linkContext_.deviceMacros : nullptr);
        slot.setAvailableRackMacros(hasLinkContext_ ? linkContext_.rackMacros : nullptr);
        slot.setAvailableTrackMacros(hasLinkContext_ ? linkContext_.trackMacros : nullptr);
        slot.setSelectedModIndex(hasLinkContext_ ? linkContext_.selectedModIndex : -1);
        slot.setSelectedMacroIndex(hasLinkContext_ ? linkContext_.selectedMacroIndex : -1);
    };

    apply(gainLinkSlot_);
    apply(panLinkSlot_);
    gainLinkSlot_.refreshLinkModeState();
    panLinkSlot_.refreshLinkModeState();

    // The overlay link slots hide their own valueSlider_ (the part that paints
    // the "automated" purple tint), so bind the automation target onto the
    // visible DraggableValueLabels directly so they highlight like the track
    // volume / pan readouts.
    auto bindTarget = [this](magda::DraggableValueLabel& label, int slotIndex) {
        if (!hasLinkContext_) {
            label.clearAutomationTarget();
            return;
        }
        magda::AutomationTarget target;
        target.kind = magda::ControlTarget::Kind::PluginParam;
        target.devicePath = linkContext_.devicePath;
        target.paramIndex = slotIndex;
        label.setAutomationTarget(target);
    };
    using Util = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;
    bindTarget(gainFader_, Util::kGainSlot);
    bindTarget(panLabel_, Util::kPanSlot);
    bindTarget(widthLabel_, Util::kWidthSlot);
    bindTarget(xoverLabel_, Util::kLowMonoFreqSlot);

    updateLinkSlotValues();
}

void CompiledUtilityView::updateLinkSlotValues() {
    using Util = magda::daw::audio::compiled::MagdaUtilityCompiledPlugin;

    auto update = [this](ParamSlotComponent& slot, int slotIndex, float fallback, bool& infoSet) {
        if (!infoSet) {
            if (auto* param = parameterForSlot(deviceSnapshot_, slotIndex)) {
                infoSet = true;
                slot.setParameterInfo(*param);
            }
        }
        slot.setParamValue(valueForSlot(deviceSnapshot_, slotIndex, fallback));
        slot.repaint();
    };

    update(gainLinkSlot_, Util::kGainSlot, 0.0f, gainLinkInfoSet_);
    update(panLinkSlot_, Util::kPanSlot, 0.0f, panLinkInfoSet_);
}

void CompiledUtilityView::resized() {
    constexpr int kPad = 6;
    constexpr int kLabelH = 11;
    constexpr int kControlH = 17;
    constexpr int kButtonH = 17;
    constexpr int kGap = 5;
    constexpr int kFaderW = 30;
    constexpr int kReadoutH = 16;
    constexpr int kTopInset = 3;

    auto body = getLocalBounds().reduced(kPad);
    if (body.isEmpty())
        return;

    const int contentW = body.getWidth();
    const int halfW = contentW / 2;
    const int labelledControlH = kLabelH + 2 + kControlH;
    body.removeFromTop(kTopInset);

    auto takeTop = [](juce::Rectangle<int>& area, int height) {
        return area.removeFromTop(juce::jmin(height, area.getHeight()));
    };

    auto takeBottom = [](juce::Rectangle<int>& area, int height) {
        return area.removeFromBottom(juce::jmin(height, area.getHeight()));
    };

    auto dropTop = [&takeTop](juce::Rectangle<int>& area, int height) { takeTop(area, height); };

    auto dropBottom = [&takeBottom](juce::Rectangle<int>& area, int height) {
        takeBottom(area, height);
    };

    auto layoutLabeledControl = [&](juce::Label& name, juce::Component& value,
                                    juce::Rectangle<int> area) {
        name.setBounds(takeTop(area, kLabelH));
        dropTop(area, 2);
        value.setBounds(area);
    };

    auto layoutSplitControls = [&](juce::Label& nameA, juce::Component& valueA, juce::Label& nameB,
                                   juce::Component& valueB, juce::Rectangle<int> area) {
        auto left = area.removeFromLeft(halfW - 1);
        area.removeFromLeft(2);
        layoutLabeledControl(nameA, valueA, left);
        layoutLabeledControl(nameB, valueB, area);
    };

    auto placeButtonRow = [&](juce::Rectangle<int> row, int leftIdx, int rightIdx) {
        auto left = row.removeFromLeft(halfW - 1);
        row.removeFromLeft(2);
        btns_[static_cast<size_t>(leftIdx)].setBounds(left.reduced(1, 0));
        btns_[static_cast<size_t>(rightIdx)].setBounds(row.reduced(1, 0));
    };

    auto lower = body;
    auto buttonRow2 = takeBottom(lower, kButtonH);
    dropBottom(lower, 3);
    auto buttonRow1 = takeBottom(lower, kButtonH);
    dropBottom(lower, kGap);
    auto stereoBlock = takeBottom(lower, labelledControlH);
    dropBottom(lower, kGap);
    auto panBlock = takeBottom(lower, labelledControlH);
    dropBottom(lower, kGap);
    auto gainValueBlock = takeBottom(lower, kReadoutH);

    auto gainBlock = lower;
    gainName_.setBounds(takeTop(gainBlock, kLabelH));
    dropTop(gainBlock, 3);
    auto faderArea = gainBlock;
    const int faderX = faderArea.getCentreX() - kFaderW / 2;
    gainFader_.setBounds(faderX, faderArea.getY(), kFaderW, faderArea.getHeight());
    gainLinkSlot_.setBounds(gainFader_.getBounds());

    gainValue_.setBounds(gainValueBlock);
    layoutLabeledControl(panName_, panLabel_, panBlock);
    panLinkSlot_.setBounds(panLabel_.getBounds());
    layoutSplitControls(widthName_, widthLabel_, xoverName_, xoverLabel_, stereoBlock);
    placeButtonRow(buttonRow1, 0, 1);
    placeButtonRow(buttonRow2, 2, 3);
    gainLinkSlot_.toFront(false);
    panLinkSlot_.toFront(false);
}

void CompiledUtilityView::bindPlugin(te::Plugin* plugin) {
    compiledPlugin_ =
        dynamic_cast<magda::daw::audio::compiled::MagdaUtilityCompiledPlugin*>(plugin);
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
        .preferredSlotWidth = 160,
    };
    return kSpec;
}

}  // namespace magda::daw::ui
