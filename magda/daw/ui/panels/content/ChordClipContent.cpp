#include "ChordClipContent.hpp"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "../../state/TimelineController.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/TempoUtils.hpp"
#include "core/UndoManager.hpp"
#include "music/ChordEngine.hpp"
#include "music/ChordEnums.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/InspectorComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

namespace {

using magda::music::ChordQuality;
using magda::music::ChordRoot;

// The editor splits the flat ChordQuality into a base triad/type and a separate
// extension. These map between the two representations.
const char* const kBaseLabels[] = {"Major", "Minor", "Dim",       "Aug",
                                   "Sus2",  "Sus4",  "5 (Power)", "Dom"};
const char* const kExtLabels[] = {"None", "6", "7", "9", "11", "13", "add9"};

ChordQuality qualityFromParts(int base, int ext) {
    using Q = ChordQuality;
    switch (base) {
        case 0:  // Major
            switch (ext) {
                case 1:
                    return Q::MajorAdd6;
                case 2:
                    return Q::Major7;
                case 3:
                    return Q::Major9;
                case 5:
                    return Q::Major7Add13;
                case 6:
                    return Q::MajorAdd9;
                default:
                    return Q::Major;
            }
        case 1:  // Minor
            switch (ext) {
                case 1:
                    return Q::Minor7Add6;
                case 2:
                    return Q::Minor7;
                case 3:
                    return Q::Minor9;
                case 4:
                    return Q::Minor11;
                case 5:
                    return Q::Minor13;
                case 6:
                    return Q::MinorAdd9;
                default:
                    return Q::Minor;
            }
        case 2:  // Diminished
            return ext == 2 ? Q::Diminished7 : ext == 3 ? Q::Diminished9 : Q::Diminished;
        case 3:
            return Q::Augmented;
        case 4:
            return ext == 1 ? Q::Sus2Add6 : Q::Sus2;
        case 5:
            return ext == 1 ? Q::Sus4Add6 : Q::Sus4;
        case 6:
            return Q::Power;
        case 7:  // Dominant
            switch (ext) {
                case 3:
                    return Q::Dominant9;
                case 4:
                    return Q::Dominant11;
                case 5:
                    return Q::Dominant13;
                case 0:
                    return Q::Major;
                default:
                    return Q::Dominant7;
            }
        default:
            return Q::Major;
    }
}

std::pair<int, int> partsFromQuality(ChordQuality q) {
    using Q = ChordQuality;
    switch (q) {
        case Q::Major:
            return {0, 0};
        case Q::MajorAdd6:
            return {0, 1};
        case Q::Major7:
            return {0, 2};
        case Q::Major9:
            return {0, 3};
        case Q::Major7Add13:
            return {0, 5};
        case Q::MajorAdd9:
            return {0, 6};
        case Q::Minor:
            return {1, 0};
        case Q::Minor7Add6:
            return {1, 1};
        case Q::Minor7:
            return {1, 2};
        case Q::Minor9:
            return {1, 3};
        case Q::Minor11:
            return {1, 4};
        case Q::Minor13:
            return {1, 5};
        case Q::MinorAdd9:
            return {1, 6};
        case Q::Diminished:
            return {2, 0};
        case Q::Diminished7:
            return {2, 2};
        case Q::Diminished9:
            return {2, 3};
        case Q::Augmented:
            return {3, 0};
        case Q::Sus2:
            return {4, 0};
        case Q::Sus2Add6:
            return {4, 1};
        case Q::Sus4:
            return {5, 0};
        case Q::Sus4Add6:
            return {5, 1};
        case Q::Power:
            return {6, 0};
        case Q::Dominant7:
            return {7, 2};
        case Q::Dominant9:
            return {7, 3};
        case Q::Dominant11:
            return {7, 4};
        case Q::Dominant13:
            return {7, 5};
        default:
            return {0, 0};
    }
}

/// Root / base-quality / extension / octave / inversion editor (CallOutBox).
class ChordEditorPopup : public juce::Component {
  public:
    std::function<void(ChordRoot, ChordQuality, int octave, int inversion)> onChange;

    ChordEditorPopup(ChordRoot root, ChordQuality quality, int octave, int inversion) {
        const auto [base, ext] = partsFromQuality(quality);

        auto wire = [this](juce::ComboBox& c) {
            c.setLookAndFeel(&laf_);
            c.setColour(juce::ComboBox::backgroundColourId,
                        DarkTheme::getColour(DarkTheme::SURFACE));
            c.setColour(juce::ComboBox::textColourId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
            c.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
            c.onChange = [this] {
                if (onChange)
                    onChange(static_cast<ChordRoot>(rootCombo_.getSelectedId() - 1),
                             qualityFromParts(baseCombo_.getSelectedId() - 1,
                                              extCombo_.getSelectedId() - 1),
                             octaveCombo_.getSelectedId() - 1, inversionCombo_.getSelectedId() - 1);
            };
            addAndMakeVisible(c);
        };

        for (int i = 0; i < 12; ++i)
            rootCombo_.addItem(magda::music::ChordUtils::rootToString(static_cast<ChordRoot>(i)),
                               i + 1);
        rootCombo_.setSelectedId(static_cast<int>(root) + 1, juce::dontSendNotification);

        for (int i = 0; i < 8; ++i)
            baseCombo_.addItem(kBaseLabels[i], i + 1);
        baseCombo_.setSelectedId(base + 1, juce::dontSendNotification);

        for (int i = 0; i < 7; ++i)
            extCombo_.addItem(kExtLabels[i], i + 1);
        extCombo_.setSelectedId(ext + 1, juce::dontSendNotification);

        for (int o = 0; o <= 8; ++o)
            octaveCombo_.addItem("Octave " + juce::String(o), o + 1);
        octaveCombo_.setSelectedId(juce::jlimit(0, 8, octave) + 1, juce::dontSendNotification);

        for (int inv = 0; inv <= 3; ++inv)
            inversionCombo_.addItem(inv == 0 ? "Root position" : ("Inversion " + juce::String(inv)),
                                    inv + 1);
        inversionCombo_.setSelectedId(juce::jlimit(0, 3, inversion) + 1,
                                      juce::dontSendNotification);

        wire(rootCombo_);
        wire(baseCombo_);
        wire(extCombo_);
        wire(octaveCombo_);
        wire(inversionCombo_);

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
    InspectorComboBoxLookAndFeel laf_;
    juce::ComboBox rootCombo_, baseCombo_, extCombo_, octaveCombo_, inversionCombo_;
};

}  // namespace

int ChordClipContent::maxLaneHeight() const {
    return std::max(MIN_LANE_HEIGHT, getHeight() - RULER_HEIGHT);
}

bool ChordClipContent::isOnLaneDivider(juce::Point<int> p) const {
    // The divider sits at the bottom edge of the chord lane (above the ruler).
    return std::abs(p.y - laneHeight_) <= DIVIDER_HIT;
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
    selectedGroup_ = a.chordGroup;
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
    if (e.y < chordRowHeight() && e.x >= chordLaneLeftX()) {
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
    if (isOnLaneDivider(e.getPosition())) {
        draggingDivider_ = true;
        return;
    }
    if (e.y < chordRowHeight() && e.x >= chordLaneLeftX()) {
        const int idx = annotationIndexAtBeat(chordRowBeatForX(e.x));
        if (idx >= 0) {
            grabKeyboardFocus();  // so Delete removes the chord, not the clip
            if (e.mods.isPopupMenu()) {
                showChordContextMenu(idx);
                return;
            }
            const auto mode = dragModeForBlock(idx, e.x);
            copyDrag_ = (mode == BlockDrag::Move && e.mods.isAltDown());
            beginBlockDrag(idx, mode, e.x);
            return;
        }
        if (selectedGroup_ != 0) {
            selectedGroup_ = 0;
            repaint();
        }
    }
    PianoRollContent::mouseDown(e);
}

void ChordClipContent::mouseDrag(const juce::MouseEvent& e) {
    if (draggingDivider_) {
        laneHeight_ = juce::jlimit(MIN_LANE_HEIGHT, maxLaneHeight(), e.y);
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
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) &&
        selectedGroup_ != 0) {
        const auto* clip = magda::ClipManager::getInstance().getClip(getEditingClipId());
        if (clip != nullptr) {
            for (int i = 0; i < static_cast<int>(clip->chordAnnotations.size()); ++i)
                if (clip->chordAnnotations[static_cast<size_t>(i)].chordGroup == selectedGroup_) {
                    deleteChord(i);
                    return true;
                }
        }
    }
    return PianoRollContent::keyPressed(key);
}

void ChordClipContent::paintOverChildren(juce::Graphics& g) {
    PianoRollContent::paintOverChildren(g);

    if (!copyDrag_ || blockDrag_ == BlockDrag::None)
        return;

    const int x1 = std::max(chordLaneLeftX(), chordRowXForBeat(dragNewStart_));
    const int x2 = chordRowXForBeat(dragNewEnd_);
    if (x2 <= x1)
        return;

    const juce::Rectangle<int> ghost(x1 + 1, 2, x2 - x1 - 2, chordRowHeight() - 4);
    const auto accent = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
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
    if (draggingDivider_) {
        draggingDivider_ = false;
        return;
    }
    if (blockDrag_ != BlockDrag::None) {
        commitBlockDrag();
        return;
    }
    PianoRollContent::mouseUp(e);
}

void ChordClipContent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.y < chordRowHeight() && e.x >= chordLaneLeftX()) {
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
    int octave = 4;
    int lowest = 128;
    for (const auto& n : clip->midiNotes)
        if (ann.chordGroup != 0 && n.chordGroup == ann.chordGroup)
            lowest = std::min(lowest, n.noteNumber);
    if (lowest <= 127)
        octave = lowest / 12 - 1;

    // Anchor the editor at the block; capture the bar (stable across edits, where
    // the annotation index is not).
    const double bar = ann.beatPosition;
    const int x1 = chordRowXForBeat(ann.beatPosition);
    const int x2 = chordRowXForBeat(ann.beatPosition + ann.lengthBeats);
    const juce::Rectangle<int> blockLocal(x1, 2, std::max(20, x2 - x1), chordRowHeight() - 4);
    const auto screenArea = localAreaToGlobal(blockLocal);

    auto popup =
        std::make_unique<ChordEditorPopup>(spec.root, spec.quality, octave, spec.inversion);
    popup->onChange = [this, bar](ChordRoot r, ChordQuality q, int oct, int inv) {
        const auto chord =
            magda::music::ChordEngine::getInstance().buildChordInversion(r, q, inv, oct);
        std::vector<int> pitches;
        for (const auto& note : chord.notes)
            pitches.push_back(note.noteNumber);
        const int idx = annotationIndexAtBeat(bar + 0.001);
        if (idx >= 0)
            replaceChordNotes(idx, pitches);
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

    auto& undo = magda::UndoManager::getInstance();

    // Delete the chord's existing notes (highest index first so earlier indices
    // stay valid), then insert the new voicing at the same bar/length.
    std::vector<size_t> indices;
    for (size_t i = 0; i < clip->midiNotes.size(); ++i)
        if (group != 0 && clip->midiNotes[i].chordGroup == group)
            indices.push_back(i);
    std::sort(indices.rbegin(), indices.rend());
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
    for (const auto& n : clip->midiNotes)
        if (group != 0 && n.chordGroup == group)
            pitches.push_back(n.noteNumber);
    return pitches;
}

void ChordClipContent::deleteChord(int annIndex) {
    const auto clipId = getEditingClipId();
    auto* clip = magda::ClipManager::getInstance().getClip(clipId);
    if (clip == nullptr || annIndex < 0 ||
        annIndex >= static_cast<int>(clip->chordAnnotations.size()))
        return;
    const int group = clip->chordAnnotations[static_cast<size_t>(annIndex)].chordGroup;

    std::vector<size_t> indices;
    for (size_t i = 0; i < clip->midiNotes.size(); ++i)
        if (group != 0 && clip->midiNotes[i].chordGroup == group)
            indices.push_back(i);
    std::sort(indices.rbegin(), indices.rend());

    auto& undo = magda::UndoManager::getInstance();
    for (size_t idx : indices)
        undo.executeCommand(std::make_unique<magda::DeleteMidiNoteCommand>(clipId, idx));

    if (selectedGroup_ == group)
        selectedGroup_ = 0;
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

    int beatsPerBar = magda::DEFAULT_TIME_SIGNATURE_NUMERATOR;
    if (auto* controller = magda::TimelineController::getCurrent())
        beatsPerBar = controller->getState().tempo.timeSignatureNumerator;
    const double bar = std::max(1, beatsPerBar);

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
    selectedGroup_ = clip->chordAnnotations[static_cast<size_t>(annIndex)].chordGroup;
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
    int beatsPerBar = magda::DEFAULT_TIME_SIGNATURE_NUMERATOR;
    if (auto* controller = magda::TimelineController::getCurrent())
        beatsPerBar = controller->getState().tempo.timeSignatureNumerator;
    const double barBeats = std::max(1, beatsPerBar);
    const double bar = std::max(0.0, std::round(clipRelativeBeat / barBeats) * barBeats);
    constexpr int kDefaultVelocity = 100;

    // A bar that already has a chord is for editing it, not stacking a new one.
    for (const auto& ann : clip->chordAnnotations) {
        if (bar >= ann.beatPosition && bar < ann.beatPosition + ann.lengthBeats)
            return false;
    }

    // Insert the notes, then detection builds the (linked) chord-lane block, so
    // later note edits re-sync the chord via syncChordAnnotations().
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
    std::vector<int> pitches;
    for (const auto& note : chord.notes)
        pitches.push_back(note.noteNumber);

    insertChordAtBeat(clipRelativeBeat, pitches);
    return true;  // consume the click either way (empty bar adds; occupied is reserved)
}

bool ChordClipContent::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
            return true;
    return false;
}

void ChordClipContent::filesDropped(const juce::StringArray& files, int x, int y) {
    // Only the chord lane accepts chord drops.
    if (y >= chordRowHeight())
        return;

    for (const auto& f : files) {
        if (!f.endsWithIgnoreCase(".mid") && !f.endsWithIgnoreCase(".midi"))
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

}  // namespace magda::daw::ui
