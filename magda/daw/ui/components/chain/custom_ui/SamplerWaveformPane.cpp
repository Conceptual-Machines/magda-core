// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/SamplerWaveformPane.hpp"

#include "DisplayListGraphics.hpp"
#include "core/GestureRouter.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/CursorManager.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SdkColourRoles.hpp"

namespace magda::daw::ui {

SamplerWaveformPane::SamplerWaveformPane() {
    view_.setChrome({.ruler = true, .scrollBar = true});
}

void SamplerWaveformPane::setAudio(const juce::AudioBuffer<float>* buffer, double lengthSeconds) {
    length_ = lengthSeconds;
    if (buffer == nullptr || buffer->getNumSamples() == 0) {
        view_.setSource(nullptr, 0.0);
        source_.reset();
        repaint();
        return;
    }
    // The view draws channel 0 and fits the whole sample to the pane.
    source_ = std::make_unique<sdk::BufferWaveformSource>(buffer->getReadPointer(0),
                                                          buffer->getNumSamples());
    view_.setSize(getWidth(), getHeight());
    view_.setSource(source_.get(), length_);
    repaint();
}

void SamplerWaveformPane::setPlayhead(double seconds) {
    if (std::abs(seconds - playhead_) <= 0.0001)
        return;
    playhead_ = seconds;
    repaint();
}

void SamplerWaveformPane::setGain(float gain) {
    view_.setGain(gain);
    if (hasAudio())
        repaint();
}

void SamplerWaveformPane::resized() {
    // The view keeps the whole sample in reach at the new width.
    view_.setSize(getWidth(), getHeight());
}

void SamplerWaveformPane::pullMarkers() {
    if (markers)
        view_.setMarkers(markers());
}

void SamplerWaveformPane::apply(const sdk::WaveformResponse& response) {
    if (response.markersChanged) {
        if (onMarkersMoved)
            onMarkersMoved(view_.markers());
        // The owner may round what it keeps; the view follows that.
        pullMarkers();
    }
    if (response.repaint || response.markersChanged)
        repaint();
}

sdk::WaveformPointer SamplerWaveformPane::pointer(const juce::MouseEvent& e) const {
    return {static_cast<float>(e.x), static_cast<float>(e.y), e.mods.isShiftDown(),
            e.mods.isCommandDown(),  e.mods.isAltDown(),      e.mods.isMiddleButtonDown()};
}

void SamplerWaveformPane::updateCursor(const juce::MouseEvent& e) {
    if (!hasAudio()) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }
    pullMarkers();
    switch (view_.cursor(pointer(e))) {
        case sdk::WaveformCursor::ResizeLeftRight:
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            break;
        case sdk::WaveformCursor::DraggingHand:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        case sdk::WaveformCursor::Zoom:
            setMouseCursor(CursorManager::getInstance().getZoomCursor());
            break;
        case sdk::WaveformCursor::ZoomIn:
            setMouseCursor(CursorManager::getInstance().getZoomInCursor());
            break;
        case sdk::WaveformCursor::ZoomOut:
            setMouseCursor(CursorManager::getInstance().getZoomOutCursor());
            break;
        case sdk::WaveformCursor::Normal:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
    }
}

void SamplerWaveformPane::showRulerMenu() {
    const bool samples = view_.chrome().units == sdk::WaveformUnits::Samples;
    juce::PopupMenu menu;
    menu.addItem(1, "Seconds", true, !samples);
    menu.addItem(2, "Samples", true, samples);
    menu.showMenuAsync(
        juce::PopupMenu::Options(),
        [safe = juce::Component::SafePointer<SamplerWaveformPane>(this)](int result) {
            if (safe == nullptr || result == 0)
                return;
            auto chrome = safe->view_.chrome();
            chrome.units = result == 2 ? sdk::WaveformUnits::Samples : sdk::WaveformUnits::Seconds;
            safe->view_.setChrome(chrome);
            safe->repaint();
        });
}

void SamplerWaveformPane::mouseDown(const juce::MouseEvent& e) {
    gesture_ = false;
    // An empty well is the load button's bigger twin, as its hint says.
    if (!hasAudio()) {
        if (onEmptyClicked)
            onEmptyClicked();
        return;
    }
    if (e.mods.isPopupMenu() && view_.overRuler(pointer(e))) {
        showRulerMenu();
        return;
    }
    gesture_ = true;
    pullMarkers();
    apply(view_.pointerDown(pointer(e)));
    if (e.mods.isCommandDown())
        setMouseCursor(CursorManager::getInstance().getZoomCursor());
}

void SamplerWaveformPane::mouseDrag(const juce::MouseEvent& e) {
    if (!gesture_ || !hasAudio())
        return;
    apply(view_.pointerDrag(pointer(e)));
    if (e.mods.isCommandDown())
        updateCursor(e);
}

void SamplerWaveformPane::mouseUp(const juce::MouseEvent& e) {
    if (gesture_)
        view_.pointerUp(pointer(e));
    gesture_ = false;
    updateCursor(e);
}

void SamplerWaveformPane::mouseMove(const juce::MouseEvent& e) {
    updateCursor(e);
}

void SamplerWaveformPane::mouseWheelMove(const juce::MouseEvent& e,
                                         const juce::MouseWheelDetails& wheel) {
    const auto gesture = magda::GestureRouter::getInstance().resolve(
        magda::GestureContext::Sampler, wheel, e.mods, e.getPosition());
    if (!hasAudio() || length_ <= 0.0 || gesture.type != magda::GestureActionType::ZoomHorizontal) {
        Component::mouseWheelMove(e, wheel);
        return;
    }
    apply(view_.zoomBy(1.0 + static_cast<double>(gesture.magnitude), pointer(e).x));
}

void SamplerWaveformPane::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
    // Empty, the well is a load button: a + over the hint (#3201).
    if (!hasAudio()) {
        auto& fonts = FontManager::getInstance();
        const auto plus = bounds.withSizeKeepingCentre(28, 28).translated(0, -10).toFloat();
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
        g.drawRoundedRectangle(plus.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM));
        g.setFont(fonts.getUIFont(18.0f));
        g.drawText("+", plus, juce::Justification::centred);
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_DIM2));
        g.setFont(fonts.getMonoFont(11.0f));
        g.drawText("Drop a sample or click to load",
                   bounds.withTop(juce::roundToInt(plus.getBottom()) + 6).withHeight(16),
                   juce::Justification::centred);
        return;
    }
    pullMarkers();
    view_.setPlayhead(length_ > 0.0 ? playhead_ : 0.0);
    view_.render(list_);
    sdk::juce_host::drawDisplayList(g, list_, sdkRoleColour);
}

}  // namespace magda::daw::ui
