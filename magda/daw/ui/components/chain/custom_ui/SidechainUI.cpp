#include "custom_ui/SidechainUI.hpp"

#include <vector>

#include "audio/plugins/SidechainPlugin.hpp"
#include "core/ControlTarget.hpp"
#include "core/TrackManager.hpp"
#include "ui/components/chain/modulation/SyncDivisionUi.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {

void setupSmallLabel(juce::Label& label, const juce::String& text, juce::Component* parent) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centred);
    parent->addAndMakeVisible(label);
}

magda::ControlTarget gainTarget(const magda::ChainNodePath& path) {
    return magda::ControlTarget::pluginParam(path, daw::audio::SidechainPlugin::kGainParamIndex);
}

}  // namespace

SidechainUI::SidechainUI() {
    // Curve editor bound to the bundled duck modulator (mods[0]). A generous
    // padding insets the curve field so extreme points (and the parked phase
    // dot at full level) sit clearly inside the widget.
    curveEditor_.setPadding(16);
    curveEditor_.setDrawContentBorder(true);
    curveEditor_.setGridDivisionsX(4);
    curveEditor_.setGridDivisionsY(4);
    addAndMakeVisible(curveEditor_);

    // Duck depth = the mod link's amount on the gain param (negated: the link
    // is stored at negative depth so the curve ducks).
    setupSmallLabel(depthLabel_, "DEPTH", this);
    depthSlider_.setRange(0.0, 100.0, 1.0);
    depthSlider_.setValue(100.0, juce::dontSendNotification);
    depthSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    depthSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)) + " %"; });
    depthSlider_.onValueChanged = [this](double value) {
        auto path = currentPath();
        magda::TrackManager::getInstance().setModLinkAmount(path, 0, gainTarget(path),
                                                            static_cast<float>(-value / 100.0));
    };
    addAndMakeVisible(depthSlider_);

    // Trigger sync division (duck curve length). Stepped, index-mapped.
    setupSmallLabel(divisionLabel_, "SYNC", this);
    divisionSlider_.setRange(0.0, static_cast<double>(kNumSyncDivisions - 1), 1.0);
    divisionSlider_.setValue(static_cast<double>(syncDivisionToIndex(magda::SyncDivision::Quarter)),
                             juce::dontSendNotification);
    divisionSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    divisionSlider_.setShowFillIndicator(false);
    divisionSlider_.setValueFormatter([](double v) {
        return syncDivisionLabelForIndex(
            juce::jlimit(0, kNumSyncDivisions - 1, static_cast<int>(std::round(v))));
    });
    divisionSlider_.onValueChanged = [this](double value) {
        const int idx = juce::jlimit(0, kNumSyncDivisions - 1, static_cast<int>(std::round(value)));
        magda::TrackManager::getInstance().setModSyncDivision(currentPath(), 0,
                                                              indexToSyncDivision(idx));
    };
    divisionSlider_.setRightClickEditsText(false);
    addAndMakeVisible(divisionSlider_);

    // Loop vs 1-Shot. One-shot is the classic sidechain behavior (duck plays
    // once per trigger and holds at no-duck); loop pumps continuously at the
    // sync division while notes retrigger the phase.
    setupSmallLabel(modeLabel_, "MODE", this);
    modeButton_.setClickingTogglesState(true);
    modeButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    modeButton_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    modeButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getAccentColour().withAlpha(0.6f));
    modeButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    modeButton_.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    modeButton_.setToggleState(true, juce::dontSendNotification);
    modeButton_.setButtonText("1-Shot");
    modeButton_.onClick = [this]() {
        const bool oneShot = modeButton_.getToggleState();
        modeButton_.setButtonText(oneShot ? "1-Shot" : "Loop");
        magda::TrackManager::getInstance().setModOneShot(currentPath(), 0, oneShot);
    };
    addAndMakeVisible(modeButton_);

    // Attack / release gain smoothing — real plugin params, macro/mod linkable.
    setupSmallLabel(attackLabel_, "ATK", this);
    attackSlider_.setParamIndex(daw::audio::SidechainPlugin::kAttackParamIndex);
    attackSlider_.setRange(0.0, 50.0, 0.1);
    attackSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    attackSlider_.setValueFormatter([](double v) { return juce::String(v, 1) + " ms"; });
    attackSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(daw::audio::SidechainPlugin::kAttackParamIndex,
                               static_cast<float>(value));
    };
    addAndMakeVisible(attackSlider_);

    setupSmallLabel(releaseLabel_, "REL", this);
    releaseSlider_.setParamIndex(daw::audio::SidechainPlugin::kReleaseParamIndex);
    releaseSlider_.setRange(0.0, 500.0, 1.0);
    releaseSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    releaseSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)) + " ms"; });
    releaseSlider_.onValueChanged = [this](double value) {
        if (onParameterChanged)
            onParameterChanged(daw::audio::SidechainPlugin::kReleaseParamIndex,
                               static_cast<float>(value));
    };
    addAndMakeVisible(releaseSlider_);
}

magda::ChainNodePath SidechainUI::currentPath() const {
    return getNodePath ? getNodePath() : magda::ChainNodePath{};
}

magda::DeviceInfo* SidechainUI::liveDevice() const {
    return magda::TrackManager::getInstance().getDeviceInChainByPath(currentPath());
}

void SidechainUI::bindCurveEditor() {
    auto* dev = liveDevice();
    if (dev == nullptr || dev->mods.empty())
        return;

    auto* mod = &dev->mods[0];
    if (curveEditor_.getModInfo() != mod)
        curveEditor_.setModInfo(mod);
    else
        curveEditor_.syncFromModInfo();
    curveEditor_.setUndoTarget(currentPath(), 0);
    curveEditor_.setShowLoopRegion(mod->useLoopRegion);
}

void SidechainUI::updateFromDevice(const magda::DeviceInfo& device) {
    updateFromParameters(device.parameters);
    refreshFromModel();
}

void SidechainUI::refreshFromModel() {
    bindCurveEditor();

    if (auto* dev = liveDevice(); dev != nullptr && !dev->mods.empty()) {
        auto& mod = dev->mods[0];
        divisionSlider_.setValue(static_cast<double>(syncDivisionToIndex(mod.syncDivision)),
                                 juce::dontSendNotification);
        modeButton_.setToggleState(mod.oneShot, juce::dontSendNotification);
        modeButton_.setButtonText(mod.oneShot ? "1-Shot" : "Loop");
        if (auto* link = mod.getLink(gainTarget(currentPath())))
            depthSlider_.setValue(-link->amount * 100.0, juce::dontSendNotification);
    }
}

void SidechainUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    constexpr int attackIdx = daw::audio::SidechainPlugin::kAttackParamIndex;
    constexpr int releaseIdx = daw::audio::SidechainPlugin::kReleaseParamIndex;
    if (static_cast<int>(params.size()) > attackIdx)
        attackSlider_.setValue(params[static_cast<size_t>(attackIdx)].currentValue,
                               juce::dontSendNotification);
    if (static_cast<int>(params.size()) > releaseIdx)
        releaseSlider_.setValue(params[static_cast<size_t>(releaseIdx)].currentValue,
                                juce::dontSendNotification);
}

std::vector<LinkableTextSlider*> SidechainUI::getLinkableSliders() {
    return {&attackSlider_, &releaseSlider_};
}

void SidechainUI::paint(juce::Graphics& g) {
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));
}

void SidechainUI::resized() {
    auto area = getLocalBounds().reduced(6);
    constexpr int labelHeight = 14;
    constexpr int controlHeight = 18;

    auto controlRow = area.removeFromBottom(labelHeight + controlHeight);
    const auto curveBounds = area.reduced(0, 2);
    curveEditor_.setBounds(curveBounds);

    // X gridlines stay at 4 (musical quarters of the cycle); pick the value
    // gridline count from the aspect ratio so the cells come out square.
    if (curveBounds.getWidth() > 0) {
        const int squareY = juce::roundToInt(4.0 * curveBounds.getHeight() /
                                             static_cast<double>(curveBounds.getWidth()));
        curveEditor_.setGridDivisionsY(juce::jlimit(2, 10, squareY));
    }

    const int colWidth = controlRow.getWidth() / 5;
    auto layoutColumn = [&](juce::Label& label, juce::Component& control) {
        auto col = controlRow.removeFromLeft(colWidth).reduced(2, 0);
        label.setBounds(col.removeFromTop(labelHeight));
        control.setBounds(col.removeFromTop(controlHeight));
    };
    layoutColumn(depthLabel_, depthSlider_);
    layoutColumn(divisionLabel_, divisionSlider_);
    layoutColumn(modeLabel_, modeButton_);
    layoutColumn(attackLabel_, attackSlider_);
    layoutColumn(releaseLabel_, releaseSlider_);
}

}  // namespace magda::daw::ui
