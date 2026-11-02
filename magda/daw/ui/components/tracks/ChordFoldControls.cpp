// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChordFoldControls.hpp"

#include "core/Config.hpp"
#include "core/TrackManager.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda {

namespace {

void strokeChevron(juce::Graphics& g, juce::Point<float> centre,
                   FoldChevronButton::Direction direction) {
    juce::Path chevron;
    if (direction == FoldChevronButton::Direction::Down) {
        chevron.startNewSubPath(centre.x - 4.0f, centre.y - 2.0f);
        chevron.lineTo(centre.x, centre.y + 2.0f);
        chevron.lineTo(centre.x + 4.0f, centre.y - 2.0f);
    } else {
        const float reach = direction == FoldChevronButton::Direction::Left ? -2.0f : 2.0f;
        chevron.startNewSubPath(centre.x - reach, centre.y - 4.0f);
        chevron.lineTo(centre.x + reach, centre.y);
        chevron.lineTo(centre.x - reach, centre.y + 4.0f);
    }
    g.strokePath(chevron, juce::PathStrokeType(1.5f));
}

}  // namespace

void setChordTrackFolded(TrackId trackId, bool folded) {
    juce::MessageManager::callAsync(
        [trackId, folded] { TrackManager::getInstance().setTrackCollapsed(trackId, folded); });
}

FoldChevronButton::FoldChevronButton(Direction direction)
    : juce::Button("Fold"), direction_(direction) {}

void FoldChevronButton::paintButton(juce::Graphics& g, bool highlighted, bool /*down*/) {
    // Full bar puts it on the track colour, where the name is white too.
    const auto colour = Config::getInstance().getTrackColourStyle() == "full"
                            ? juce::Colours::white
                            : ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY);
    g.setColour(highlighted ? colour : colour.withAlpha(0.7f));
    strokeChevron(g, getLocalBounds().toFloat().getCentre(), direction_);
}

FoldedChordStrip::FoldedChordStrip(TrackId trackId) : trackId_(trackId) {
    setTooltip("Unfold the chord track");
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void FoldedChordStrip::paint(juce::Graphics& g) {
    const auto* track = TrackManager::getInstance().getTrack(trackId_);
    if (track == nullptr)
        return;

    constexpr int kChevronRow = 26;
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::SESSION_STOP_ROW));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::SESSION_CONTROL_BORDER));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path outline;
        outline.addRoundedRectangle(bounds, 6.0f);
        g.reduceClipRegion(outline);
        g.setColour(deriveTrackSwatch(track->colour));
        g.fillRect(bounds.withHeight(3.0f));
    }

    g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    strokeChevron(g, {bounds.getCentreX(), kChevronRow / 2.0f + 2.0f},
                  FoldChevronButton::Direction::Right);

    // The name reads upward from under the chevron.
    const auto font = FontManager::getInstance().getUIFont(11.0f);
    const int textLength =
        juce::jmin(getHeight() - kChevronRow,
                   juce::GlyphArrangement::getStringWidthInt(font, track->name) + 12);
    g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi)
                       .translated(0.0f, static_cast<float>(kChevronRow + textLength)));
    g.setFont(font);
    g.drawText(track->name, 0, 0, textLength, getWidth(), juce::Justification::centred, true);
}

void FoldedChordStrip::mouseUp(const juce::MouseEvent& event) {
    if (event.mouseWasClicked() && !event.mods.isPopupMenu())
        setChordTrackFolded(trackId_, false);
}

}  // namespace magda
