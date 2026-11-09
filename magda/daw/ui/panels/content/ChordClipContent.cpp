// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChordClipContent.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <vector>

#include "../../state/TimelineController.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/TempoUtils.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "music/ChordEngine.hpp"
#include "music/ChordEnums.hpp"
#include "music/NotationSettings.hpp"
#include "ui/components/chord/ChordWheel.hpp"
#include "ui/components/common/InternalFileDrag.hpp"
#include "ui/themes/ActiveTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/InspectorComboBoxLookAndFeel.hpp"
#include "ui/utils/AudioFileTypes.hpp"

namespace magda::daw::ui {

namespace {

using magda::music::ChordQuality;
using magda::music::ChordRoot;

// The editor splits the flat ChordQuality into a Base type and a Base-dependent
// Extension. This table lists every quality grouped under its base so the
// Extension dropdown can offer all of them.
struct ExtOption {
    const char* label;
    ChordQuality quality;
};

const char* const kBaseLabels[] = {"Major", "Minor", "Dim",       "Aug",
                                   "Sus2",  "Sus4",  "5 (Power)", "Dom"};

const std::vector<std::vector<ExtOption>>& baseExtensions() {
    using Q = ChordQuality;
    static const std::vector<std::vector<ExtOption>> table = {
        // Major
        {{"None", Q::Major},
         {"add2", Q::MajorAdd2},
         {"add4", Q::MajorAdd4},
         {"6", Q::MajorAdd6},
         {"7", Q::Major7},
         {"9", Q::Major9},
         {"add9", Q::MajorAdd9},
         {"maj7 add13", Q::Major7Add13}},
        // Minor
        {{"None", Q::Minor},
         {"add2", Q::MinorAdd2},
         {"add4", Q::MinorAdd4},
         {"add9", Q::MinorAdd9},
         {"7", Q::Minor7},
         {"9", Q::Minor9},
         {"11", Q::Minor11},
         {"13", Q::Minor13},
         {"7 add2", Q::Minor7Add2},
         {"7 add4", Q::Minor7Add4},
         {"7 add6", Q::Minor7Add6},
         {"7 add2/6", Q::Minor7Add2Add6},
         {"7 add2/4", Q::Minor7Add2Add4},
         {"7 add4/6", Q::Minor7Add4Add6},
         {"7 add2/4/6", Q::Minor7Add2Add4Add6}},
        // Diminished
        {{"None", Q::Diminished}, {"7", Q::Diminished7}, {"9", Q::Diminished9}},
        // Augmented
        {{"None", Q::Augmented}},
        // Sus2
        {{"None", Q::Sus2}, {"6", Q::Sus2Add6}},
        // Sus4
        {{"None", Q::Sus4}, {"6", Q::Sus4Add6}},
        // Power
        {{"None", Q::Power}},
        // Dominant
        {{"7", Q::Dominant7}, {"9", Q::Dominant9}, {"11", Q::Dominant11}, {"13", Q::Dominant13}},
    };
    return table;
}

ChordQuality qualityFromParts(int base, int ext) {
    const auto& t = baseExtensions();
    if (base < 0 || base >= static_cast<int>(t.size()) || t[static_cast<size_t>(base)].empty())
        return ChordQuality::Major;
    const auto& exts = t[static_cast<size_t>(base)];
    ext = juce::jlimit(0, static_cast<int>(exts.size()) - 1, ext);
    return exts[static_cast<size_t>(ext)].quality;
}

std::pair<int, int> partsFromQuality(ChordQuality q) {
    const auto& t = baseExtensions();
    for (int b = 0; b < static_cast<int>(t.size()); ++b)
        for (int e = 0; e < static_cast<int>(t[static_cast<size_t>(b)].size()); ++e)
            if (t[static_cast<size_t>(b)][static_cast<size_t>(e)].quality == q)
                return {b, e};
    return {0, 0};
}

constexpr int kMaxWheelSize = 320;
constexpr int kWheelGap = 8;

/** @brief The current time signature's bar, in beats. */
double beatsPerBar() {
    if (auto* controller = magda::TimelineController::getCurrent())
        return controller->getState().tempo.beatsPerBar();
    return 4.0;
}

/** @brief A built chord's notes as MIDI pitches. */
std::vector<int> pitchesOf(const magda::music::Chord& chord) {
    std::vector<int> pitches;
    pitches.reserve(chord.notes.size());
    for (const auto& note : chord.notes)
        pitches.push_back(note.noteNumber);
    return pitches;
}

/** @brief A predicate matching the notes linked to a chord group; group 0 links none. */
auto isInChordGroup(int group) {
    return [group](const magda::MidiNote& note) { return group != 0 && note.chordGroup == group; };
}

/** @brief A chord group's note indices, highest first so deleting keeps them valid. */
std::vector<size_t> noteIndicesInChordGroup(const magda::ClipInfo& clip, int group) {
    const auto isInGroup = [&clip, group](size_t i) {
        return isInChordGroup(group)(clip.midiNotes[i]);
    };
    std::vector<size_t> indices;
    std::ranges::copy(std::views::iota(size_t{0}, clip.midiNotes.size()) |
                          std::views::filter(isInGroup),
                      std::back_inserter(indices));
    std::ranges::reverse(indices);
    return indices;
}

/// Root / base-quality / extension / octave / inversion editor (CallOutBox).
class ChordEditorPopup : public juce::Component {
  public:
    std::function<void(ChordRoot, ChordQuality, int octave, int inversion)> onChange;

    ChordEditorPopup(ChordRoot root, ChordQuality quality, int octave, int inversion) {
        const auto [base, ext] = partsFromQuality(quality);

        auto style = [this](juce::ComboBox& c) {
            c.setLookAndFeel(&laf_);
            c.setColour(juce::ComboBox::backgroundColourId,
                        ActiveTheme::getColour(ActiveTheme::SURFACE));
            c.setColour(juce::ComboBox::textColourId,
                        ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
            c.setColour(juce::ComboBox::outlineColourId,
                        ActiveTheme::getColour(ActiveTheme::BORDER));
            addAndMakeVisible(c);
        };

        for (int i = 0; i < 12; ++i)
            rootCombo_.addItem(magda::music::ChordUtils::rootToString(static_cast<ChordRoot>(i)),
                               i + 1);
        rootCombo_.setSelectedId(static_cast<int>(root) + 1, juce::dontSendNotification);
        rootCombo_.onChange = [this] { fireChange(); };

        for (int i = 0; i < 8; ++i)
            baseCombo_.addItem(kBaseLabels[i], i + 1);
        baseCombo_.setSelectedId(base + 1, juce::dontSendNotification);
        baseCombo_.onChange = [this] {
            populateExt(baseCombo_.getSelectedId() - 1, 0);
            fireChange();
        };

        populateExt(base, ext);
        extCombo_.onChange = [this] { fireChange(); };

        for (int o = 0; o <= 8; ++o)
            octaveCombo_.addItem("Octave " + juce::String(o), o + 1);
        octaveCombo_.setSelectedId(juce::jlimit(0, 8, octave) + 1, juce::dontSendNotification);
        octaveCombo_.onChange = [this] { fireChange(); };

        for (int inv = 0; inv <= 3; ++inv)
            inversionCombo_.addItem(inv == 0 ? "Root position" : ("Inversion " + juce::String(inv)),
                                    inv + 1);
        inversionCombo_.setSelectedId(juce::jlimit(0, 3, inversion) + 1,
                                      juce::dontSendNotification);
        inversionCombo_.onChange = [this] { fireChange(); };

        style(rootCombo_);
        style(baseCombo_);
        style(extCombo_);
        style(octaveCombo_);
        style(inversionCombo_);

        setSize(200, 5 * 30 + 8);
    }

    ~ChordEditorPopup() override {
        for (auto* c : {&rootCombo_, &baseCombo_, &extCombo_, &octaveCombo_, &inversionCombo_})
            c->setLookAndFeel(nullptr);
    }

    void resized() override {
        auto b = getLocalBounds().reduced(4);
        for (auto* c : {&rootCombo_, &baseCombo_, &extCombo_, &octaveCombo_, &inversionCombo_}) {
            c->setBounds(b.removeFromTop(28));
            b.removeFromTop(2);
        }
    }

  private:
    void populateExt(int base, int selectExt) {
        extCombo_.clear(juce::dontSendNotification);
        const auto& t = baseExtensions();
        if (base < 0 || base >= static_cast<int>(t.size()))
            return;
        const auto& exts = t[static_cast<size_t>(base)];
        for (int i = 0; i < static_cast<int>(exts.size()); ++i)
            extCombo_.addItem(exts[static_cast<size_t>(i)].label, i + 1);
        extCombo_.setSelectedId(juce::jlimit(0, static_cast<int>(exts.size()) - 1, selectExt) + 1,
                                juce::dontSendNotification);
    }

    void fireChange() {
        if (onChange)
            onChange(
                static_cast<ChordRoot>(rootCombo_.getSelectedId() - 1),
                qualityFromParts(baseCombo_.getSelectedId() - 1, extCombo_.getSelectedId() - 1),
                octaveCombo_.getSelectedId() - 1, inversionCombo_.getSelectedId() - 1);
    }

    InspectorComboBoxLookAndFeel laf_;
    juce::ComboBox rootCombo_, baseCombo_, extCombo_, octaveCombo_, inversionCombo_;
};

}  // namespace

ChordClipContent::ChordClipContent() {
    setWantsKeyboardFocus(true);
}

ChordClipContent::~ChordClipContent() = default;

int ChordClipContent::maxLaneHeight() const {
    return std::max(MIN_LANE_HEIGHT, getHeight() - RULER_HEIGHT);
}

void ChordClipContent::onGridToggleClicked() {
    const int maxH = maxLaneHeight();
    if (laneHeight_ > maxH - 2) {
        // Grid currently hidden - restore it.
        laneHeight_ = juce::jlimit(MIN_LANE_HEIGHT, maxH, expandedLaneHeight_);
    } else {
        // Hide the grid only: the lane grows to cover the note grid but stops
        // above the time ruler, so the timeline stays visible.
        expandedLaneHeight_ = juce::jmin(laneHeight_, maxH);
        laneHeight_ = maxH;
    }
    setGridToggleActive(laneHeight_ <= maxH - 2);  // active = grid visible
    resized();
    repaint();
}

bool ChordClipContent::isOnLaneDivider(juce::Point<int> p) const {
    // The divider sits at the bottom edge of the chord lane (the ruler is above
    // the lane, so the lane starts at chordRowTop()).
    return std::abs(p.y - (chordRowTop() + laneHeight_)) <= DIVIDER_HIT;
}

int ChordClipContent::annotationIndexAtBeat(double beat) const {
    const auto clipId = getEditingClipId();
    const auto* clip = (clipId != magda::INVALID_CLIP_ID)
                           ? magda::ClipManager::getInstance().getClip(clipId)
                           : nullptr;
    if (clip == nullptr)
        return -1;
    for (int i = 0; i < static_cast<int>(clip->chordAnnotations.size()); ++i) {
        const auto& a = clip->chordAnnotations[static_cast<size_t>(i)];
        if (beat >= a.beatPosition && beat < a.beatPosition + a.lengthBeats)
            return i;
    }
    return -1;
}

ChordClipContent::BlockDrag ChordClipContent::dragModeForBlock(int annIndex, int mouseX) const {
    const auto clipId = getEditingClipId();
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || annIndex < 0)
        return BlockDrag::Move;
    const auto& a = clip->chordAnnotations[static_cast<size_t>(annIndex)];
    const int leftX = chordRowXForBeat(a.beatPosition);
    const int rightX = chordRowXForBeat(a.beatPosition + a.lengthBeats);
    if (std::abs(mouseX - leftX) <= BLOCK_EDGE_PX)
        return BlockDrag::ResizeLeft;
    if (std::abs(mouseX - rightX) <= BLOCK_EDGE_PX)
        return BlockDrag::ResizeRight;
    return BlockDrag::Move;
}

void ChordClipContent::beginBlockDrag(int annIndex, BlockDrag mode, int mouseX) {
    const auto clipId = getEditingClipId();
    auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || annIndex < 0)
        return;
    const auto& a = clip->chordAnnotations[static_cast<size_t>(annIndex)];

    blockDrag_ = mode;
    dragAnnIndex_ = annIndex;
    selectChordGroup(a.chordGroup);
    dragStartMouseBeat_ = chordRowBeatForX(mouseX);
    dragOrigStart_ = a.beatPosition;
    dragOrigEnd_ = a.beatPosition + a.lengthBeats;
    dragNewStart_ = dragOrigStart_;
    dragNewEnd_ = dragOrigEnd_;

    dragNotes_.clear();
    for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
        const auto& n = clip->midiNotes[i];
        if (a.chordGroup != 0 && n.chordGroup == a.chordGroup)
            dragNotes_.push_back({i, n.startBeat, n.lengthBeats, n.noteNumber});
    }
    repaint();
}

void ChordClipContent::updateBlockDrag(int mouseX) {
    auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (clip == nullptr || dragAnnIndex_ < 0 ||
        dragAnnIndex_ >= static_cast<int>(clip->chordAnnotations.size()))
        return;

    // Respect the editor's snap/quantize setting (grid resolution), not a fixed
    // bar. Snap disabled = free drag.
    const double rawDelta = chordRowBeatForX(mouseX) - dragStartMouseBeat_;
    const double minLen = std::max(0.0625, getGridResolutionBeats());
    auto snap = [this](double beat) { return snapEnabled_ ? snapBeatToGrid(beat) : beat; };

    switch (blockDrag_) {
        case BlockDrag::Move:
            dragNewStart_ = std::max(0.0, snap(dragOrigStart_ + rawDelta));
            dragNewEnd_ = dragNewStart_ + (dragOrigEnd_ - dragOrigStart_);
            break;
        case BlockDrag::ResizeRight:
            dragNewStart_ = dragOrigStart_;
            dragNewEnd_ = std::max(dragOrigStart_ + minLen, snap(dragOrigEnd_ + rawDelta));
            break;
        case BlockDrag::ResizeLeft:
            dragNewStart_ =
                juce::jlimit(0.0, dragOrigEnd_ - minLen, snap(dragOrigStart_ + rawDelta));
            dragNewEnd_ = dragOrigEnd_;
            break;
        case BlockDrag::None:
            return;
    }

    // Live preview: move the annotation only (display); notes commit on mouseUp.
    // For an alt-copy the original must stay put, so skip the live move.
    if (!copyDrag_) {
        auto& a = clip->chordAnnotations[static_cast<size_t>(dragAnnIndex_)];
        a.beatPosition = dragNewStart_;
        a.lengthBeats = dragNewEnd_ - dragNewStart_;
    }
    placeWheel();  // its "+" moves with the chord
    repaint();
}

void ChordClipContent::commitBlockDrag() {
    const auto clipId = getEditingClipId();
    auto& undo = magda::UndoManager::getInstance();

    const bool moved = std::abs(dragNewStart_ - dragOrigStart_) > 1e-6 ||
                       std::abs(dragNewEnd_ - dragOrigEnd_) > 1e-6;

    // Alt-drag: drop a copy of the chord at the target bar, leaving the original.
    if (copyDrag_) {
        if (moved) {
            std::vector<int> pitches;
            pitches.reserve(dragNotes_.size());
            for (const auto& dn : dragNotes_)
                pitches.push_back(dn.note);
            insertChordAtBeat(dragNewStart_, pitches);
        }
        copyDrag_ = false;
        blockDrag_ = BlockDrag::None;
        dragAnnIndex_ = -1;
        dragNotes_.clear();
        repaint();
        return;
    }

    if (moved) {
        const double startDelta = dragNewStart_ - dragOrigStart_;
        for (const auto& dn : dragNotes_) {
            if (blockDrag_ != BlockDrag::ResizeRight && std::abs(startDelta) > 1e-6)
                undo.executeCommand(std::make_unique<magda::MoveMidiNoteCommand>(
                    clipId, dn.index, dn.start + startDelta, dn.note));
            if (blockDrag_ != BlockDrag::Move) {
                const double newStart =
                    (blockDrag_ == BlockDrag::ResizeLeft) ? dragNewStart_ : dn.start;
                undo.executeCommand(std::make_unique<magda::ResizeMidiNoteCommand>(
                    clipId, dn.index, dragNewEnd_ - newStart));
            }
        }
    }

    blockDrag_ = BlockDrag::None;
    dragAnnIndex_ = -1;
    dragNotes_.clear();
    // syncChordAnnotations (fired by the note edits) reconciles the block to the
    // notes; if nothing moved, the live annotation is already correct.
    repaint();
}

void ChordClipContent::mouseMove(const juce::MouseEvent& e) {
    if (isOnLaneDivider(e.getPosition())) {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }
    if (e.y >= chordRowTop() && e.y < chordRowTop() + chordRowHeight() && e.x >= chordLaneLeftX()) {
        const int idx = annotationIndexAtBeat(chordRowBeatForX(e.x));
        if (idx >= 0) {
            const auto mode = dragModeForBlock(idx, e.x);
            if (mode == BlockDrag::Move)
                setMouseCursor(e.mods.isAltDown() ? juce::MouseCursor::CopyingCursor
                                                  : juce::MouseCursor::DraggingHandCursor);
            else
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
    PianoRollContent::mouseMove(e);
}

void ChordClipContent::mouseDown(const juce::MouseEvent& e) {
    const bool onLane =
        e.y >= chordRowTop() && e.y < chordRowTop() + chordRowHeight() && e.x >= chordLaneLeftX();
    const int idx = onLane ? annotationIndexAtBeat(chordRowBeatForX(e.x)) : -1;
    if (idx < 0)
        closeWheel();  // a click on a chord moves the open wheel to it instead
    if (isOnLaneDivider(e.getPosition())) {
        draggingDivider_ = true;
        return;
    }
    if (onLane) {
        if (idx >= 0) {
            grabKeyboardFocus();  // so Delete removes the chord, not the clip
            if (e.mods.isPopupMenu()) {
                showChordContextMenu(idx);
                if (isChordWheelOpen())
                    openWheel(idx);
                return;
            }
            const auto mode = dragModeForBlock(idx, e.x);
            copyDrag_ = (mode == BlockDrag::Move && e.mods.isAltDown());
            if (mode == BlockDrag::Move)
                startChordPreview(idx);  // audition the chord on click
            beginBlockDrag(idx, mode, e.x);
            if (isChordWheelOpen())
                openWheel(idx);
            return;
        }
        if (selectedGroup_ != 0) {
            selectChordGroup(0);
            repaint();
        }
    }
    PianoRollContent::mouseDown(e);
}

void ChordClipContent::mouseDrag(const juce::MouseEvent& e) {
    if (draggingDivider_) {
        laneHeight_ = juce::jlimit(MIN_LANE_HEIGHT, maxLaneHeight(), e.y - chordRowTop());
        resized();
        repaint();
        return;
    }
    if (blockDrag_ != BlockDrag::None) {
        updateBlockDrag(e.x);
        return;
    }
    PianoRollContent::mouseDrag(e);
}

bool ChordClipContent::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && wheelGroup_ != 0) {
        closeWheel();
        return true;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (const int idx = annotationIndexForGroup(selectedGroup_); idx >= 0) {
            deleteChord(idx);
            return true;
        }
    }
    return PianoRollContent::keyPressed(key);
}

void ChordClipContent::paintOverChildren(juce::Graphics& g) {
    {
        const juce::Graphics::ScopedSaveState state(g);
        if (wheelGroup_ != 0 && wheel_ != nullptr)
            g.excludeClipRegion(wheel_->getBounds());  // keeps the playhead off the wheel
        PianoRollContent::paintOverChildren(g);
    }
    paintWheelDragLabel(g);

    if (!copyDrag_ || blockDrag_ == BlockDrag::None)
        return;

    const int x1 = std::max(chordLaneLeftX(), chordRowXForBeat(dragNewStart_));
    const int x2 = chordRowXForBeat(dragNewEnd_);
    if (x2 <= x1)
        return;

    const juce::Rectangle<int> ghost(x1 + 1, chordRowTop() + 2, x2 - x1 - 2, chordRowHeight() - 4);
    const auto accent = ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
    g.setColour(accent.withAlpha(0.22f));
    g.fillRoundedRectangle(ghost.toFloat(), 4.0f);
    g.setColour(accent.withAlpha(0.7f));
    g.drawRoundedRectangle(ghost.toFloat().reduced(0.5f), 4.0f, 1.2f);

    // "+" badge (copy affordance)
    const float s = 14.0f;
    const juce::Rectangle<float> badge(static_cast<float>(ghost.getRight()) - s - 2.0f,
                                       static_cast<float>(ghost.getY()) + 2.0f, s, s);
    g.setColour(accent);
    g.fillEllipse(badge);
    g.setColour(juce::Colours::white);
    const auto c = badge.getCentre();
    const float r = 3.5f;
    g.drawLine(c.x - r, c.y, c.x + r, c.y, 1.6f);
    g.drawLine(c.x, c.y - r, c.x, c.y + r, 1.6f);
}

void ChordClipContent::mouseUp(const juce::MouseEvent& e) {
    stopChordPreview();
    if (draggingDivider_) {
        draggingDivider_ = false;
        return;
    }
    if (blockDrag_ != BlockDrag::None) {
        commitBlockDrag();
        // An alt-copy re-detects, which renumbers every chord the wheel could be following.
        if (annotationIndexForGroup(wheelGroup_) < 0)
            closeWheel();
        else
            placeWheel();
        return;
    }
    PianoRollContent::mouseUp(e);
}

void ChordClipContent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.y >= chordRowTop() && e.y < chordRowTop() + chordRowHeight() && e.x >= chordLaneLeftX()) {
        const int idx = annotationIndexAtBeat(chordRowBeatForX(e.x));
        if (idx >= 0) {
            openChordEditor(idx);
            return;
        }
    }
    PianoRollContent::mouseDoubleClick(e);
}

void ChordClipContent::openChordEditor(int annIndex) {
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (clip == nullptr || annIndex < 0 ||
        annIndex >= static_cast<int>(clip->chordAnnotations.size()))
        return;
    const auto& ann = clip->chordAnnotations[static_cast<size_t>(annIndex)];

    const auto spec = magda::music::ChordEngine::parseChordName(ann.chordName);

    // Derive the octave from the chord's lowest linked note.
    auto groupPitches = clip->midiNotes | std::views::filter(isInChordGroup(ann.chordGroup)) |
                        std::views::transform(&magda::MidiNote::noteNumber);
    const int octave =
        std::ranges::empty(groupPitches) ? 4 : std::ranges::min(groupPitches) / 12 - 1;

    // Anchor the editor at the block; capture the bar (stable across edits, where
    // the annotation index is not).
    const double bar = ann.beatPosition;
    const int x1 = chordRowXForBeat(ann.beatPosition);
    const int x2 = chordRowXForBeat(ann.beatPosition + ann.lengthBeats);
    const juce::Rectangle<int> blockLocal(x1, chordRowTop() + 2, std::max(20, x2 - x1),
                                          chordRowHeight() - 4);
    const auto screenArea = localAreaToGlobal(blockLocal);

    auto popup =
        std::make_unique<ChordEditorPopup>(spec.root, spec.quality, octave, spec.inversion);
    popup->onChange = [this, bar](ChordRoot r, ChordQuality q, int oct, int inv) {
        const auto chord =
            magda::music::ChordEngine::getInstance().buildChordInversion(r, q, inv, oct);
        const int idx = annotationIndexAtBeat(bar + 0.001);
        if (idx >= 0)
            replaceChordNotes(idx, pitchesOf(chord));
    };

    juce::CallOutBox::launchAsynchronously(std::move(popup), screenArea, nullptr);
}

void ChordClipContent::replaceChordNotes(int annIndex, const std::vector<int>& pitches) {
    const auto clipId = getEditingClipId();
    auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || pitches.empty() || annIndex < 0 ||
        annIndex >= static_cast<int>(clip->chordAnnotations.size()))
        return;

    const auto ann = clip->chordAnnotations[static_cast<size_t>(annIndex)];  // copy
    const double bar = ann.beatPosition;
    const double length = ann.lengthBeats;
    const int group = ann.chordGroup;

    magda::CompoundOperationScope scope("Edit Chord");
    auto& undo = magda::UndoManager::getInstance();

    // Delete the chord's existing notes, then insert the new voicing at the same
    // bar/length.
    const auto indices = noteIndicesInChordGroup(*clip, group);
    for (size_t idx : indices)
        undo.executeCommand(std::make_unique<magda::DeleteMidiNoteCommand>(clipId, idx));

    for (int p : pitches)
        undo.executeCommand(std::make_unique<magda::AddMidiNoteCommand>(
            clipId, bar, std::clamp(p, 0, 127), length, 100));

    redetectChords();
    repaint();
}

std::vector<int> ChordClipContent::chordPitches(int annIndex) const {
    std::vector<int> pitches;
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (clip == nullptr || annIndex < 0 ||
        annIndex >= static_cast<int>(clip->chordAnnotations.size()))
        return pitches;
    const int group = clip->chordAnnotations[static_cast<size_t>(annIndex)].chordGroup;
    std::ranges::copy(clip->midiNotes | std::views::filter(isInChordGroup(group)) |
                          std::views::transform(&magda::MidiNote::noteNumber),
                      std::back_inserter(pitches));
    return pitches;
}

void ChordClipContent::startChordPreview(int annIndex) {
    stopChordPreview();
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (clip == nullptr || annIndex < 0 ||
        annIndex >= static_cast<int>(clip->chordAnnotations.size()))
        return;
    previewPitches(chordPitches(annIndex));
    previewGroup_ = clip->chordAnnotations[static_cast<size_t>(annIndex)].chordGroup;
    repaint();
}

void ChordClipContent::previewPitches(const std::vector<int>& pitches) {
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (clip == nullptr)
        return;
    for (int p : pitches) {
        magda::TrackManager::getInstance().previewNote(clip->trackId, p, 100, true);
        previewNotes_.push_back(p);
    }
}

void ChordClipContent::stopChordPreview() {
    previewGroup_ = 0;
    if (previewNotes_.empty()) {
        repaint();
        return;
    }
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    const auto trackId = clip ? clip->trackId : magda::INVALID_TRACK_ID;
    for (int p : previewNotes_)
        magda::TrackManager::getInstance().previewNote(trackId, p, 0, false);
    previewNotes_.clear();
    repaint();
}

void ChordClipContent::deleteChord(int annIndex) {
    const auto clipId = getEditingClipId();
    auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || annIndex < 0 ||
        annIndex >= static_cast<int>(clip->chordAnnotations.size()))
        return;
    const int group = clip->chordAnnotations[static_cast<size_t>(annIndex)].chordGroup;

    const auto indices = noteIndicesInChordGroup(*clip, group);

    magda::CompoundOperationScope scope("Delete Chord");
    auto& undo = magda::UndoManager::getInstance();
    for (size_t idx : indices)
        undo.executeCommand(std::make_unique<magda::DeleteMidiNoteCommand>(clipId, idx));

    if (selectedGroup_ == group)
        selectChordGroup(0);
    if (wheelGroup_ == group)
        closeWheel();
    redetectChords();
    repaint();
}

void ChordClipContent::duplicateChord(int annIndex) {
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (clip == nullptr || annIndex < 0 ||
        annIndex >= static_cast<int>(clip->chordAnnotations.size()))
        return;
    const auto pitches = chordPitches(annIndex);
    if (pitches.empty())
        return;
    const auto& ann = clip->chordAnnotations[static_cast<size_t>(annIndex)];
    const double bar = beatsPerBar();

    // Drop the copy in the next free bar after the chord.
    double t = ann.beatPosition + std::max(bar, ann.lengthBeats);
    for (int i = 0; i < 128; ++i, t += bar) {
        if (annotationIndexAtBeat(t + 0.001) < 0) {
            insertChordAtBeat(t, pitches);
            break;
        }
    }
}

void ChordClipContent::showChordContextMenu(int annIndex) {
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (clip == nullptr || annIndex < 0 ||
        annIndex >= static_cast<int>(clip->chordAnnotations.size()))
        return;
    selectChordGroup(clip->chordAnnotations[static_cast<size_t>(annIndex)].chordGroup);
    repaint();
    const double bar = clip->chordAnnotations[static_cast<size_t>(annIndex)].beatPosition;

    juce::PopupMenu menu;
    menu.addItem(1, "Edit...");
    menu.addItem(2, "Duplicate");
    menu.addSeparator();
    menu.addItem(3, "Delete");
    menu.showMenuAsync(
        juce::PopupMenu::Options(),
        [safeThis = juce::Component::SafePointer<ChordClipContent>(this), bar](int result) {
            if (safeThis == nullptr)
                return;
            const int idx = safeThis->annotationIndexAtBeat(bar + 0.001);
            if (idx < 0)
                return;
            if (result == 1)
                safeThis->openChordEditor(idx);
            else if (result == 2)
                safeThis->duplicateChord(idx);
            else if (result == 3)
                safeThis->deleteChord(idx);
        });
}

bool ChordClipContent::insertChordAtBeat(double clipRelativeBeat, const std::vector<int>& pitches) {
    const auto clipId = getEditingClipId();
    if (clipId == magda::INVALID_CLIP_ID || pitches.empty())
        return false;

    const auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr)
        return false;

    // Chords snap to the bar so the per-bar chord detection picks them up
    // cleanly. A chord defaults to one bar long.
    const double barBeats = beatsPerBar();
    const double bar = std::max(0.0, std::round(clipRelativeBeat / barBeats) * barBeats);
    constexpr int kDefaultVelocity = 100;

    // A bar that already has a chord is for editing it, not stacking a new one.
    const auto coversBar = [bar](const magda::ClipInfo::ChordAnnotation& ann) {
        return bar >= ann.beatPosition && bar < ann.beatPosition + ann.lengthBeats;
    };
    if (std::ranges::any_of(clip->chordAnnotations, coversBar))
        return false;

    // Insert the notes, then detection builds the (linked) chord-lane block, so
    // later note edits re-sync the chord via syncChordAnnotations().
    magda::CompoundOperationScope scope("Add Chord");
    auto& undo = magda::UndoManager::getInstance();
    for (int pitch : pitches) {
        undo.executeCommand(std::make_unique<magda::AddMidiNoteCommand>(
            clipId, bar, std::clamp(pitch, 0, 127), barBeats, kDefaultVelocity));
    }

    redetectChords();
    return true;
}

bool ChordClipContent::onChordRowClicked(double clipRelativeBeat) {
    // Default to a C major triad; quality/extensions get edited afterwards.
    const auto chord = magda::music::ChordEngine::getInstance().buildChordInRootPosition(
        magda::music::ChordRoot::C, magda::music::ChordQuality::Major, 4);
    insertChordAtBeat(clipRelativeBeat, pitchesOf(chord));
    return true;  // consume the click either way (empty bar adds; occupied is reserved)
}

int ChordClipContent::annotationIndexForGroup(int chordGroup) const {
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (clip == nullptr || chordGroup == 0)
        return -1;
    const auto& annotations = clip->chordAnnotations;
    const auto it =
        std::ranges::find(annotations, chordGroup, &magda::ClipInfo::ChordAnnotation::chordGroup);
    return it == annotations.end()
               ? -1
               : static_cast<int>(std::ranges::distance(annotations.begin(), it));
}

std::optional<double> ChordClipContent::nextChordSlotBeat(int annIndex) const {
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (clip == nullptr || annIndex < 0 ||
        annIndex >= static_cast<int>(clip->chordAnnotations.size()))
        return std::nullopt;
    const auto& ann = clip->chordAnnotations[static_cast<size_t>(annIndex)];
    const double bar = beatsPerBar();
    double beat = std::ceil((ann.beatPosition + ann.lengthBeats) / bar - 1e-6) * bar;
    for (int i = 0; i < 128; ++i, beat += bar)
        if (annotationIndexAtBeat(beat + 0.001) < 0)
            return beat;
    return std::nullopt;
}

void ChordClipContent::setChordWheelOpen(bool open) {
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (!open || clip == nullptr || clip->chordAnnotations.empty()) {
        closeWheel();
        return;
    }
    int idx = annotationIndexForGroup(selectedGroup_);
    if (idx < 0) {
        const auto& annotations = clip->chordAnnotations;
        const auto last = std::ranges::max_element(annotations, {},
                                                   &magda::ClipInfo::ChordAnnotation::beatPosition);
        idx = static_cast<int>(std::ranges::distance(annotations.begin(), last));
        selectChordGroup(last->chordGroup);
    }
    openWheel(idx);
}

void ChordClipContent::clipSelectionChanged(magda::ClipId clipId) {
    PianoRollContent::clipSelectionChanged(clipId);
    if (clipId != getEditingClipId())
        closeWheel();
    if (onChordSelectionChanged)
        onChordSelectionChanged();
}

void ChordClipContent::clipPropertyChanged(magda::ClipId clipId) {
    PianoRollContent::clipPropertyChanged(clipId);
    if (clipId == getEditingClipId() && onChordSelectionChanged)
        onChordSelectionChanged();
}

void ChordClipContent::selectChordGroup(int chordGroup) {
    if (selectedGroup_ == chordGroup)
        return;
    selectedGroup_ = chordGroup;
    if (onChordSelectionChanged)
        onChordSelectionChanged();
}

std::optional<FifthsSource> ChordClipContent::selectedChordSource() const {
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    const int idx = annotationIndexForGroup(selectedGroup_);
    if (clip == nullptr || idx < 0)
        return std::nullopt;
    const auto& ann = clip->chordAnnotations[static_cast<size_t>(idx)];
    const auto spec = magda::music::ChordEngine::parseChordName(ann.chordName);
    const auto pitches = chordPitches(idx);
    return FifthsSource{spec.root, spec.quality,
                        magda::music::NotationSettings::getInstance().format(ann.chordName),
                        progressionKey(*clip),
                        pitches.empty() ? 4 : std::ranges::min(pitches) / 12 - 1};
}

void ChordClipContent::openWheel(int annIndex) {
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (clip == nullptr || annIndex < 0 ||
        annIndex >= static_cast<int>(clip->chordAnnotations.size()))
        return;
    const auto& ann = clip->chordAnnotations[static_cast<size_t>(annIndex)];

    if (wheel_ == nullptr) {
        wheel_ = std::make_unique<ChordWheel>();
        wheel_->onPick = [this](ChordRoot root, ChordQuality quality) {
            pickFromWheel(root, quality);
        };
        wheel_->onAuditionStart = [this](ChordRoot root, ChordQuality quality) {
            auditionFromWheel(root, quality);
        };
        wheel_->onAuditionStop = [this] { stopChordPreview(); };
        wheel_->onDragMove = [this](ChordRoot root, ChordQuality quality, juce::Point<int> point) {
            wheelDragPoint_ = wheel_->getPosition() + point;
            wheelDragLabel_ = ChordWheel::labelFor(root, quality);
            repaint();
        };
        wheel_->onDragEnd = [this](ChordRoot root, ChordQuality quality, juce::Point<int> point) {
            const auto drop = wheel_->getPosition() + point;
            wheelDragPoint_.reset();
            repaint();
            const auto slot = nextChordSlotArea(annotationIndexForGroup(wheelGroup_));
            if (slot.has_value() && slot->contains(drop))
                pickFromWheel(root, quality);
        };
        wheel_->onDismiss = [this] {
            closeWheel();
            grabKeyboardFocus();
        };
        addChildComponent(*wheel_);
    }

    const auto spec = magda::music::ChordEngine::parseChordName(ann.chordName);
    wheelGroup_ = ann.chordGroup;
    wheel_->setSource(spec.root, spec.quality,
                      magda::music::NotationSettings::getInstance().format(ann.chordName),
                      progressionKey(*clip));
    placeWheel();
    wheel_->setVisible(true);
    wheel_->toFront(false);
    wheel_->grabKeyboardFocus();
    repaint();
    if (onChordWheelOpenChanged)
        onChordWheelOpenChanged(true);
}

void ChordClipContent::closeWheel() {
    if (wheelGroup_ == 0)
        return;
    wheelGroup_ = 0;
    wheelDragPoint_.reset();
    if (wheel_ != nullptr)
        wheel_->setVisible(false);
    repaint();
    if (onChordWheelOpenChanged)
        onChordWheelOpenChanged(false);
}

void ChordClipContent::placeWheel() {
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    const int idx = annotationIndexForGroup(wheelGroup_);
    if (wheel_ == nullptr || clip == nullptr || idx < 0)
        return;
    const auto& ann = clip->chordAnnotations[static_cast<size_t>(idx)];

    const auto slotArea = nextChordSlotArea(idx);
    const int clearOf = slotArea.has_value() ? slotArea->getRight()
                                             : chordRowXForBeat(ann.beatPosition + ann.lengthBeats);
    const int laneLeft = chordLaneLeftX();
    const int laneMiddle = chordRowTop() + laneHeight_ / 2;
    const int laneBottom = chordRowTop() + laneHeight_;
    const int blockLeft = chordRowXForBeat(ann.beatPosition);

    // The wheel never covers the "+" it is dragged onto: right of it, else left of the chord, else
    // under the lane, shrunk to the roomiest of them when none takes it whole.
    const std::array<juce::Rectangle<int>, 3> rooms{
        juce::Rectangle<int>::leftTopRightBottom(clearOf + kWheelGap, 0, getWidth(), getHeight()),
        juce::Rectangle<int>::leftTopRightBottom(laneLeft, 0, blockLeft - kWheelGap, getHeight()),
        juce::Rectangle<int>::leftTopRightBottom(laneLeft, laneBottom + kWheelGap, getWidth(),
                                                 getHeight())};
    const auto sizeIn = [](juce::Rectangle<int> room) {
        return std::max(0, std::min({kMaxWheelSize, room.getWidth(), room.getHeight()}));
    };
    size_t best = 0;
    for (size_t i = 0; i < rooms.size(); ++i) {
        if (sizeIn(rooms[i]) == kMaxWheelSize) {
            best = i;
            break;
        }
        if (sizeIn(rooms[i]) > sizeIn(rooms[best]))
            best = i;
    }
    const auto& room = rooms[best];
    const int size = sizeIn(room);
    const juce::Point<int> anchors[] = {
        {room.getX() + size / 2, laneMiddle},
        {room.getRight() - size / 2, laneMiddle},
        {slotArea.has_value() ? slotArea->getCentreX() : clearOf, room.getY() + size / 2}};
    wheel_->setBounds(
        juce::Rectangle<int>(size, size).withCentre(anchors[best]).constrainedWithin(room));
}

void ChordClipContent::onChordLaneMoved() {
    placeWheel();
    repaint();
}

std::vector<int> ChordClipContent::wheelChordPitches(ChordRoot root, ChordQuality quality) const {
    const auto source = chordPitches(annotationIndexForGroup(wheelGroup_));
    const int octave = source.empty() ? 4 : std::ranges::min(source) / 12 - 1;
    return pitchesOf(magda::music::ChordEngine::buildChordInRootPosition(root, quality, octave));
}

void ChordClipContent::auditionFromWheel(ChordRoot root, ChordQuality quality) {
    stopChordPreview();
    previewPitches(wheelChordPitches(root, quality));
}

void ChordClipContent::pickFromWheel(ChordRoot root, ChordQuality quality) {
    const auto slot = nextChordSlotBeat(annotationIndexForGroup(wheelGroup_));
    const auto pitches = wheelChordPitches(root, quality);
    closeWheel();
    grabKeyboardFocus();
    if (!slot.has_value() || !insertChordAtBeat(*slot, pitches))
        return;

    // The new chord becomes the selection, so the next "+" follows it.
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    if (const int added = annotationIndexAtBeat(*slot + 0.001); clip != nullptr && added >= 0)
        selectChordGroup(clip->chordAnnotations[static_cast<size_t>(added)].chordGroup);
    repaint();
}

void ChordClipContent::resized() {
    PianoRollContent::resized();
    placeWheel();
}

void ChordClipContent::paint(juce::Graphics& g) {
    PianoRollContent::paint(g);
    paintNextChordSlot(g);
}

std::optional<juce::Rectangle<int>> ChordClipContent::nextChordSlotArea(int annIndex) const {
    const auto slot = nextChordSlotBeat(annIndex);
    if (!slot.has_value())
        return std::nullopt;
    const int x1 = std::max(chordLaneLeftX(), chordRowXForBeat(*slot));
    const int x2 = chordRowXForBeat(*slot + beatsPerBar());
    if (x2 - x1 < 12)
        return std::nullopt;
    return juce::Rectangle<int>(x1 + 1, chordRowTop() + 4, x2 - x1 - 2, chordRowHeight() - 8);
}

void ChordClipContent::paintNextChordSlot(juce::Graphics& g) {
    const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
    const int selected = annotationIndexForGroup(selectedGroup_);
    const auto slotArea = nextChordSlotArea(selected);
    if (clip == nullptr || !slotArea.has_value())
        return;

    const auto accent = ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY);
    const auto colour = accent.withAlpha(0.6f);
    const auto area = slotArea->toFloat().reduced(0.5f);
    if (wheelDragPoint_.has_value() && slotArea->contains(*wheelDragPoint_)) {
        g.setColour(accent.withAlpha(0.2f));
        g.fillRoundedRectangle(area, 4.0f);
    }
    juce::Path outline;
    outline.addRoundedRectangle(area, 4.0f);
    juce::Path dashed;
    const float dashes[] = {4.0f, 3.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
    g.setColour(colour);
    g.fillPath(dashed);
    const auto c = area.getCentre();
    const float arm = 6.0f;
    g.drawLine(c.x - arm, c.y, c.x + arm, c.y, 1.5f);
    g.drawLine(c.x, c.y - arm, c.x, c.y + arm, 1.5f);

    // A line to the wheel from the "+", or from the chord when the wheel sits on its left.
    if (wheel_ == nullptr || wheelGroup_ != selectedGroup_)
        return;
    const auto centre = wheel_->getBounds().toFloat().getCentre();
    const float radius = wheel_->discRadius();
    const float y = juce::jlimit(area.getY(), area.getBottom(), centre.y);
    const float dy = y - centre.y;
    if (std::abs(dy) >= radius)
        return;
    const float halfWidth = std::sqrt(radius * radius - dy * dy);
    if (centre.x > area.getRight()) {
        if (const float rimX = centre.x - halfWidth; rimX > area.getRight())
            g.drawLine(area.getRight(), y, rimX, y, 1.0f);
    } else {
        const auto& ann = clip->chordAnnotations[static_cast<size_t>(selected)];
        const auto blockLeft = static_cast<float>(chordRowXForBeat(ann.beatPosition));
        if (const float rimX = centre.x + halfWidth; rimX < blockLeft)
            g.drawLine(rimX, y, blockLeft, y, 1.0f);
    }
}

void ChordClipContent::paintWheelDragLabel(juce::Graphics& g) {
    if (!wheelDragPoint_.has_value())
        return;
    const auto font = FontManager::getInstance().getUIFontMedium(13.0f);
    const int width = juce::GlyphArrangement::getStringWidthInt(font, wheelDragLabel_) + 16;
    const auto label = juce::Rectangle<int>(width, 22).withCentre(*wheelDragPoint_).toFloat();
    g.setColour(ActiveTheme::getColour(ActiveTheme::ACCENT_PRIMARY));
    g.fillRoundedRectangle(label, 4.0f);
    g.setColour(juce::Colours::white);
    g.setFont(font);
    g.drawText(wheelDragLabel_, label, juce::Justification::centred, false);
}

bool ChordClipContent::isInterestedInFileDrag(const juce::StringArray& files) {
    return std::ranges::any_of(files, isMidiFile);
}

void ChordClipContent::filesDropped(const juce::StringArray& files, int x, int y) {
    // Only the chord lane accepts chord drops.
    if (y < chordRowTop() || y >= chordRowTop() + chordRowHeight())
        return;

    for (const auto& f : files) {
        if (!isMidiFile(f))
            continue;

        juce::FileInputStream stream{juce::File(f)};
        if (!stream.openedOk())
            continue;
        juce::MidiFile midiFile;
        if (!midiFile.readFrom(stream))
            continue;

        std::vector<int> pitches;
        for (int t = 0; t < midiFile.getNumTracks(); ++t) {
            const auto* seq = midiFile.getTrack(t);
            for (int i = 0; i < seq->getNumEvents(); ++i) {
                const auto& msg = seq->getEventPointer(i)->message;
                if (msg.isNoteOn())
                    pitches.push_back(msg.getNoteNumber());
            }
        }

        if (insertChordAtBeat(chordRowBeatForX(x), pitches))
            return;  // one chord per drop
    }
}

// Chord blocks dragged from the chord panel arrive as an internal
// {type:"files"} drag on Linux, so route them back into the handlers above.
// See InternalFileDrag.hpp.
bool ChordClipContent::isInterestedInDragSource(const SourceDetails& details) {
    return magda::dnd::acceptsFilesDrag(*this, details);
}

void ChordClipContent::itemDragEnter(const SourceDetails& details) {
    magda::dnd::forwardFilesDragEnter(*this, details);
}

void ChordClipContent::itemDragMove(const SourceDetails& details) {
    magda::dnd::forwardFilesDragMove(*this, details);
}

void ChordClipContent::itemDragExit(const SourceDetails& details) {
    magda::dnd::forwardFilesDragExit(*this, details);
}

void ChordClipContent::itemDropped(const SourceDetails& details) {
    magda::dnd::forwardFilesDrop(*this, details);
}

}  // namespace magda::daw::ui
