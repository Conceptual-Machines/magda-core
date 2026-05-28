#include "MiniChainRow.hpp"

#include <BinaryData.h>

#include "../../../audio/AudioBridge.hpp"
#include "../../../engine/AudioEngine.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"

namespace magda {

MiniChainRow::MiniChainRow() {
    setInterceptsMouseClicks(true, true);
    paramSliders_.reserve(kMaxExpandedParams);
    trackedParams_.reserve(kMaxExpandedParams);
}

MiniChainRow::~MiniChainRow() {
    stopTimer();
}

void MiniChainRow::setDevice(TrackId trackId, DeviceId deviceId, AudioEngine* engine,
                             const juce::String& name, bool bypassed) {
    const bool deviceChanged = (deviceId_ != deviceId || trackId_ != trackId);
    trackId_ = trackId;
    deviceId_ = deviceId;
    engine_ = engine;
    deviceName_ = name;
    bypassed_ = bypassed;
    if (deviceChanged) {
        paramsResolved_ = false;
        paramSliders_.clear();
        trackedParams_.clear();
    }
    repaint();
}

void MiniChainRow::setExpanded(bool expanded) {
    if (expanded_ == expanded)
        return;
    expanded_ = expanded;
    if (expanded_ && !paramsResolved_)
        resolveParams();
    for (auto& slider : paramSliders_)
        if (slider)
            slider->setVisible(expanded_);
    for (auto& label : paramLabels_)
        if (label)
            label->setVisible(expanded_);
    if (expanded_ && !trackedParams_.empty())
        startTimerHz(15);
    else
        stopTimer();
    resized();
    repaint();
    if (onExpandChanged)
        onExpandChanged();
}

int MiniChainRow::preferredHeight() const {
    if (!expanded_ || trackedParams_.empty())
        return kCollapsedHeight;
    return kCollapsedHeight + static_cast<int>(trackedParams_.size()) * kParamRowHeight + 2;
}

void MiniChainRow::resolveParams() {
    paramsResolved_ = true;
    paramSliders_.clear();
    paramLabels_.clear();
    trackedParams_.clear();
    if (deviceId_ == INVALID_DEVICE_ID || engine_ == nullptr)
        return;
    auto* bridge = engine_->getAudioBridge();
    if (bridge == nullptr)
        return;
    auto pluginPtr = bridge->getPlugin(deviceId_);
    if (pluginPtr == nullptr)
        return;

    // Curated parameter list: skips slot-level Dry/Wet and hidden params,
    // uses the plugin spec's names / units / scales. paramIndex is the
    // TE-side index so we can still resolve the live AutomatableParameter.
    const auto* devInfo = TrackManager::getInstance().getDevice(trackId_, deviceId_);
    if (devInfo == nullptr)
        return;
    auto teParams = pluginPtr->getAutomatableParameters();

    int placed = 0;
    for (const auto& paramInfo : devInfo->parameters) {
        if (placed >= kMaxExpandedParams)
            break;
        if (paramInfo.hidden)
            continue;
        if (paramInfo.paramIndex < 0 || paramInfo.paramIndex >= teParams.size())
            continue;
        auto* param = teParams[paramInfo.paramIndex];
        if (param == nullptr)
            continue;

        trackedParams_.push_back(param);

        auto label = std::make_unique<juce::Label>();
        label->setText(paramInfo.name, juce::dontSendNotification);
        label->setFont(FontManager::getInstance().getUIFont(9.0f));
        label->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_DIM));
        label->setJustificationType(juce::Justification::centredLeft);
        label->setInterceptsMouseClicks(false, false);
        label->setVisible(expanded_);
        addAndMakeVisible(*label);
        paramLabels_.push_back(std::move(label));

        auto slider =
            std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        slider->setRange(0.0, 1.0, 0.0);
        slider->setValue(param->getCurrentNormalisedValue(), juce::dontSendNotification);
        slider->setColour(juce::Slider::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
        slider->setColour(juce::Slider::trackColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        slider->setColour(juce::Slider::thumbColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_BLUE_LIGHT));
        slider->setWantsKeyboardFocus(false);
        slider->onValueChange = [param, sliderPtr = slider.get()]() {
            param->setParameterFromHost(static_cast<float>(sliderPtr->getValue()),
                                        juce::sendNotificationSync);
        };
        slider->setVisible(expanded_);
        addAndMakeVisible(*slider);
        paramSliders_.push_back(std::move(slider));
        ++placed;
    }
}

void MiniChainRow::timerCallback() {
    // Keep the sliders in sync with the live parameter values (automation,
    // external changes, etc.). Skips notification to avoid feedback loops.
    for (size_t i = 0; i < paramSliders_.size(); ++i) {
        auto* slider = paramSliders_[i].get();
        auto* param = (i < trackedParams_.size()) ? trackedParams_[i] : nullptr;
        if (slider == nullptr || param == nullptr)
            continue;
        const auto v = static_cast<double>(param->getCurrentNormalisedValue());
        if (std::abs(slider->getValue() - v) > 1e-4)
            slider->setValue(v, juce::dontSendNotification);
    }
}

void MiniChainRow::paint(juce::Graphics& g) {
    auto headRect = getLocalBounds().removeFromTop(kCollapsedHeight);

    // Row background
    g.setColour(DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    g.fillRect(headRect);

    // Bypass dot — green when active, dim when bypassed.
    constexpr int dotSize = 8;
    auto dotBounds = bypassRect_.withSizeKeepingCentre(dotSize, dotSize).toFloat();
    g.setColour(bypassed_ ? DarkTheme::getColour(DarkTheme::TEXT_DISABLED)
                          : DarkTheme::getColour(DarkTheme::ACCENT_GREEN));
    g.fillEllipse(dotBounds);

    // Device name
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.setColour(bypassed_ ? DarkTheme::getColour(DarkTheme::TEXT_DIM)
                          : DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.drawText(deviceName_, nameRect_.reduced(2, 0), juce::Justification::centredLeft, true);

    // Chevron (down when expanded, right when collapsed).
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_DIM));
    auto centre = chevronRect_.getCentre().toFloat();
    juce::Path arrow;
    if (expanded_) {
        arrow.addTriangle(centre.x - 4.0f, centre.y - 2.0f, centre.x + 4.0f, centre.y - 2.0f,
                          centre.x, centre.y + 3.0f);
    } else {
        arrow.addTriangle(centre.x - 2.0f, centre.y - 4.0f, centre.x - 2.0f, centre.y + 4.0f,
                          centre.x + 3.0f, centre.y);
    }
    g.fillPath(arrow);
}

void MiniChainRow::resized() {
    auto headRect = getLocalBounds().removeFromTop(kCollapsedHeight);
    constexpr int dotZoneWidth = 16;
    constexpr int chevronZoneWidth = 14;
    bypassRect_ = headRect.removeFromLeft(dotZoneWidth);
    chevronRect_ = headRect.removeFromRight(chevronZoneWidth);
    nameRect_ = headRect;

    if (expanded_ && !paramSliders_.empty()) {
        auto paramsArea = getLocalBounds().withTrimmedTop(kCollapsedHeight + 2);
        for (size_t i = 0; i < paramSliders_.size(); ++i) {
            auto* slider = paramSliders_[i].get();
            auto* label = (i < paramLabels_.size()) ? paramLabels_[i].get() : nullptr;
            if (slider == nullptr)
                continue;
            auto row = paramsArea.removeFromTop(kParamRowHeight);
            if (label != nullptr) {
                auto labelArea =
                    row.removeFromLeft(juce::jlimit(28, 48, row.getWidth() * 35 / 100));
                label->setBounds(labelArea.reduced(2, 0));
            }
            slider->setBounds(row.reduced(2, 2));
        }
    }
}

void MiniChainRow::mouseDown(const juce::MouseEvent& event) {
    if (deviceId_ == INVALID_DEVICE_ID)
        return;
    const auto pos = event.getPosition();
    if (bypassRect_.contains(pos)) {
        TrackManager::getInstance().setDeviceBypassed(trackId_, deviceId_, !bypassed_);
        bypassed_ = !bypassed_;
        repaint();
        return;
    }
    // Toggle expand on click anywhere else in the row head.
    if (pos.getY() < kCollapsedHeight)
        setExpanded(!expanded_);
}

}  // namespace magda
