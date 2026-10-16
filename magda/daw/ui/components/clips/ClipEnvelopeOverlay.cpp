#include "ClipEnvelopeOverlay.hpp"

#include <algorithm>

#include "core/ClipManager.hpp"
#include "ui/components/tracks/TrackContentPanel.hpp"

namespace magda {

ClipEnvelopeOverlay::ClipEnvelopeOverlay(ClipId clipId, juce::Component& clip,
                                         TrackContentPanel* panel, Hooks hooks)
    : clipId_(clipId), clipComponent_(clip), panel_(panel), hooks_(std::move(hooks)) {
    editor_ = std::make_unique<daw::ui::GainEnvelopeEditor>(
        static_cast<daw::ui::GainEnvelopeHost&>(*this));
    clipComponent_.addAndMakeVisible(*editor_);
}

ClipEnvelopeOverlay::~ClipEnvelopeOverlay() = default;

const ClipInfo* ClipEnvelopeOverlay::clip() const {
    return ClipManager::getInstance().getClip(clipId_);
}

void ClipEnvelopeOverlay::layout(juce::Rectangle<int> body) {
    const auto* info = clip();
    const auto* event = info != nullptr ? info->primaryEvent() : nullptr;
    const auto bpm = panel_ != nullptr ? panel_->getTempo() : 120.0;
    map_ =
        event != nullptr ? ClipSourceMap::of(*info, bpm, event->sourceSampleRate()) : std::nullopt;
    lengthBeats_ = info != nullptr ? info->placement.lengthBeats : 0.0;

    editor_->setBounds(body);
    editor_->setClip(clipId_);
    // White reads over any clip colour; the clip's own would vanish into its body.
    editor_->setCurveColour(juce::Colours::white);
}

double ClipEnvelopeOverlay::pixelsPerX() const {
    return lengthBeats_ > 0.0 ? clipComponent_.getWidth() / lengthBeats_ : 1.0;
}

double ClipEnvelopeOverlay::xToPixel(double x) const {
    return (x * pixelsPerX()) - editor_->getX();
}

double ClipEnvelopeOverlay::pixelToX(double px) const {
    return (px + editor_->getX()) / pixelsPerX();
}

double ClipEnvelopeOverlay::sourceAt(double x) const {
    return map_ ? map_->sourceSecondsAt(x) : 0.0;
}

std::vector<double> ClipEnvelopeOverlay::xsOf(double sourceSeconds) const {
    if (!map_)
        return {};
    const auto [from, to] = xRange();
    return map_->clipBeatsOf(sourceSeconds, from, to, std::max(64, clipComponent_.getWidth()));
}

std::pair<double, double> ClipEnvelopeOverlay::xRange() const {
    if (!map_)
        return {0.0, 0.0};
    return {std::max(0.0, map_->eventStartBeat()), std::min(lengthBeats_, map_->eventEndBeat())};
}

bool ClipEnvelopeOverlay::snapsToGrid() const {
    return panel_ != nullptr && static_cast<bool>(panel_->snapBeatsToGrid);
}

double ClipEnvelopeOverlay::snapToGrid(double x) const {
    const auto* info = clip();
    if (!snapsToGrid() || info == nullptr)
        return x;
    const auto start = info->placement.startBeat;
    return panel_->snapBeatsToGrid(start + x) - start;
}

double ClipEnvelopeOverlay::gridSpacing() const {
    return panel_ != nullptr && panel_->getGridSpacingBeats ? panel_->getGridSpacingBeats() : 0.0;
}

bool ClipEnvelopeOverlay::keepsPress(int x, int y) const {
    return hooks_.clipKeepsPress &&
           hooks_.clipKeepsPress({x + editor_->getX(), y + editor_->getY()});
}

void ClipEnvelopeOverlay::envelopePressed() {
    if (hooks_.pressed)
        hooks_.pressed();
}

bool ClipEnvelopeOverlay::showContextMenu() {
    if (!hooks_.showContextMenu)
        return false;
    hooks_.showContextMenu();
    return true;
}

}  // namespace magda
