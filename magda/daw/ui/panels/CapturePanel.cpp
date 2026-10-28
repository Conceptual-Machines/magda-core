// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CapturePanel.hpp"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>

#include "../../audio/io/AudioIOControl.hpp"
#include "../themes/ActiveTheme.hpp"
#include "../themes/FontManager.hpp"
#include "core/Config.hpp"
#include "core/GestureRouter.hpp"

namespace magda {

namespace {

constexpr int kSideWidth = 96;
constexpr int kIconSize = 20;
constexpr int kIconGap = 6;
constexpr int kRulerHeight = 22;  // the ruler's compact editor height
/// Room either side of the lanes so the ruler's first and last labels are not cut off.
constexpr int kRulerPadding = 8;
constexpr int kRulerOverhang = 18;
constexpr int kScrollBarHeight = 10;
constexpr int kPlayheadMs = 30;

/// The closest the view zooms: a pixel for every two samples.
constexpr double kSamplesPerPixelAtMost = 2.0;

juce::String clock(double seconds) {
    const auto tenths = static_cast<int>(std::round(std::abs(seconds) * 10.0));
    return juce::String(seconds < 0.0 ? "-" : "") + juce::String(tenths / 600) + ":" +
           juce::String((tenths / 10) % 60).paddedLeft('0', 2) + "." + juce::String(tenths % 10);
}

/// The transport's tile look: its glyph and tile colours, the memory amber when lit.
void styleButton(SvgButton& button) {
    button.setActiveColor(ActiveTheme::getColour(ActiveTheme::TRANSPORT_MEMORY));
    button.setPressedColor(ActiveTheme::getColour(ActiveTheme::TRANSPORT_MEMORY));
    button.setHoverColor(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    button.setNormalColor(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    button.setIconPadding(0.0f);
    button.setStateColourReplacement(juce::Colour(0xFF1A1A1A), ActiveTheme::TRANSPORT_TILE,
                                     ActiveTheme::TRANSPORT_MEMORY);
    button.setStateColourReplacement(juce::Colour(0xFF444444), ActiveTheme::TRANSPORT_WELL_BORDER,
                                     ActiveTheme::TRANSPORT_MEMORY);
    button.setStateColourReplacement(juce::Colour(0xFFBCBCBC), ActiveTheme::TRANSPORT_GLYPH,
                                     ActiveTheme::TEXT_BRIGHT);
    button.setStateColourReplacement(juce::Colour(0xFFB3B3B3), ActiveTheme::TRANSPORT_GLYPH,
                                     ActiveTheme::TEXT_BRIGHT);
}

}  // namespace

CapturePanel::CapturePanel(AudioEngine& engine) : engine_(engine) {
    playButton_ =
        std::make_unique<SvgButton>("Play", BinaryData::play_svg, BinaryData::play_svgSize,
                                    BinaryData::stop_svg, BinaryData::stop_svgSize);
    keepAudioButton_ = std::make_unique<SvgButton>("Import Audio", BinaryData::iconaudioboldm_svg,
                                                   BinaryData::iconaudioboldm_svgSize);
    keepMidiButton_ = std::make_unique<SvgButton>("Import MIDI", BinaryData::iconmidiboldm_svg,
                                                  BinaryData::iconmidiboldm_svgSize);
    // The browser's icons come without a tile; they sit on the transport's.
    for (auto* button : {keepAudioButton_.get(), keepMidiButton_.get()}) {
        button->setNormalBackgroundColor(ActiveTheme::TRANSPORT_TILE);
        button->setActiveBackgroundColor(ActiveTheme::TRANSPORT_TILE);
    }
    for (auto* button : {playButton_.get(), keepAudioButton_.get(), keepMidiButton_.get()}) {
        styleButton(*button);
        addAndMakeVisible(*button);
    }
    for (auto* button : {keepAudioButton_.get(), keepMidiButton_.get()})
        button->setIconPadding(3.0f);
    playButton_->onClick = [this] { togglePlayback(); };
    keepAudioButton_->onClick = [this] { keep(true, false); };
    keepMidiButton_->onClick = [this] { keep(false, true); };
    playButton_->setTooltip("Hear the selection, or what is in view");
    keepAudioButton_->setTooltip("Import the selected audio on a new track");
    keepMidiButton_->setTooltip("Import the selected notes on the track that hears their input");

    // Seconds on the ruler: at 60 BPM its beats are seconds and its zoom is ours.
    ruler_.setDisplayMode(TimeRuler::DisplayMode::Seconds);
    ruler_.setTempo(60.0);
    ruler_.setRelativeMode(true);
    ruler_.setCompact(true);
    ruler_.setLeftPadding(kRulerPadding);
    ruler_.setGestureContext(GestureContext::Arrangement);
    ruler_.onZoomChanged = [this](double pixelsPerBeat, double, int anchorScreenX) {
        setZoom(pixelsPerBeat, anchorScreenX - kRulerPadding);
    };
    ruler_.onScrollRequested = [this](int deltaX) {
        fitted_ = false;
        setScroll(scrollX_ + deltaX);
    };
    addAndMakeVisible(ruler_);

    scrollBar_.onRangeChanged = [this](double start, double end) {
        const auto lanes = lanesArea();
        if (end <= start || windowSeconds() <= 0.0)
            return;
        pixelsPerSecond_ = lanes.getWidth() / ((end - start) * windowSeconds());
        scrollX_ = start * contentWidth();
        fitted_ = false;
        syncView();
    };
    addAndMakeVisible(scrollBar_);

    player_.setSource(&transport_);
    output_.setSource(&player_);
    refresh();
}

CapturePanel::~CapturePanel() {
    stopPlayback();
    if (outputAttached_)
        if (auto* audio = engine_.getAudioIO())
            audio->removeCallback(&output_);
    transport_.setSource(nullptr);
    player_.setSource(nullptr);
}

void CapturePanel::visibilityChanged() {
    if (isVisible()) {
        snapshot();
        if (!outputAttached_)
            if (auto* audio = engine_.getAudioIO()) {
                audio->addCallback(&output_);
                outputAttached_ = true;
            }
    } else {
        stopPlayback();
    }
}

void CapturePanel::snapshot() {
    // Taken when the panel opens and held still while choosing; capture carries on beneath.
    stopPlayback();
    const auto width = std::max(1, lanesArea().getWidth());
    meter_ = engine_.recentCaptureMeter(width, Config::getInstance().getCaptureBars(), true);
    selection_ = justPlayed();
    status_ = {};
    fitWindow();
    refresh();
}

CapturePanel::Range CapturePanel::window() const {
    return {meter_.windowEndFrame - meter_.windowFrames, meter_.windowEndFrame};
}

std::optional<CapturePanel::Range> CapturePanel::justPlayed() const {
    if (!meter_.available)
        return std::nullopt;
    const auto start = window().first;
    const auto frames = static_cast<double>(meter_.windowFrames);
    return Range{start + static_cast<std::int64_t>(std::llround(meter_.passFrom * frames)),
                 start + static_cast<std::int64_t>(std::llround(meter_.passTo * frames))};
}

void CapturePanel::fetchPeaks() {
    viewPeaks_ = meter_.enabled ? engine_.recentPeaks(view_.first, view_.end,
                                                      std::max(1, lanesArea().getWidth()))
                                : std::vector<float>{};
}

void CapturePanel::refresh() {
    const auto on = meter_.enabled;
    const auto selected = selection_.has_value() && selection_->length() > 0;
    const auto start = window().first;
    const auto frames = static_cast<double>(meter_.windowFrames);
    const auto anyNotes = selected && std::ranges::any_of(meter_.notes, [&](const auto& note) {
                              return start + note.to * frames > selection_->first &&
                                     start + note.from * frames < selection_->end;
                          });
    // Always shown; dimmed while there is nothing for them to act on.
    const auto anyAudio =
        on && std::ranges::any_of(meter_.peaks, [](float peak) { return peak > 0.001f; });
    playButton_->setEnabled(anyAudio || transport_.isPlaying());
    keepAudioButton_->setEnabled(anyAudio && selected);
    keepMidiButton_->setEnabled(anyNotes);
    playButton_->setActive(transport_.isPlaying());
    playButton_->setTooltip(transport_.isPlaying() ? "Stop"
                                                   : "Hear the selection, or what is in view");
    repaint();
}

void CapturePanel::keep(bool audio, bool midi) {
    if (!selection_)
        return;
    stopPlayback();
    const auto result = engine_.importRecent(selection_->first, selection_->end, audio, midi);
    status_ = !result.success ? result.error
              : audio         ? juce::String("Imported on a new Capture Audio track")
                              : juce::String("Imported on a new Capture MIDI track");
    refresh();
}

void CapturePanel::togglePlayback() {
    if (transport_.isPlaying()) {
        stopPlayback();
        refresh();
        return;
    }

    const auto range = selection_.value_or(view_);
    if (range.length() <= 0 || !engine_.readRecent(range.first, range.length(), playBuffer_)) {
        status_ = "That audio has already left the capture buffer";
        refresh();
        return;
    }
    transport_.setSource(nullptr);
    playSource_ = std::make_unique<juce::MemoryAudioSource>(playBuffer_, false, false);
    transport_.setSource(playSource_.get(), 0, nullptr, meter_.sampleRate);
    playFrom_ = range.first;
    transport_.start();
    startTimer(kPlayheadMs);
    refresh();
}

void CapturePanel::stopPlayback() {
    stopTimer();
    transport_.stop();
}

void CapturePanel::timerCallback() {
    if (!transport_.isPlaying() || transport_.hasStreamFinished()) {
        stopPlayback();
        refresh();
        return;
    }
    repaint(lanesArea());
}

double CapturePanel::windowSeconds() const {
    return meter_.sampleRate > 0.0 ? static_cast<double>(meter_.windowFrames) / meter_.sampleRate
                                   : 0.0;
}

double CapturePanel::contentWidth() const {
    return windowSeconds() * pixelsPerSecond_;
}

void CapturePanel::fitWindow() {
    fitted_ = true;
    if (windowSeconds() > 0.0)
        pixelsPerSecond_ = std::max(1, lanesArea().getWidth()) / windowSeconds();
    scrollX_ = 0.0;
    syncView();
}

void CapturePanel::setZoom(double pixelsPerSecond, int anchorX) {
    const auto width = std::max(1, lanesArea().getWidth());
    const auto fit = width / std::max(1.0e-9, windowSeconds());
    const auto closest = std::max(fit, meter_.sampleRate / kSamplesPerPixelAtMost);
    const auto anchorTime = (anchorX + scrollX_) / pixelsPerSecond_;
    pixelsPerSecond_ = std::clamp(pixelsPerSecond, fit, closest);
    fitted_ = pixelsPerSecond_ <= fit;
    scrollX_ = anchorTime * pixelsPerSecond_ - anchorX;
    syncView();
}

void CapturePanel::setScroll(double pixels) {
    scrollX_ = pixels;
    syncView();
}

void CapturePanel::syncView() {
    const auto width = static_cast<double>(std::max(1, lanesArea().getWidth()));
    const auto content = std::max(width, contentWidth());
    scrollX_ = std::clamp(scrollX_, 0.0, content - width);

    const auto rate = std::max(1.0, meter_.sampleRate);
    const auto start = window().first;
    view_ = {start + static_cast<std::int64_t>(std::llround(scrollX_ / pixelsPerSecond_ * rate)),
             start + static_cast<std::int64_t>(
                         std::llround((scrollX_ + width) / pixelsPerSecond_ * rate))};
    view_.end = std::min(view_.end, window().end);

    ruler_.setTimelineLength(windowSeconds());
    ruler_.setZoom(pixelsPerSecond_);
    ruler_.setScrollOffset(static_cast<int>(std::round(scrollX_)));
    scrollBar_.setVisibleRange(scrollX_ / content, (scrollX_ + width) / content);
    fetchPeaks();
    repaint();
}

std::int64_t CapturePanel::frameAtX(int x) const {
    const auto lanes = lanesArea();
    const auto seconds = std::max(0.0, (x - lanes.getX() + scrollX_) / pixelsPerSecond_);
    return std::min(window().end,
                    window().first + static_cast<std::int64_t>(
                                         std::llround(seconds * std::max(1.0, meter_.sampleRate))));
}

float CapturePanel::xAtFrame(std::int64_t frame) const {
    const auto seconds =
        static_cast<double>(frame - window().first) / std::max(1.0, meter_.sampleRate);
    return static_cast<float>(lanesArea().getX() + seconds * pixelsPerSecond_ - scrollX_);
}

juce::Rectangle<int> CapturePanel::lanesArea() const {
    auto bounds = getLocalBounds().reduced(8, 8);
    bounds.removeFromLeft(kSideWidth + 8);
    bounds.removeFromRight(kIconSize + 8);
    bounds = bounds.reduced(6, 4);
    bounds.removeFromTop(kRulerHeight);
    bounds.removeFromBottom(kScrollBarHeight + 2);
    return bounds;
}

// Time zero sits kRulerPadding into the ruler, which is where the lanes start.
juce::Rectangle<int> CapturePanel::rulerArea() const {
    const auto lanes = lanesArea();
    return {lanes.getX() - kRulerPadding, lanes.getY() - kRulerHeight,
            lanes.getWidth() + kRulerPadding + kRulerOverhang, kRulerHeight};
}

juce::Rectangle<int> CapturePanel::scrollBarArea() const {
    const auto lanes = lanesArea();
    return {lanes.getX(), lanes.getBottom() + 2, lanes.getWidth(), kScrollBarHeight};
}

void CapturePanel::resized() {
    // Stacked on the right, centred down the column.
    auto icons = getLocalBounds().reduced(8, 8).removeFromRight(kIconSize);
    icons = icons.withSizeKeepingCentre(kIconSize, 3 * kIconSize + 2 * kIconGap);
    for (auto* button : {playButton_.get(), keepAudioButton_.get(), keepMidiButton_.get()}) {
        button->setBounds(icons.removeFromTop(kIconSize));
        icons.removeFromTop(kIconGap);
    }
    ruler_.setBounds(rulerArea());
    scrollBar_.setBounds(scrollBarArea());
    // Opened before it was laid out, or resized while fitted: fit what it can now see.
    if (fitted_)
        fitWindow();
    else
        syncView();
}

void CapturePanel::mouseDown(const juce::MouseEvent& e) {
    if (!meter_.enabled || !lanesArea().contains(e.getPosition()))
        return;
    dragAnchor_ = frameAtX(e.x);
    // A click without a drag clears the selection, as on the timeline.
    selection_.reset();
    refresh();
}

void CapturePanel::mouseDrag(const juce::MouseEvent& e) {
    if (!meter_.enabled || !lanesArea().contains(e.getMouseDownPosition()))
        return;
    const auto here = frameAtX(e.x);
    selection_ = Range{std::min(dragAnchor_, here), std::max(dragAnchor_, here)};
    refresh();
}

void CapturePanel::mouseDoubleClick(const juce::MouseEvent& e) {
    if (!lanesArea().contains(e.getPosition()))
        return;
    selection_ = justPlayed();
    refresh();
}

void CapturePanel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    // The arrangement's bindings, so the same gesture does the same thing here.
    const auto gesture = GestureRouter::getInstance().resolve(GestureContext::Arrangement, wheel,
                                                              e.mods, e.getPosition());
    if (gesture.type == GestureActionType::ZoomHorizontal)
        setZoom(pixelsPerSecond_ * std::pow(2.0, gesture.magnitude),
                gesture.anchor.x - lanesArea().getX());
    else if (gesture.type == GestureActionType::ScrollHorizontal) {
        fitted_ = false;
        setScroll(scrollX_ - gesture.magnitude);
    } else
        Component::mouseWheelMove(e, wheel);
}

void CapturePanel::paint(juce::Graphics& g) {
    g.fillAll(ActiveTheme::getColour(ActiveTheme::TRANSPORT_BACKGROUND));
    auto& fonts = FontManager::getInstance();
    const auto dim = ActiveTheme::getColour(ActiveTheme::TRANSPORT_TEXT_DIM);

    auto side = getLocalBounds().reduced(8, 8).removeFromLeft(kSideWidth);
    g.setFont(fonts.getUIFont(10.0f));
    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    g.drawText("RECENT", side.removeFromTop(18), juce::Justification::centredLeft, false);
    side.removeFromTop(4);
    g.setColour(dim);
    juce::String text = "Capture is off: click MEM to turn it on";
    if (status_.isNotEmpty())
        text = status_;
    else if (meter_.enabled && selection_ && selection_->length() > 0)
        text = clock(static_cast<double>(selection_->length()) / std::max(1.0, meter_.sampleRate)) +
               " selected";
    else if (meter_.enabled)
        text = "Drag to choose, double-click for what you just played";
    g.drawFittedText(text, side, juce::Justification::topLeft, 4);

    auto well = rulerArea().withBottom(scrollBarArea().getBottom()).expanded(4, 4);
    g.setColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_WELL));
    g.fillRoundedRectangle(well.toFloat(), 4.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::TRANSPORT_WELL_BORDER));
    g.drawRoundedRectangle(well.toFloat().reduced(0.5f), 4.0f, 1.0f);

    if (meter_.enabled)
        paintLanes(g);
}

void CapturePanel::paintLanes(juce::Graphics& g) const {
    auto lanes = lanesArea();
    const auto amber = ActiveTheme::getColour(ActiveTheme::TRANSPORT_MEMORY);
    const auto dim = ActiveTheme::getColour(ActiveTheme::TRANSPORT_TEXT_DIM);
    const auto rate = std::max(1.0, meter_.sampleRate);
    const auto audioLane = lanes.removeFromTop(lanes.getHeight() / 2).reduced(0, 2);
    const auto midiLane = lanes.reduced(0, 2);
    const auto top = static_cast<float>(audioLane.getY());
    const auto height = static_cast<float>(midiLane.getBottom() - audioLane.getY());

    juce::Graphics::ScopedSaveState clipToLanes(g);
    g.reduceClipRegion(audioLane.getUnion(midiLane));

    if (selection_ && selection_->length() > 0) {
        const auto from = xAtFrame(selection_->first);
        g.setColour(amber.withAlpha(0.14f));
        g.fillRect(from, top, std::max(1.0f, xAtFrame(selection_->end) - from), height);
    }

    // Audio: one bar per column over -48..0 dB, centred.
    const auto centre = static_cast<float>(audioLane.getCentreY());
    const auto half = static_cast<float>(audioLane.getHeight()) / 2.0f;
    const auto x0 = static_cast<float>(audioLane.getX());
    for (std::size_t i = 0; i < viewPeaks_.size(); ++i) {
        const auto level = std::clamp(
            (juce::Decibels::gainToDecibels(viewPeaks_[i], -48.0f) + 48.0f) / 48.0f, 0.0f, 1.0f);
        if (level <= 0.0f)
            continue;
        const auto frame =
            view_.first + static_cast<std::int64_t>(view_.length() * static_cast<double>(i) /
                                                    static_cast<double>(viewPeaks_.size()));
        const auto selected = selection_ && frame >= selection_->first && frame < selection_->end;
        g.setColour(selected ? amber : amber.withAlpha(0.4f));
        g.fillRect(x0 + static_cast<float>(i), centre - level * half, 1.0f, level * half * 2.0f);
    }

    // MIDI: notes over the range they span.
    if (!meter_.notes.empty()) {
        auto low = 127;
        auto high = 0;
        for (const auto& note : meter_.notes) {
            low = std::min(low, note.note);
            high = std::max(high, note.note);
        }
        low = std::max(0, low - 2);
        high = std::min(127, high + 2);
        const auto rowHeight =
            static_cast<float>(midiLane.getHeight()) / static_cast<float>(high - low + 1);
        const auto start = window().first;
        const auto frames = static_cast<double>(meter_.windowFrames);
        for (const auto& note : meter_.notes) {
            const auto from = start + static_cast<std::int64_t>(note.from * frames);
            const auto to = start + static_cast<std::int64_t>(note.to * frames);
            const auto selected = selection_ && to > selection_->first && from < selection_->end;
            g.setColour(selected ? amber.withAlpha(0.5f + 0.5f * note.velocity)
                                 : amber.withAlpha(0.3f));
            const auto y = static_cast<float>(midiLane.getBottom()) -
                           static_cast<float>(note.note - low + 1) * rowHeight;
            const auto x = xAtFrame(from);
            g.fillRect(x, y, std::max(1.5f, xAtFrame(to) - x), std::max(1.5f, rowHeight - 0.5f));
        }
    } else {
        g.setColour(dim);
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText("No notes played on a MIDI device", midiLane, juce::Justification::centred,
                   false);
    }

    if (transport_.isPlaying()) {
        const auto playhead =
            playFrom_ + static_cast<std::int64_t>(transport_.getCurrentPosition() * rate);
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
        g.fillRect(xAtFrame(playhead), top, 1.5f, height);
    }
}

}  // namespace magda
