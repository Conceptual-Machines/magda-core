#include "MiniChainRow.hpp"

#include <BinaryData.h>

#include "../../../audio/AudioBridge.hpp"
#include "../../../engine/AudioEngine.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../chain/layout/NodeHeaderStyles.hpp"
#include "../common/SvgButton.hpp"
#include "../common/TextSlider.hpp"
#include "core/ChainNodePath.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "ui/dialogs/ParameterConfigDialog.hpp"

namespace magda {

MiniChainRow::MiniChainRow() {
    setInterceptsMouseClicks(true, true);
    paramSliders_.reserve(kMaxExpandedParams);
    trackedParamIndices_.reserve(kMaxExpandedParams);
}

MiniChainRow::~MiniChainRow() {
    stopTimer();
}

void MiniChainRow::setDevice(const ChainNodePath& devicePath, AudioEngine* engine,
                             const juce::String& name, bool bypassed) {
    const bool deviceChanged = devicePath_ != devicePath;
    devicePath_ = devicePath;
    engine_ = engine;
    deviceName_ = name;
    bypassed_ = bypassed;
    if (deviceChanged) {
        paramsResolved_ = false;
        paramSliders_.clear();
        paramLabels_.clear();
        trackedParamIndices_.clear();
        retainExpandedForFadeOut_ = false;
        paramsFadeActive_ = false;
        paramsAlpha_ = 1.0f;
    }

    // "Open native editor" icon. Only genuine external plugins (VST3/AU/VST)
    // have a native editor window worth popping; every MAGDA-internal device
    // (TE built-ins, the magda_* Faust effects, native instruments, analysis)
    // reports PluginFormat::Internal and edits inline, so it gets no icon.
    const auto* devInfo = devicePath_.isValid()
                              ? TrackManager::getInstance().getDeviceInChainByPath(devicePath_)
                              : nullptr;
    const bool wantUiButton = devInfo != nullptr && devInfo->format != PluginFormat::Internal;
    if (wantUiButton && uiButton_ == nullptr) {
        uiButton_ = std::make_unique<SvgButton>("UI", BinaryData::open_in_new_svg,
                                                BinaryData::open_in_new_svgSize);
        daw::ui::node_header::applyHeaderIconStyle(*uiButton_,
                                                   DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        uiButton_->onClick = [this]() {
            if (engine_ == nullptr)
                return;
            if (auto* bridge = engine_->getAudioBridge()) {
                const bool isOpen = bridge->togglePluginWindow(devicePath_);
                uiButton_->setToggleState(isOpen, juce::dontSendNotification);
                uiButton_->setActive(isOpen);
            }
        };
        addAndMakeVisible(*uiButton_);
    }
    if (uiButton_)
        uiButton_->setVisible(wantUiButton);

    repaint();
}

void MiniChainRow::setBypassedState(bool bypassed) {
    if (bypassed_ == bypassed)
        return;
    bypassed_ = bypassed;
    repaint();
}

void MiniChainRow::setPluginEditorOpen(bool open) {
    if (uiButton_ == nullptr)
        return;
    uiButton_->setToggleState(open, juce::dontSendNotification);
    uiButton_->setActive(open);
}

void MiniChainRow::setExpanded(bool expanded) {
    if (expanded_ == expanded)
        return;
    expanded_ = expanded;
    if (expanded_) {
        retainExpandedForFadeOut_ = false;
        if (!paramsResolved_)
            resolveParams();
    } else {
        retainExpandedForFadeOut_ = !trackedParamIndices_.empty();
    }
    startParamsFade(expanded_);
    const bool showParams = isParamsLaidOut();
    for (auto& slider : paramSliders_)
        if (slider)
            slider->setVisible(showParams);
    for (auto& label : paramLabels_)
        if (label)
            label->setVisible(showParams);
    applyParamsAlpha();
    updateTimerState();
    resized();
    repaint();
    if (onExpandChanged)
        onExpandChanged();
}

int MiniChainRow::preferredHeight() const {
    if (!isParamsLaidOut() || trackedParamIndices_.empty())
        return kCollapsedHeight;
    const int fullParamsHeight =
        static_cast<int>(trackedParamIndices_.size()) * kParamRowHeight + 2;
    if (!paramsFadeActive_)
        return kCollapsedHeight + (expanded_ ? fullParamsHeight : 0);
    return kCollapsedHeight + juce::roundToInt(static_cast<float>(fullParamsHeight) * paramsAlpha_);
}

void MiniChainRow::resolveParams() {
    paramsResolved_ = true;
    paramSliders_.clear();
    paramLabels_.clear();
    trackedParamIndices_.clear();
    if (!devicePath_.isValid() || engine_ == nullptr)
        return;

    // Read values from the device model and write through TrackManager, both in
    // display units. This is the same path the device chain uses, so the mini
    // row stays in sync regardless of how the device maps display values onto
    // its live parameter (Faust devices keep a normalized 0..1 native param).
    auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(devicePath_);
    if (devInfo == nullptr)
        return;
    if (devInfo->uniqueId.isNotEmpty())
        daw::ui::ParameterConfigDialog::applyConfigToDevice(devInfo->uniqueId, *devInfo);
    const auto path = devicePath_;

    auto addParamSlider = [&](const ParameterInfo& paramInfo) {
        if (paramInfo.paramIndex < 0)
            return;

        trackedParamIndices_.push_back(paramInfo.paramIndex);

        auto label = std::make_unique<juce::Label>();
        label->setText(paramInfo.name, juce::dontSendNotification);
        label->setFont(FontManager::getInstance().getUIFont(9.0f));
        label->setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_DIM));
        label->setJustificationType(juce::Justification::centredLeft);
        label->setInterceptsMouseClicks(false, false);
        label->setAlpha(paramsAlpha_);
        label->setVisible(isParamsLaidOut());
        addAndMakeVisible(*label);
        paramLabels_.push_back(std::move(label));

        // Same value control as the device chain: a TextSlider that displays
        // the parameter's real value and formats/parses it from ParameterInfo.
        auto slider = std::make_unique<daw::ui::TextSlider>(daw::ui::TextSlider::Format::Decimal);
        slider->setParameterInfo(paramInfo);
        slider->setValue(paramInfo.currentValue, juce::dontSendNotification);
        slider->setFont(FontManager::getInstance().getUIFont(10.0f));
        slider->setTextColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        const int paramIndex = paramInfo.paramIndex;
        slider->onValueChanged = [path, paramIndex](double v) {
            TrackManager::getInstance().setDeviceParameterValue(path, paramIndex,
                                                                static_cast<float>(v));
        };
        slider->setAlpha(paramsAlpha_);
        slider->setVisible(isParamsLaidOut());
        addAndMakeVisible(*slider);
        paramSliders_.push_back(std::move(slider));
    };

    // 1) Explicit user selection from the parameter config dialog's "Mini"
    //    column (indices into devInfo->parameters), in the order chosen.
    for (int idx : devInfo->miniMixerParameters) {
        if (static_cast<int>(trackedParamIndices_.size()) >= kMaxExpandedParams)
            break;
        if (idx >= 0 && idx < static_cast<int>(devInfo->parameters.size())) {
            const auto& paramInfo = devInfo->parameters[static_cast<size_t>(idx)];
            if (!paramInfo.hidden)
                addParamSlider(paramInfo);
        }
    }

    // 2) Fallback (no explicit selection): first N non-hidden parameters in
    //    device order.
    if (trackedParamIndices_.empty()) {
        for (const auto& paramInfo : devInfo->parameters) {
            if (static_cast<int>(trackedParamIndices_.size()) >= kMaxExpandedParams)
                break;
            if (paramInfo.hidden)
                continue;
            addParamSlider(paramInfo);
        }
    }
}

void MiniChainRow::startParamsFade(bool expanding) {
    paramsFadeActive_ = true;
    paramsFadeStartMs_ = juce::Time::getMillisecondCounterHiRes();
    paramsFadeStartAlpha_ = expanding ? 0.0f : paramsAlpha_;
    paramsFadeTargetAlpha_ = expanding ? 1.0f : 0.0f;
    paramsAlpha_ = paramsFadeStartAlpha_;
}

void MiniChainRow::advanceParamsFade() {
    if (!paramsFadeActive_)
        return;

    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - paramsFadeStartMs_;
    const auto progress = static_cast<float>(juce::jlimit(0.0, 1.0, elapsed / kParamsFadeMs));
    paramsAlpha_ =
        paramsFadeStartAlpha_ + (paramsFadeTargetAlpha_ - paramsFadeStartAlpha_) * progress;
    applyParamsAlpha();
    resized();
    repaint();
    if (onExpandChanged)
        onExpandChanged();

    if (progress < 1.0f)
        return;

    paramsFadeActive_ = false;
    paramsAlpha_ = expanded_ ? 1.0f : 0.0f;
    if (!expanded_) {
        retainExpandedForFadeOut_ = false;
        for (auto& slider : paramSliders_)
            if (slider)
                slider->setVisible(false);
        for (auto& label : paramLabels_)
            if (label)
                label->setVisible(false);
    }
    applyParamsAlpha();
    resized();
    repaint();
    if (onExpandChanged)
        onExpandChanged();
    updateTimerState();
}

void MiniChainRow::applyParamsAlpha() {
    for (auto& slider : paramSliders_)
        if (slider)
            slider->setAlpha(paramsAlpha_);
    for (auto& label : paramLabels_)
        if (label)
            label->setAlpha(paramsAlpha_);
}

void MiniChainRow::updateTimerState() {
    if ((expanded_ && !trackedParamIndices_.empty()) || paramsFadeActive_)
        startTimerHz(30);
    else
        stopTimer();
}

void MiniChainRow::timerCallback() {
    advanceParamsFade();

    // Keep the sliders in sync with the live parameter values (automation,
    // external changes, etc.). Skips notification to avoid feedback loops.
    if (!expanded_)
        return;
    const auto* devInfo = TrackManager::getInstance().getDeviceInChainByPath(devicePath_);
    if (devInfo == nullptr)
        return;
    for (size_t i = 0; i < paramSliders_.size(); ++i) {
        auto* slider = paramSliders_[i].get();
        if (slider == nullptr || slider->isBeingDragged())
            continue;
        const int paramIndex = (i < trackedParamIndices_.size()) ? trackedParamIndices_[i] : -1;
        const auto* pInfo = devInfo->findParameterByIndex(paramIndex);
        if (pInfo == nullptr)
            continue;
        const auto v = static_cast<double>(pInfo->currentValue);
        if (std::abs(slider->getValue() - v) > 1e-6)
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
    if (uiButton_ != nullptr && uiButton_->isVisible()) {
        auto uiZone = headRect.removeFromRight(16);
        uiButton_->setBounds(uiZone.withSizeKeepingCentre(14, 14));
    }
    nameRect_ = headRect;

    if (isParamsLaidOut() && !paramSliders_.empty()) {
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
    if (!devicePath_.isValid())
        return;
    const auto pos = event.getPosition();
    if (bypassRect_.contains(pos)) {
        // Update local state and repaint first, then notify. setDeviceInChain
        // BypassedByPath fires a synchronous devicePropertyChanged that may
        // rebuild/destroy this row, so touch no members after the call.
        const bool newBypassed = !bypassed_;
        const auto path = devicePath_;
        bypassed_ = newBypassed;
        repaint();
        TrackManager::getInstance().setDeviceInChainBypassedByPath(path, newBypassed);
        return;
    }
    // Toggle expand on click anywhere else in the row head.
    if (pos.getY() < kCollapsedHeight)
        setExpanded(!expanded_);
}

}  // namespace magda
