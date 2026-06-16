#include "MidiTakeLanesComponent.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "VelocityLaneUtils.hpp"
#include "core/ClipManager.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kSwipeThreshold = 3;
constexpr int kVMargin = 3;
}  // namespace

MidiTakeLanesComponent::MidiTakeLanesComponent() {
    foldMap_.setEnabled(true);
    // Match the audio take lanes: an I-beam signals the swipe-to-comp gesture.
    setMouseCursor(juce::MouseCursor::IBeamCursor);
}

void MidiTakeLanesComponent::setClip(magda::ClipId clipId) {
    clipId_ = clipId;
    refresh();
}

void MidiTakeLanesComponent::refresh() {
    if (const auto* clip = magda::ClipManager::getInstance().getClip(clipId_))
        clipStartBeats_ = clip->placement.startBeat;
    rebuildFoldMap();
    repaint();
}

void MidiTakeLanesComponent::setPixelsPerBeat(double ppb) {
    if (pixelsPerBeat_ != ppb) {
        pixelsPerBeat_ = ppb;
        repaint();
    }
}

void MidiTakeLanesComponent::setScrollOffset(int offsetX) {
    if (scrollOffsetX_ != offsetX) {
        scrollOffsetX_ = offsetX;
        repaint();
    }
}

void MidiTakeLanesComponent::setLeftPadding(int padding) {
    if (leftPadding_ != padding) {
        leftPadding_ = padding;
        repaint();
    }
}

void MidiTakeLanesComponent::setRelativeMode(bool relative) {
    if (relativeMode_ != relative) {
        relativeMode_ = relative;
        repaint();
    }
}

int MidiTakeLanesComponent::beatToPixel(double beat) const {
    return velocity_lane::beatToPixel(beat, pixelsPerBeat_, leftPadding_ + labelGutter_,
                                      scrollOffsetX_);
}

double MidiTakeLanesComponent::pixelToBeat(int x) const {
    return velocity_lane::pixelToBeat(x, pixelsPerBeat_, leftPadding_ + labelGutter_,
                                      scrollOffsetX_);
}

void MidiTakeLanesComponent::setLabelGutter(int gutter) {
    if (labelGutter_ != gutter) {
        labelGutter_ = gutter;
        repaint();
    }
}

double MidiTakeLanesComponent::displayBeat(double noteBeat) const {
    return (relativeMode_ ? 0.0 : clipStartBeats_) + noteBeat;
}

void MidiTakeLanesComponent::rebuildFoldMap() {
    std::set<int> pitches;
    if (const auto* clip = magda::ClipManager::getInstance().getClip(clipId_)) {
        if (clip->isMidi())
            for (const auto& take : clip->midi().takes)
                for (const auto& n : take.notes)
                    pitches.insert(n.noteNumber);
    }
    foldMap_.rebuild(std::vector<int>(pitches.begin(), pitches.end()));
}

int MidiTakeLanesComponent::laneCount() const {
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    if (clip == nullptr || !clip->isMidi())
        return 0;
    const int n = static_cast<int>(clip->midi().takes.size());
    return n >= 2 ? n : 0;
}

int MidiTakeLanesComponent::preferredHeight() const {
    return laneCount() * LANE_HEIGHT;
}

int MidiTakeLanesComponent::laneAtY(int y) const {
    const int n = laneCount();
    if (n <= 0 || getHeight() <= 0)
        return -1;
    const int laneH = getHeight() / n;
    if (laneH <= 0)
        return -1;
    return std::clamp(y / laneH, 0, n - 1);
}

void MidiTakeLanesComponent::paint(juce::Graphics& g) {
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    const int n = laneCount();
    if (clip == nullptr || n <= 0) {
        g.fillAll(DarkTheme::getPanelBackgroundColour());
        return;
    }

    const auto& midi = clip->midi();
    const juce::Colour base = clip->colour;
    const int laneH = getHeight() / n;
    const int rows = foldMap_.rowCount();
    const int activeTake = midi.currentTakeIndex;
    const bool comping = midi.compActive;

    g.setFont(FontManager::getInstance().getUIFont(10.0f));

    // Neutral lane backgrounds (greys), so the clip colour is reserved for the
    // notes themselves — keeps the strip from reading as a wall of blue.
    const juce::Colour activeBg(0xFF333333);
    const juce::Colour inactiveBg(0xFF262626);

    for (int i = 0; i < n; ++i) {
        const int laneY = i * laneH;
        const bool isActive = (i == activeTake) && !comping;

        juce::Rectangle<int> laneRect(0, laneY, getWidth(), laneH);
        g.setColour(isActive ? activeBg : inactiveBg);
        g.fillRect(laneRect);

        // Lane content (notes + comp bands) lives right of the label gutter, so
        // it stays clear of the take name and aligned with the grid columns.
        const juce::Rectangle<int> contentRect(labelGutter_, laneY, getWidth() - labelGutter_,
                                               laneH);
        {
            juce::Graphics::ScopedSaveState ss(g);
            g.reduceClipRegion(contentRect);

            // Note blocks (mini-roll on the shared folded axis). Clip colour, with
            // inactive lanes dimmed so the active take reads clearly. The active,
            // non-comping lane draws the clip's live notes so piano-roll edits show
            // immediately; other lanes draw their recorded take snapshot.
            const int usableH = laneH - 2 * kVMargin;
            const int rowH = std::max(1, rows > 0 ? usableH / rows : usableH);
            const float laneDim = isActive ? 1.0f : 0.5f;
            const std::vector<MidiNote>& laneNotes =
                isActive ? clip->midiNotes : midi.takes[static_cast<size_t>(i)].notes;
            for (const auto& note : laneNotes) {
                const int x = beatToPixel(displayBeat(note.startBeat));
                const int w = std::max(2, static_cast<int>(note.lengthBeats * pixelsPerBeat_));
                if (x + w < labelGutter_ || x > getWidth())
                    continue;
                const int row = foldMap_.rowForNote(note.noteNumber);
                const int y = laneY + kVMargin + row * rowH;
                const float alpha =
                    (0.45f + 0.55f * (static_cast<float>(note.velocity) / 127.0f)) * laneDim;
                g.setColour(base.withAlpha(alpha));
                g.fillRect(juce::Rectangle<int>(x, y, w, rowH).reduced(0, rowH > 2 ? 1 : 0));
            }

            // Comp section bands owned by this take — neutral wash, clip-colour
            // edge marks ownership without flooding the lane.
            if (comping) {
                for (const auto& sec : midi.comp) {
                    if (sec.takeIndex != i)
                        continue;
                    const int sx0 = beatToPixel(displayBeat(sec.startBeat));
                    const int sx1 = beatToPixel(displayBeat(sec.endBeat));
                    juce::Rectangle<int> band(sx0, laneY, std::max(1, sx1 - sx0), laneH);
                    g.setColour(juce::Colours::white.withAlpha(0.05f));
                    g.fillRect(band);
                    g.setColour(base.withAlpha(0.7f));
                    g.drawRect(band, 1);
                }
            }
        }

        // Active-take accent bar (in the gutter).
        if (isActive) {
            g.setColour(base);
            g.fillRect(0, laneY, 2, laneH);
        }

        // Take name in the fixed left gutter.
        g.setColour(juce::Colours::white.withAlpha(isActive ? 0.85f : 0.5f));
        g.drawText("Take " + juce::String(i + 1), 6, laneY, juce::jmax(0, labelGutter_ - 8), laneH,
                   juce::Justification::centredLeft, false);

        // Gutter divider + separator.
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        if (labelGutter_ > 0)
            g.drawVerticalLine(labelGutter_, static_cast<float>(laneY),
                               static_cast<float>(laneY + laneH));
        g.drawHorizontalLine(laneY + laneH - 1, 0.0f, static_cast<float>(getWidth()));
    }

    // Live swipe band.
    if (swiping_ && swipeLane_ >= 0) {
        const int laneY = swipeLane_ * laneH;
        const int sx0 = std::min(swipeStartX_, swipeCurrentX_);
        const int sx1 = std::max(swipeStartX_, swipeCurrentX_);
        juce::Rectangle<int> band(sx0, laneY, std::max(1, sx1 - sx0), laneH);
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillRect(band);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.drawRect(band, 1);
    }
}

void MidiTakeLanesComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        const int lane = laneAtY(e.y);
        juce::PopupMenu menu;
        const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
        const bool comping = clip != nullptr && clip->isMidi() && clip->midi().compActive;
        if (lane >= 0)
            menu.addItem(2, "Delete Take " + juce::String(lane + 1), laneCount() > 1);
        menu.addItem(1, "Clear comp", comping);
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, lane](int r) {
            if (r == 1 && onCompClear)
                onCompClear();
            else if (r == 2 && lane >= 0 && onDeleteTake)
                onDeleteTake(lane);
        });
        return;
    }
    swipeLane_ = laneAtY(e.y);
    swipeStartX_ = e.x;
    swipeCurrentX_ = e.x;
    swiping_ = false;
}

void MidiTakeLanesComponent::mouseDrag(const juce::MouseEvent& e) {
    if (swipeLane_ < 0)
        return;
    swipeCurrentX_ = e.x;
    if (std::abs(e.x - swipeStartX_) > kSwipeThreshold)
        swiping_ = true;
    repaint();
}

void MidiTakeLanesComponent::mouseUp(const juce::MouseEvent& e) {
    juce::ignoreUnused(e);
    if (swipeLane_ < 0)
        return;
    const int lane = swipeLane_;
    const bool swiped = swiping_;
    const int x0 = swipeStartX_;
    const int x1 = swipeCurrentX_;
    swipeLane_ = -1;
    swiping_ = false;

    const double origin = relativeMode_ ? 0.0 : clipStartBeats_;
    if (swiped && onCompSectionSet) {
        double a = pixelToBeat(std::min(x0, x1)) - origin;
        double b = pixelToBeat(std::max(x0, x1)) - origin;
        if (snapEnabled_ && gridResolutionBeats_ > 0.0) {
            a = std::round(a / gridResolutionBeats_) * gridResolutionBeats_;
            b = std::round(b / gridResolutionBeats_) * gridResolutionBeats_;
        }
        if (b > a)
            onCompSectionSet(a, b, lane);
    } else if (!swiped && onTakeSelected) {
        onTakeSelected(lane);
    }
    repaint();
}

void MidiTakeLanesComponent::mouseMove(const juce::MouseEvent& e) {
    const int lane = laneAtY(e.y);
    if (lane != hoverLane_) {
        hoverLane_ = lane;
        if (onTakeHovered)
            onTakeHovered(lane);
    }
}

void MidiTakeLanesComponent::mouseExit(const juce::MouseEvent&) {
    if (hoverLane_ != -1) {
        hoverLane_ = -1;
        if (onTakeHovered)
            onTakeHovered(-1);
    }
}

}  // namespace magda::daw::ui
