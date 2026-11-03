// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/SamplerWaveformPane.hpp"

#include <cmath>
#include <variant>

#include "DisplayListGraphics.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/GestureRouter.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/CursorManager.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SdkColourRoles.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kClipWaveInset = 2;
// The most a thumbnail's length can differ from the sample's and still be the same audio.
constexpr double kSameLengthSeconds = 0.01;

template <class... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

/// The view's frame without its wave, the only paths it draws.
void copyWithoutWave(const sdk::display::DisplayList& frame, sdk::display::DisplayList& out) {
    namespace display = sdk::display;
    out.reset(frame.width(), frame.height());
    const Overloaded copy{
        [&](const display::FillRect& c) { out.fillRect(c.rect, c.paint, c.cornerRadius); },
        [&](const display::StrokeRect& c) {
            out.strokeRect(c.rect, c.paint, c.lineWidth, c.cornerRadius);
        },
        [](const display::FillPath&) {},
        [](const display::StrokePath&) {},
        [&](const display::FillEllipse& c) { out.fillEllipse(c.rect, c.paint); },
        [&](const display::StrokeEllipse& c) { out.strokeEllipse(c.rect, c.paint, c.lineWidth); },
        [&](const display::Text& c) {
            out.text(c.text, c.rect, c.colour, c.fontSize, c.justification);
        },
        [&](const display::ClipRect& c) { out.clipRect(c.rect); },
        [&](const display::Save&) { out.save(); },
        [&](const display::Restore&) { out.restore(); },
    };
    for (const auto& command : frame.commands())
        std::visit(copy, command);
}

}  // namespace

SamplerWaveformPane::SamplerWaveformPane() {
    view_.setChrome({.ruler = true, .scrollBar = true});
}

SamplerWaveformPane::~SamplerWaveformPane() {
    setClipFile({});
}

void SamplerWaveformPane::setAudio(const juce::AudioBuffer<float>* buffer, double lengthSeconds,
                                   const juce::String& sourceFile) {
    length_ = lengthSeconds;
    if (buffer == nullptr || buffer->getNumSamples() == 0) {
        setClipFile({});
        view_.setSource(nullptr, 0.0);
        source_.reset();
        repaint();
        return;
    }
    setClipFile(sourceFile);
    // The view draws channel 0 and fits the whole sample to the pane.
    source_ = std::make_unique<sdk::BufferWaveformSource>(buffer->getReadPointer(0),
                                                          buffer->getNumSamples());
    view_.setSize(getWidth(), getHeight());
    view_.setSource(source_.get(), length_);
    repaint();
}

void SamplerWaveformPane::setClipFile(const juce::String& file) {
    auto& thumbnails = magda::AudioThumbnailManager::getInstance();
    if (clipFile_.isNotEmpty())
        thumbnails.removeThumbnailChangeListener(clipFile_, this);
    clipFile_.clear();
    clipWave_ = {};
    auto* thumbnail = file.isNotEmpty() ? thumbnails.getThumbnail(file) : nullptr;
    if (thumbnail == nullptr ||
        std::abs(thumbnail->getTotalLength() - length_) > kSameLengthSeconds)
        return;
    clipFile_ = file;
    thumbnail->addChangeListener(this);
}

void SamplerWaveformPane::changeListenerCallback(juce::ChangeBroadcaster*) {
    clipWave_ = {};
    repaint();
}

void SamplerWaveformPane::setPlayhead(double seconds) {
    if (std::abs(seconds - playhead_) <= 0.0001)
        return;
    playhead_ = seconds;
    repaint();
}

void SamplerWaveformPane::setGain(float gain) {
    gain_ = gain;
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
    if (clipFile_.isEmpty()) {
        sdk::juce_host::drawDisplayList(g, list_, sdkRoleColour);
        return;
    }
    paintClipWave(g);
    copyWithoutWave(list_, chromeList_);
    sdk::juce_host::drawDisplayList(g, chromeList_, sdkRoleColour);
}

void SamplerWaveformPane::paintClipWave(juce::Graphics& g) {
    if (view_.pixelsPerSecond() <= 0.0)
        return;
    const int top = sdk::WaveformView::kRulerHeight + kClipWaveInset;
    const int bottom = getHeight() - sdk::WaveformView::kScrollBarHeight - kClipWaveInset;
    const double start = view_.scrollSeconds();
    const double end = juce::jmin(length_, start + getWidth() / view_.pixelsPerSecond());
    const auto area = juce::Rectangle<int>(
        0, top, juce::roundToInt((end - start) * view_.pixelsPerSecond()), bottom - top);
    if (area.isEmpty())
        return;

    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const ClipWaveKey key{start, end, area.getWidth(), area.getHeight(), gain_, scale};
    if (clipWave_.isNull() || key != clipWaveKey_) {
        clipWaveKey_ = key;
        clipWave_ = juce::Image(juce::Image::ARGB, juce::roundToInt(area.getWidth() * scale),
                                juce::roundToInt(area.getHeight() * scale), true);
        juce::Graphics wave(clipWave_);
        wave.addTransform(juce::AffineTransform::scale(scale));
        magda::AudioThumbnailManager::getInstance().drawWaveform(
            wave, area.withPosition(0, 0), clipFile_, start, end,
            ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY), gain_, true);
    }
    g.drawImage(clipWave_, area.toFloat());
}

}  // namespace magda::daw::ui
