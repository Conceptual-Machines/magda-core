#include "ChordProgressionSection.hpp"

#include <algorithm>
#include <vector>

#include "core/TempoUtils.hpp"
#include "core/TrackManager.hpp"
#include "music/NotationSettings.hpp"
#include "ui/state/TimelineController.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

ChordProgressionSection::ChordProgressionSection() {
    magda::music::NotationSettings::getInstance().addChangeListener(this);
}

ChordProgressionSection::~ChordProgressionSection() {
    magda::music::NotationSettings::getInstance().removeChangeListener(this);
}

void ChordProgressionSection::setClip(magda::ClipId clipId) {
    if (clipId_ != clipId) {
        clipId_ = clipId;
        repaint();
    }
}

void ChordProgressionSection::setSelectedClips(const std::unordered_set<magda::ClipId>& clipIds) {
    setClip(clipIds.size() == 1 ? *clipIds.begin() : magda::INVALID_CLIP_ID);
}

bool ChordProgressionSection::isChordClip() const {
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    if (clip == nullptr)
        return false;
    const auto* track = magda::TrackManager::getInstance().getTrack(clip->trackId);
    return track != nullptr && track->type == magda::TrackType::Chord;
}

int ChordProgressionSection::getPreferredHeight() const {
    if (!isChordClip())
        return 0;
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    if (clip == nullptr)
        return 0;
    // Always show the header (even with no chords yet, as a hint).
    const int rows = std::max(1, static_cast<int>(clip->chordAnnotations.size()));
    return HEADER_H + rows * ROW_H + 4;
}

void ChordProgressionSection::paint(juce::Graphics& g) {
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    if (clip == nullptr)
        return;

    auto area = getLocalBounds();

    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.setFont(FontManager::getInstance().getUIFontMedium(11.0f));
    g.drawText("PROGRESSION", area.removeFromTop(HEADER_H), juce::Justification::centredLeft);

    if (clip->chordAnnotations.empty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.6f));
        g.setFont(FontManager::getInstance().getUIFont(11.0f));
        g.drawText("No chords yet - click the chord lane to add one.", area.removeFromTop(ROW_H),
                   juce::Justification::centredLeft);
        return;
    }

    int beatsPerBar = magda::DEFAULT_TIME_SIGNATURE_NUMERATOR;
    if (auto* controller = magda::TimelineController::getCurrent())
        beatsPerBar = controller->getState().tempo.timeSignatureNumerator;
    const double bar = std::max(1, beatsPerBar);

    // Display in time order.
    std::vector<ClipInfo::ChordAnnotation> chords(clip->chordAnnotations.begin(),
                                                  clip->chordAnnotations.end());
    std::sort(chords.begin(), chords.end(),
              [](const auto& a, const auto& b) { return a.beatPosition < b.beatPosition; });

    auto& notation = magda::music::NotationSettings::getInstance();
    for (const auto& c : chords) {
        auto row = area.removeFromTop(ROW_H);
        if (row.getHeight() <= 0)
            break;

        const int barNum = static_cast<int>(c.beatPosition / bar) + 1;
        const double beat = std::fmod(c.beatPosition, bar) + 1.0;

        // Bar.beat marker
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        g.setFont(FontManager::getInstance().getMonoFont(11.0f));
        g.drawText(juce::String(barNum) + "." + juce::String(static_cast<int>(beat)),
                   row.removeFromLeft(40), juce::Justification::centredLeft);

        // Length (bars or beats)
        g.drawText(juce::String(c.lengthBeats / bar, 1) + " bar", row.removeFromRight(56),
                   juce::Justification::centredRight);

        // Chord name
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        g.setFont(FontManager::getInstance().getUIFontMedium(12.0f));
        g.drawText(notation.format(c.chordName), row, juce::Justification::centredLeft, true);
    }
}

}  // namespace magda::daw::ui
