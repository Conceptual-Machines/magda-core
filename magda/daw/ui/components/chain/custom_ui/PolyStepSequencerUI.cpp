// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/PolyStepSequencerUI.hpp"

#include <algorithm>

#include "core/GestureRouter.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackManager.hpp"
#include "music/ChordEngine.hpp"
#include "ui/state/ExpandedFaceplate.hpp"

namespace magda::daw::ui {

using PolySeqPlugin = daw::audio::PolyStepSequencerPlugin;
namespace sf = sequencer_faceplate;

std::atomic<int> PolyStepSequencerUI::nextPatternGesture_{magda::kNoStepPatternGesture + 1};

namespace {

/// The velocity @p note plays at on @p step, 0 when the step does not hold it.
int noteVelocity(const sdk::sequencer::PolyStep& step, int note) {
    for (int n = 0; n < step.noteCount; ++n) {
        const auto& held = step.notes[static_cast<size_t>(n)];
        if (held.noteNumber == note)
            return held.velocity > 0 ? held.velocity : step.velocity;
    }
    return 0;
}

/// Where a note started on @p step ends: past the gated ties after it, which hold it.
int heldUntil(const step_pattern::PolyPattern& pattern, int step) {
    const int count = pattern.playingLength();
    int end = step + 1;
    while (end < count && pattern.step(end).tie && pattern.step(end).gate)
        ++end;
    return end;
}

/// True when every note in the pattern can move by @p semitones and stay in
/// MIDI range. A transpose is all or nothing, so it is asked before it is done.
bool canTranspose(const step_pattern::PolyPattern& pattern, int semitones) {
    for (const auto& step : pattern.steps) {
        for (int n = 0; n < step.noteCount; ++n) {
            const int moved = step.notes[static_cast<size_t>(n)].noteNumber + semitones;
            if (moved < 0 || moved > 127)
                return false;
        }
    }
    return true;
}

void transposePattern(step_pattern::PolyPattern& pattern, int semitones) {
    if (!canTranspose(pattern, semitones))
        return;
    for (auto& step : pattern.steps)
        for (int n = 0; n < step.noteCount; ++n)
            step.notes[static_cast<size_t>(n)].noteNumber += semitones;
}

/// Add or remove @p note on @p stepIndex, the way a grid click does.
void toggleStepNote(step_pattern::PolyPattern& pattern, int stepIndex, int note) {
    if (stepIndex < 0 || stepIndex >= sdk::sequencer::kMaxSteps)
        return;

    auto& step = pattern.steps[static_cast<size_t>(stepIndex)];
    for (int i = 0; i < step.noteCount; ++i) {
        if (step.notes[static_cast<size_t>(i)].noteNumber != note)
            continue;
        for (int j = i; j + 1 < step.noteCount; ++j)
            step.notes[static_cast<size_t>(j)] = step.notes[static_cast<size_t>(j) + 1];
        --step.noteCount;
        step.notes[static_cast<size_t>(step.noteCount)] = {};
        return;
    }

    if (step.noteCount >= sdk::sequencer::kMaxNotesPerStep)
        return;  // voice cap reached
    step.notes[static_cast<size_t>(step.noteCount)] = {.noteNumber = juce::jlimit(0, 127, note)};
    ++step.noteCount;
}

}  // namespace

// =============================================================================
// PatternView — what both modes share
// =============================================================================

void PolyStepSequencerUI::PatternView::setContext(const ViewContext& context) {
    context_ = context;
    repaint();
}

const step_pattern::PolyPattern& PolyStepSequencerUI::PatternView::pattern() const {
    static const step_pattern::PolyPattern empty;
    return context_.pattern != nullptr ? *context_.pattern : empty;
}

void PolyStepSequencerUI::PatternView::editPattern(
    const juce::String& description, std::function<void(step_pattern::PolyPattern&)> edit,
    magda::StepPatternGesture gesture) const {
    if (context_.edit)
        context_.edit(description, std::move(edit), gesture);
}

bool PolyStepSequencerUI::PatternView::applyStepMenuAction(int result, int stepIndex) {
    const auto transpose = [this](int semitones) {
        if (!canTranspose(pattern(), semitones))
            return;
        editPattern("Transpose Pattern",
                    [semitones](step_pattern::PolyPattern& p) { transposePattern(p, semitones); });
        patternTransposed(semitones);
    };

    switch (result) {
        case 1:
            editPattern("Mute Step", [stepIndex](step_pattern::PolyPattern& p) {
                auto& step = p.steps[static_cast<size_t>(stepIndex)];
                step.gate = !step.gate;
            });
            return true;
        case 2:
            editPattern("Clear Step", [stepIndex](step_pattern::PolyPattern& p) {
                p.steps[static_cast<size_t>(stepIndex)] = {};
            });
            return true;
        case 10:
            transpose(1);
            return true;
        case 11:
            transpose(-1);
            return true;
        case 12:
            transpose(12);
            return true;
        case 13:
            transpose(-12);
            return true;
        case 20:
            editPattern("Clear Pattern", [](step_pattern::PolyPattern& p) { p.steps.fill({}); });
            return true;
        default:
            return false;
    }
}

// =============================================================================
// KeysView — piano-roll style pitch x step grid
// =============================================================================

class PolyStepSequencerUI::KeysView : public PolyStepSequencerUI::PatternView {
  public:
    KeysView() = default;

    void setPlayStep(int step) override {
        if (step != playStep_) {
            playStep_ = step;
            repaint();
        }
    }

    void patternChanged() override {
        repaint();
    }

    void patternTransposed(int semitones) override {
        shiftWindow(semitones);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        auto strip = bounds.removeFromLeft(STRIP_WIDTH);
        bounds.removeFromLeft(4);
        pianoArea_ = bounds.removeFromLeft(sf::kGutterWidth - STRIP_WIDTH - 8);
        bounds.removeFromLeft(4);
        cellArea_ = bounds;

        // Octave shift at the top of the strip, zoom at its foot.
        auto shiftBox = strip.removeFromTop(BUTTON_BOX_HEIGHT);
        octaveUpArea_ = shiftBox.removeFromTop(BUTTON_BOX_HEIGHT / 2);
        octaveDownArea_ = shiftBox;
        auto zoomBox = strip.removeFromBottom(BUTTON_BOX_HEIGHT);
        zoomInArea_ = zoomBox.removeFromTop(BUTTON_BOX_HEIGHT / 2);
        zoomOutArea_ = zoomBox;
    }

    void paint(juce::Graphics& g) override {
        if (cellArea_.isEmpty() || context_.pattern == nullptr)
            return;

        sf::drawButtonPair(g, octaveUpArea_.getUnion(octaveDownArea_), sf::Glyph::Up,
                           lowNote_ < 127 - visibleNotes_ + 1, sf::Glyph::Down, lowNote_ > 0);
        sf::drawButtonPair(g, zoomInArea_.getUnion(zoomOutArea_), sf::Glyph::Plus,
                           visibleNotes_ > MIN_VISIBLE_NOTES, sf::Glyph::Minus,
                           visibleNotes_ < MAX_VISIBLE_NOTES);

        const auto& pat = pattern();
        const int count = pat.playingLength();
        const float rowH = static_cast<float>(cellArea_.getHeight()) / visibleNotes_;
        const float colW = static_cast<float>(cellArea_.getWidth()) / static_cast<float>(count);
        const auto noteOfRow = [this](int row) { return lowNote_ + (visibleNotes_ - 1 - row); };

        // The keys beside the rows, C named.
        g.setFont(FontManager::getInstance().getMonoFont(juce::jlimit(7.0f, 9.5f, rowH)));
        for (int row = 0; row < visibleNotes_; ++row) {
            const int note = noteOfRow(row);
            const juce::Rectangle<float> key{static_cast<float>(pianoArea_.getX()),
                                             pianoArea_.getY() + row * rowH,
                                             static_cast<float>(pianoArea_.getWidth()), rowH};
            g.setColour(sf::isBlackKey(note) ? sf::kBlackKey : sf::kWhiteKey);
            g.fillRect(key.reduced(0.0f, 0.5f));
            if (note % 12 == 0) {
                g.setColour(sf::kBlackKey.withAlpha(0.7f));
                g.drawText(sf::noteName(note), key.withTrimmedLeft(3.0f).toNearestInt(),
                           juce::Justification::centredLeft, false);
            }
        }

        sf::drawGridFloor(
            g, cellArea_, count, visibleNotes_,
            [&](int row) { return sf::isBlackKey(noteOfRow(row)); },
            [&pat](int i) { return !pat.step(i).gate; });

        // Each hit as a bar running on through the gated ties that hold it.
        const float barH = juce::jmax(3.0f, rowH - 4.0f);
        for (int i = 0; i < count; ++i) {
            const auto& step = pat.step(i);
            const bool held = step.tie && i > 0;
            const int end = held ? i + 1 : heldUntil(pat, i);
            for (int n = 0; n < step.noteCount; ++n) {
                const int note = step.notes[static_cast<size_t>(n)].noteNumber;
                const int row = visibleNotes_ - 1 - (note - lowNote_);
                if (row < 0 || row >= visibleNotes_)
                    continue;
                const juce::Rectangle<float> bar{cellArea_.getX() + i * colW + 2.0f,
                                                 cellArea_.getY() + row * rowH + (rowH - barH) / 2,
                                                 (end - i) * colW - 4.0f, barH};
                sf::drawNoteBar(g, bar, noteVelocity(step, note), step.gate && !held);
            }
        }

        sf::drawPlayhead(g, cellArea_, count, playStep_);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (context_.pattern == nullptr)
            return;
        const auto pos = e.getPosition();

        if (zoomInArea_.contains(pos)) {
            zoomBy(-2);  // fewer rows = taller keys
            return;
        }
        if (zoomOutArea_.contains(pos)) {
            zoomBy(2);
            return;
        }
        if (octaveUpArea_.contains(pos)) {
            shiftWindow(12);
            return;
        }
        if (octaveDownArea_.contains(pos)) {
            shiftWindow(-12);
            return;
        }

        if (!cellArea_.contains(pos))
            return;

        const int step = stepAt(pos.x);
        const int note = noteAt(pos.y);
        if (step < 0 || note < 0 || note > 127)
            return;

        if (e.mods.isRightButtonDown()) {
            showStepContextMenu(step);
            return;
        }

        editPattern("Toggle Step Note",
                    [step, note](step_pattern::PolyPattern& p) { toggleStepNote(p, step, note); });
        repaint();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        const auto gesture = magda::GestureRouter::getInstance().resolve(
            magda::GestureContext::StepSequencer, wheel, e.mods, e.getPosition());
        const int steps = magda::quantizedGestureStep(gesture.magnitude);
        if (gesture.type == magda::GestureActionType::ZoomVertical)
            zoomBy(-2 * steps);  // scroll up = zoom in (fewer rows)
        else if (gesture.type == magda::GestureActionType::ScrollVertical)
            shiftWindow(2 * steps);
        else
            juce::Component::mouseWheelMove(e, wheel);
    }

  private:
    static constexpr int DEFAULT_VISIBLE_NOTES = 24;  // ~2 octaves
    static constexpr int MIN_VISIBLE_NOTES = 12;      // zoomed in: one octave
    static constexpr int MAX_VISIBLE_NOTES = 48;      // zoomed out: four octaves
    static constexpr int STRIP_WIDTH = 20;
    static constexpr int BUTTON_BOX_HEIGHT = 44;

    void shiftWindow(int semitones) {
        const int next = juce::jlimit(0, 127 - visibleNotes_ + 1, lowNote_ + semitones);
        if (next != lowNote_) {
            lowNote_ = next;
            repaint();
        }
    }

    // Vertical zoom: change how many pitch rows are visible, keeping the centre
    // pitch roughly anchored. Fewer rows = taller keys (zoomed in).
    void zoomBy(int deltaNotes) {
        const int next =
            juce::jlimit(MIN_VISIBLE_NOTES, MAX_VISIBLE_NOTES, visibleNotes_ + deltaNotes);
        if (next == visibleNotes_)
            return;
        const int centre = lowNote_ + visibleNotes_ / 2;
        visibleNotes_ = next;
        lowNote_ = juce::jlimit(0, 127 - visibleNotes_ + 1, centre - visibleNotes_ / 2);
        repaint();
    }

    int stepAt(int x) const {
        if (context_.pattern == nullptr || cellArea_.getWidth() <= 0)
            return -1;
        const int count = pattern().playingLength();
        const int relX = x - cellArea_.getX();
        if (relX < 0 || relX >= cellArea_.getWidth())
            return -1;
        return relX * count / cellArea_.getWidth();
    }

    int noteAt(int y) const {
        if (cellArea_.getHeight() <= 0)
            return -1;
        const int relY = y - cellArea_.getY();
        if (relY < 0 || relY >= cellArea_.getHeight())
            return -1;
        const int row = relY * visibleNotes_ / cellArea_.getHeight();
        return lowNote_ + (visibleNotes_ - 1 - row);
    }

    void showStepContextMenu(int stepIndex) {
        const auto& step = pattern().step(stepIndex);

        juce::PopupMenu menu;
        menu.addItem(1, step.gate ? "Mute Step" : "Unmute Step");
        menu.addItem(2, "Clear Step");
        menu.addSeparator();

        juce::PopupMenu patternMenu;
        patternMenu.addItem(10, "Shift +1 semitone");
        patternMenu.addItem(11, "Shift -1 semitone");
        patternMenu.addItem(12, "Shift +1 octave");
        patternMenu.addItem(13, "Shift -1 octave");
        patternMenu.addSeparator();
        patternMenu.addItem(20, "Clear Pattern");
        menu.addSubMenu("Pattern", patternMenu);

        menu.showMenuAsync(juce::PopupMenu::Options(),
                           [safeThis = juce::Component::SafePointer(this), stepIndex](int result) {
                               if (safeThis == nullptr || safeThis->context_.pattern == nullptr)
                                   return;
                               if (!safeThis->applyStepMenuAction(result, stepIndex))
                                   return;
                               safeThis->repaint();
                           });
    }

    int playStep_ = -1;
    int lowNote_ = 48;                          // C2..B3 window — C3 (60) centered
    int visibleNotes_ = DEFAULT_VISIBLE_NOTES;  // pitch rows shown (vertical zoom)

    juce::Rectangle<int> zoomInArea_;
    juce::Rectangle<int> zoomOutArea_;
    juce::Rectangle<int> octaveUpArea_;
    juce::Rectangle<int> octaveDownArea_;
    juce::Rectangle<int> pianoArea_;
    juce::Rectangle<int> cellArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeysView)
};

// =============================================================================
// DrumLanesView — classic x0x drum-lane grid
// =============================================================================
//
// One horizontal lane per drum sound, lowest note at the bottom (drum machine
// convention). The lane set is discovered from a Drum Grid plugin downstream
// of the sequencer in the same chain (one lane per chain, triggered by the
// chain's low note). Without a Drum Grid a generic GM drum map is shown, and
// pattern notes that match no lane get an orphan lane so they stay editable.

class PolyStepSequencerUI::DrumLanesView : public PolyStepSequencerUI::PatternView,
                                           private magda::TrackManagerListener {
  public:
    DrumLanesView() {
        magda::TrackManager::getInstance().addListener(this);
    }

    ~DrumLanesView() override {
        magda::TrackManager::getInstance().removeListener(this);
    }

    void setContext(const ViewContext& context) override {
        PatternView::setContext(context);
        refreshLanes();
    }

    void setPlayStep(int step) override {
        if (step != playStep_) {
            playStep_ = step;
            repaint();
        }
    }

    void patternChanged() override {
        // Pattern edits can add or remove orphan lanes
        refreshLanes();
    }

    void resized() override {
        auto bounds = getLocalBounds();
        auto strip = bounds.removeFromLeft(STRIP_WIDTH);
        bounds.removeFromLeft(4);
        labelArea_ = bounds.removeFromLeft(sf::kGutterWidth - STRIP_WIDTH - 8);
        bounds.removeFromLeft(4);
        cellArea_ = bounds;
        auto scrollBox = strip.removeFromTop(BUTTON_BOX_HEIGHT);
        scrollUpArea_ = scrollBox.removeFromTop(BUTTON_BOX_HEIGHT / 2);
        scrollDownArea_ = scrollBox;
        clampScrollOffset();
    }

    void paint(juce::Graphics& g) override {
        if (cellArea_.isEmpty() || context_.pattern == nullptr || lanes_.empty())
            return;

        const int numLanes = static_cast<int>(lanes_.size());
        const int visible = visibleLaneCount();
        const float laneH = laneHeight();
        if (visible <= 0)
            return;
        clampScrollOffset();

        const int maxOffset = juce::jmax(0, numLanes - visible);
        sf::drawButtonPair(g, scrollUpArea_.getUnion(scrollDownArea_), sf::Glyph::Up,
                           scrollOffset_ < maxOffset, sf::Glyph::Down, scrollOffset_ > 0);

        const auto& pat = pattern();
        const int count = pat.playingLength();
        const float colW = static_cast<float>(cellArea_.getWidth()) / static_cast<float>(count);
        const auto laneOfRow = [&](int row) { return scrollOffset_ + (visible - 1 - row); };

        sf::drawGridFloor(
            g, cellArea_, count, visible, [&](int row) { return laneOfRow(row) % 2 == 1; },
            [&pat](int i) { return !pat.step(i).gate; });

        g.setFont(FontManager::getInstance().getUIFont(juce::jlimit(8.0f, 11.0f, laneH * 0.6f)));
        for (int row = 0; row < visible; ++row) {
            const int laneIdx = laneOfRow(row);
            if (laneIdx < 0 || laneIdx >= numLanes)
                continue;
            const auto& lane = lanes_[static_cast<size_t>(laneIdx)];
            const float y = cellArea_.getY() + row * laneH;

            g.setColour(ActiveTheme::getColour(lane.orphan ? ActiveTheme::DEVICE_DIM2
                                                           : ActiveTheme::DEVICE_DIM));
            g.drawText(lane.label,
                       juce::Rectangle<float>(static_cast<float>(labelArea_.getX()), y,
                                              static_cast<float>(labelArea_.getWidth()), laneH)
                           .toNearestInt(),
                       juce::Justification::centredRight, true);

            for (int i = 0; i < count; ++i) {
                const auto& step = pat.step(i);
                if (const int velocity = noteVelocity(step, lane.note); velocity > 0)
                    sf::drawNoteBar(
                        g,
                        juce::Rectangle<float>(cellArea_.getX() + i * colW, y, colW, laneH)
                            .reduced(juce::jmin(3.0f, colW * 0.1f), 3.0f),
                        velocity, step.gate && !(step.tie && i > 0));
            }
        }

        sf::drawPlayhead(g, cellArea_, count, playStep_);
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (context_.pattern == nullptr)
            return;
        const auto pos = e.getPosition();

        if (scrollUpArea_.contains(pos)) {
            scrollBy(1);
            return;
        }
        if (scrollDownArea_.contains(pos)) {
            scrollBy(-1);
            return;
        }

        if (!cellArea_.contains(pos))
            return;

        const int step = stepAt(pos.x);
        const int laneIdx = laneAt(pos.y);
        if (step < 0 || laneIdx < 0)
            return;

        if (e.mods.isRightButtonDown()) {
            showStepContextMenu(step);
            return;
        }

        const int laneNote = lanes_[static_cast<size_t>(laneIdx)].note;
        editPattern("Toggle Step Note", [step, laneNote](step_pattern::PolyPattern& p) {
            toggleStepNote(p, step, laneNote);
        });
        repaint();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        const auto gesture = magda::GestureRouter::getInstance().resolve(
            magda::GestureContext::StepSequencer, wheel, e.mods, e.getPosition());
        if (gesture.type == magda::GestureActionType::ScrollVertical)
            scrollBy(magda::quantizedGestureStep(gesture.magnitude));
        else
            juce::Component::mouseWheelMove(e, wheel);
    }

  private:
    static constexpr int STRIP_WIDTH = 20;
    static constexpr int BUTTON_BOX_HEIGHT = 44;
    static constexpr int MIN_LANE_HEIGHT = 16;

    using Lane = DrumLane;

    // --- Lane discovery ---

    void refreshLanes() {
        lanes_ = drumLanesFor(context_.devicePath, pattern());
        clampScrollOffset();
        repaint();
    }

    /** Coalesced async lane refresh (ValueTree callbacks can fire mid-edit). */
    void triggerLaneRefresh() {
        if (refreshPending_)
            return;
        refreshPending_ = true;
        juce::MessageManager::callAsync(
            [safeThis = juce::Component::SafePointer<DrumLanesView>(this)] {
                if (safeThis != nullptr) {
                    safeThis->refreshPending_ = false;
                    safeThis->refreshLanes();
                }
            });
    }

    // Chain membership and pad edits arrive as a devices change on the track, a
    // pad renamed through setChainName as a property change.
    void tracksChanged() override {
        triggerLaneRefresh();
    }
    void trackDevicesChanged(magda::TrackId trackId) override {
        if (trackId == context_.devicePath.trackId)
            triggerLaneRefresh();
    }
    void trackPropertyChanged(int trackId) override {
        if (trackId == context_.devicePath.trackId)
            triggerLaneRefresh();
    }

    // --- Scrolling / geometry ---

    int visibleLaneCount() const {
        const int numLanes = static_cast<int>(lanes_.size());
        if (numLanes == 0 || cellArea_.getHeight() <= 0)
            return 0;
        if (numLanes * MIN_LANE_HEIGHT <= cellArea_.getHeight())
            return numLanes;
        return juce::jmax(1, cellArea_.getHeight() / MIN_LANE_HEIGHT);
    }

    float laneHeight() const {
        const int visible = visibleLaneCount();
        return visible > 0 ? static_cast<float>(cellArea_.getHeight()) / static_cast<float>(visible)
                           : 0.0f;
    }

    void clampScrollOffset() {
        const int maxOffset = juce::jmax(0, static_cast<int>(lanes_.size()) - visibleLaneCount());
        scrollOffset_ = juce::jlimit(0, maxOffset, scrollOffset_);
    }

    void scrollBy(int deltaLanes) {
        const int previous = scrollOffset_;
        scrollOffset_ += deltaLanes;
        clampScrollOffset();
        if (scrollOffset_ != previous)
            repaint();
    }

    int stepAt(int x) const {
        if (context_.pattern == nullptr || cellArea_.getWidth() <= 0)
            return -1;
        const int count = pattern().playingLength();
        const int relX = x - cellArea_.getX();
        if (relX < 0 || relX >= cellArea_.getWidth())
            return -1;
        return relX * count / cellArea_.getWidth();
    }

    int laneAt(int y) const {
        const int visible = visibleLaneCount();
        const float laneH = laneHeight();
        if (visible <= 0 || laneH <= 0.0f)
            return -1;
        const int relY = y - cellArea_.getY();
        if (relY < 0 || relY >= cellArea_.getHeight())
            return -1;
        const int row = juce::jmin(visible - 1, static_cast<int>(relY / laneH));
        const int laneIdx = scrollOffset_ + (visible - 1 - row);
        return (laneIdx >= 0 && laneIdx < static_cast<int>(lanes_.size())) ? laneIdx : -1;
    }

    void showStepContextMenu(int stepIndex) {
        const auto& step = pattern().step(stepIndex);

        juce::PopupMenu menu;
        menu.addItem(1, step.gate ? "Mute Step" : "Unmute Step");
        menu.addItem(2, "Clear Step");
        menu.addSeparator();

        juce::PopupMenu patternMenu;
        patternMenu.addItem(10, "Shift +1 semitone");
        patternMenu.addItem(11, "Shift -1 semitone");
        patternMenu.addItem(12, "Shift +1 octave");
        patternMenu.addItem(13, "Shift -1 octave");
        patternMenu.addSeparator();
        patternMenu.addItem(20, "Clear Pattern");
        menu.addSubMenu("Pattern", patternMenu);

        menu.showMenuAsync(juce::PopupMenu::Options(),
                           [safeThis = juce::Component::SafePointer(this), stepIndex](int result) {
                               if (safeThis == nullptr || safeThis->context_.pattern == nullptr)
                                   return;
                               if (!safeThis->applyStepMenuAction(result, stepIndex))
                                   return;
                               safeThis->repaint();
                           });
    }

    int playStep_ = -1;
    int scrollOffset_ = 0;  // Index of the bottom-most visible lane
    bool refreshPending_ = false;

    std::vector<Lane> lanes_;  // Sorted note-ascending (lowest paints at the bottom)

    juce::Rectangle<int> scrollUpArea_;
    juce::Rectangle<int> scrollDownArea_;
    juce::Rectangle<int> labelArea_;
    juce::Rectangle<int> cellArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumLanesView)
};

/** Drum Grid chains (or the GM map) and orphan lanes for pattern notes that match no chain,
 *  note-ascending so the lowest lane paints at the bottom. */
std::vector<PolyStepSequencerUI::DrumLane> PolyStepSequencerUI::drumLanesFor(
    const magda::ChainNodePath& devicePath, const step_pattern::PolyPattern& pattern) {
    std::vector<DrumLane> lanes;
    const auto* drumGrid = magda::TrackManager::getInstance().findPadDeviceDownstreamOf(devicePath);
    if (drumGrid != nullptr && drumGrid->pads) {
        // A pad's low note is the incoming MIDI note that triggers it.
        for (const auto& pad : drumGrid->pads->chains)
            lanes.push_back(
                {pad.lowNote, pad.name.isNotEmpty() ? pad.name : sf::noteName(pad.lowNote), false});
    }

    if (lanes.empty()) {
        static const struct {
            int note;
            const char* name;
        } gmLanes[] = {{36, "Kick"}, {38, "Snare"}, {42, "CHat"},  {46, "OHat"},
                       {41, "LTom"}, {47, "MTom"},  {49, "Crash"}, {51, "Ride"}};
        for (const auto& gm : gmLanes)
            lanes.push_back({gm.note, gm.name, false});
    }

    const auto hasLane = [&lanes](int note) {
        return std::ranges::any_of(lanes, [note](const auto& lane) { return lane.note == note; });
    };
    for (int i = 0; i < PolySeqPlugin::MAX_STEPS; ++i) {
        const auto& step = pattern.step(i);
        for (int n = 0; n < step.noteCount; ++n) {
            const int note = step.notes[static_cast<size_t>(n)].noteNumber;
            if (!hasLane(note))
                lanes.push_back({note, sf::noteName(note), true});
        }
    }

    std::ranges::sort(lanes, {}, &DrumLane::note);
    const auto duplicates = std::ranges::unique(lanes, {}, &DrumLane::note);
    lanes.erase(duplicates.begin(), duplicates.end());
    return lanes;
}

// =============================================================================
// Construction
// =============================================================================

PolyStepSequencerUI::PolyStepSequencerUI() {
    strip_.rate.onValueChanged = [this](double value) {
        sendChange(PolySeqPlugin::kRate, static_cast<float>(juce::roundToInt(value)));
    };
    strip_.steps.onValueChanged = [this](double value) {
        const int steps = juce::jlimit(1, PolySeqPlugin::MAX_STEPS, juce::roundToInt(value));
        // How many steps play is part of the pattern, so it is a pattern edit. A drag coalesces
        // into one undo entry; a typed or clicked value stays discrete (#2335).
        editPattern(
            "Set Step Count", [steps](step_pattern::PolyPattern& p) { p.length = steps; },
            strip_.steps.isBeingDragged() ? magda::StepPatternGesture::Continuous
                                          : magda::StepPatternGesture::Discrete);
        repaint();
    };
    strip_.steps.getSlider().onDragEnd = [this] { endPatternGesture(); };
    strip_.gate.onValueChanged = [this](double value) {
        sendChange(PolySeqPlugin::kGateLength, static_cast<float>(value));
    };
    strip_.direction.onChange = [this](int index) {
        sendChange(PolySeqPlugin::kDirection, static_cast<float>(index));
    };

    timeBend_.setShowsGrooves(true);
    bendExpandButton_->onClick = [this] {
        if (onExpandClicked)
            onExpandClicked(timeBend_);
    };
    timeBend_.setHeaderAccessory(bendExpandButton_.get());
    timeBend_.onCommit = [this](const auto& before, const auto& after) {
        timeBendEdited(before, after);
    };

    viewChoice_.setOptions({"Keys", "Drums"});
    viewChoice_.setSelectedIndex(0);
    viewChoice_.onChange = [this](int index) {
        drumViewActive_ = index == 1;
        settingsEdited();
        applyPatternViewMode();
    };
    strip_.addExtra("View", viewChoice_, viewChoice_.getPreferredWidth());
    strip_.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(strip_);
    addAndMakeVisible(timeBend_);

    // MIDI thru and step record live in the device-slot header.

    patternView_ = std::make_unique<KeysView>();
    patternHost_.addAndMakeVisible(*patternView_);
    addChildComponent(patternHost_);
    expandButton_->onClick = [this] {
        if (onExpandClicked)
            onExpandClicked(expandablePart());
    };
    addAndMakeVisible(*expandButton_);
}

PolyStepSequencerUI::~PolyStepSequencerUI() {
    stopTimer();
}

// =============================================================================
// Model and device binding
// =============================================================================

void PolyStepSequencerUI::setSequencer(daw::audio::PolyStepSequencerPlugin* device) {
    stopTimer();
    device_ = device;

    if (device_ != nullptr) {
        drumViewActive_ = device_->viewMode() == "drum";
        syncSettingsFromDevice();
        applyPatternViewMode();
        startTimerHz(30);
    }
}

void PolyStepSequencerUI::setDevicePath(const magda::ChainNodePath& devicePath) {
    devicePath_ = devicePath;
    pushViewContext();
}

void PolyStepSequencerUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    // By paramIndex, not array position: the model's list is in the saved
    // document's order and need not be complete, so a positional read draws one
    // slot's value on another's control (#2335).
    const auto display = [&params](int index, float fallback) {
        for (const auto& parameter : params)
            if (parameter.paramIndex == index)
                return parameter.currentValue;
        return fallback;
    };

    strip_.rate.setValue(static_cast<double>(display(PolySeqPlugin::kRate, 7.0f)),
                         juce::dontSendNotification);
    strip_.direction.setSelectedIndex(juce::roundToInt(display(PolySeqPlugin::kDirection, 0.0f)));
    strip_.gate.setValue(static_cast<double>(display(PolySeqPlugin::kGateLength, 0.8f)),
                         juce::dontSendNotification);

    auto bend = timeBend_.getBend();
    bend.amount = display(PolySeqPlugin::kBendAmount, 1.0f);
    bend.cycles = juce::roundToInt(display(PolySeqPlugin::kBendCycles, 1.0f));
    timeBend_.setBend(bend);

    syncSettingsFromDevice();
    applyPatternViewMode();
    repaint();
}

void PolyStepSequencerUI::setPattern(const step_pattern::PolyPattern& pattern) {
    // See StepSequencerUI::setPattern: the controls are written whether or not
    // the pattern moved, and only the redraw is skipped.
    const bool changed = !(pattern == pattern_);

    pattern_ = pattern;
    const int steps = pattern_.playingLength();
    strip_.steps.setValue(static_cast<double>(steps), juce::dontSendNotification);

    if (!changed)
        return;

    pushViewContext();
    patternView_->patternChanged();
    repaint();
}

void PolyStepSequencerUI::syncSettingsFromDevice() {
    if (device_ == nullptr)
        return;

    auto bend = device_->timeBend.current();
    bend.amount = timeBend_.getBend().amount;
    bend.cycles = timeBend_.getBend().cycles;
    timeBend_.setBend(bend);
    quantize_ = static_cast<double>(device_->quantize.load(std::memory_order_relaxed));
    quantizeSub_ = device_->quantizeSub.load(std::memory_order_relaxed);
    drumViewActive_ = device_->viewMode() == "drum";
}

void PolyStepSequencerUI::sendChange(int paramIndex, float value) {
    if (onParameterChanged)
        onParameterChanged(paramIndex, value);
}

void PolyStepSequencerUI::settingsEdited() {
    if (!onSettingsEdited)
        return;

    using IDs = daw::audio::PolyStepSequencerPlugin::SettingIDs;
    juce::NamedValueSet settings;
    settings.set(daw::audio::DeviceTimeBend::settingId,
                 juce::String(sdk::sequencer::timeBendToText(timeBend_.getBend())));
    settings.set(IDs::quantize, static_cast<float>(quantize_));
    settings.set(IDs::quantizeSub, quantizeSub_);
    settings.set(IDs::viewMode, drumViewActive_ ? juce::String("drum") : juce::String("keys"));
    onSettingsEdited(settings);
}

void PolyStepSequencerUI::timeBendEdited(const sdk::sequencer::TimeBend& before,
                                         const sdk::sequencer::TimeBend& after) {
    if (after.amount != before.amount)
        sendChange(PolySeqPlugin::kBendAmount, after.amount);
    if (after.cycles != before.cycles)
        sendChange(PolySeqPlugin::kBendCycles, static_cast<float>(after.cycles));
    auto rest = after;
    rest.amount = before.amount;
    rest.cycles = before.cycles;
    if (!(rest == before))
        settingsEdited();
}

void PolyStepSequencerUI::editPattern(const juce::String& description,
                                      std::function<void(step_pattern::PolyPattern&)> edit,
                                      magda::StepPatternGesture gesture) {
    if (onPatternEdited)
        onPatternEdited(description, std::move(edit), gesture);
}

void PolyStepSequencerUI::drainRecordedSteps() {
    if (device_ == nullptr)
        return;

    // The device heard the notes; the model is where they are written.
    daw::audio::PolyStepSequencerPlugin::RecordedStep recorded;
    while (device_->popRecordedStep(recorded)) {
        editPattern("Record Step", [recorded](step_pattern::PolyPattern& p) {
            if (recorded.stepIndex < 0 || recorded.stepIndex >= sdk::sequencer::kMaxSteps)
                return;
            p.steps[static_cast<size_t>(recorded.stepIndex)].gate = true;
            toggleStepNote(p, recorded.stepIndex, recorded.noteNumber);
        });
    }
}

void PolyStepSequencerUI::pushViewContext() {
    if (patternView_ == nullptr)
        return;

    ViewContext context;
    context.pattern = &pattern_;
    context.devicePath = devicePath_;
    context.edit = [this](const juce::String& description,
                          std::function<void(step_pattern::PolyPattern&)> edit,
                          magda::StepPatternGesture gesture) {
        editPattern(description, std::move(edit), gesture);
    };
    patternView_->setContext(context);
}

void PolyStepSequencerUI::applyPatternViewMode() {
    if (drumViewActive_ != usingDrumView_ || patternView_ == nullptr) {
        usingDrumView_ = drumViewActive_;
        patternView_ = usingDrumView_
                           ? std::unique_ptr<PatternView>(std::make_unique<DrumLanesView>())
                           : std::unique_ptr<PatternView>(std::make_unique<KeysView>());
        patternHost_.addAndMakeVisible(*patternView_);
        patternHost_.resized();
        pushViewContext();
        patternView_->setPlayStep(currentPlayStep_);
        resized();
    }
    viewChoice_.setSelectedIndex(drumViewActive_ ? 1 : 0);
}

void PolyStepSequencerUI::timerCallback() {
    if (device_ == nullptr)
        return;

    drainRecordedSteps();

    // The device is the model's projection (see StepSequencerUI): polling it
    // catches every way a pattern can change, without decoding the model's
    // document 30 times a second.
    setPattern(device_->pattern());
    timeBend_.setPlayheadPhase(device_->bendPhase_.load(std::memory_order_relaxed));
    timeBend_.setCycle(device_->bendCycleBeats_.load(std::memory_order_relaxed),
                       device_->bendBeatsPerBar_.load(std::memory_order_relaxed));
    timeBend_.setDefaultDivision(device_->bendStepBeats_.load(std::memory_order_relaxed));

    int step = device_->currentPlayStep_.load(std::memory_order_relaxed);
    if (step != currentPlayStep_) {
        currentPlayStep_ = step;
        patternView_->setPlayStep(step);
    }
}

// =============================================================================
// Layout
// =============================================================================

void PolyStepSequencerUI::setTimeBendShown(bool shown) {
    if (shown == timeBendShown_)
        return;
    timeBendShown_ = shown;
    if (!timeBendLent())
        timeBend_.setVisible(shown);
    resized();
    repaint();
}

int PolyStepSequencerUI::preferredWidth() const {
    return 2 * sf::kInsetX + sf::kStepsWidth +
           (timeBendInFaceplate() ? sf::kStepsGap + sf::kTimeBendWidth : 0);
}

int PolyStepSequencerUI::preferredHeight() {
    return 2 * sf::kInsetY + sf::SettingsStrip::kHeight + sf::kRowGap + sf::kRulerHeight + 2 +
           IDEAL_PATTERN_HEIGHT + sf::kRowGap + 3 + 2 * (sf::kToggleRowHeight + sf::kRowGap) +
           2 * MAX_LANE_HEIGHT + sf::kRowGap;
}

void PolyStepSequencerUI::resized() {
    auto bounds = getLocalBounds().reduced(sf::kInsetX, sf::kInsetY);

    // Out in the arrangement, the host's own button closes it.
    if (bendExpandButton_->isVisible() == timeBendLent()) {
        bendExpandButton_->setVisible(!timeBendLent());
        timeBend_.resized();
    }
    if (timeBendInFaceplate()) {
        timeBend_.setBounds(bounds.removeFromRight(sf::kTimeBendWidth));
        bounds.removeFromRight(sf::kStepsGap);
    }
    dividerX_ = timeBendInFaceplate() ? bounds.getRight() + sf::kStepsGap / 2 : -1;

    strip_.setBounds(bounds.removeFromTop(sf::SettingsStrip::kHeight));
    bounds.removeFromTop(sf::kRowGap);

    rulerArea_ = bounds.removeFromTop(sf::kRulerHeight);
    expandButton_->setBounds(sf::expandButtonBounds(rulerArea_));
    expandButton_->setToggleState(magda::ExpandedFaceplate::getInstance().holds(patternHost_),
                                  juce::dontSendNotification);
    bounds.removeFromTop(2);

    // The bar lanes grow with the room, up to a point; the grid takes the rest.
    const int laneH = juce::jlimit(MIN_LANE_HEIGHT, MAX_LANE_HEIGHT, bounds.getHeight() / 10);
    probabilityArea_ = bounds.removeFromBottom(laneH);
    bounds.removeFromBottom(sf::kRowGap);
    velocityArea_ = bounds.removeFromBottom(laneH);
    bounds.removeFromBottom(sf::kRowGap);
    tieArea_ = bounds.removeFromBottom(sf::kToggleRowHeight);
    bounds.removeFromBottom(sf::kRowGap);
    gateArea_ = bounds.removeFromBottom(sf::kToggleRowHeight);
    bounds.removeFromBottom(sf::kRowGap + 3);

    // Lent to the arrangement, or short of room for the grid, the note boxes stand in.
    if (magda::ExpandedFaceplate::getInstance().holds(patternHost_)) {
        noteArea_ = bounds.removeFromTop(sf::kNoteRowHeight);
        return;
    }
    const bool gridFits = bounds.getHeight() >= MIN_PATTERN_HEIGHT;
    noteArea_ = gridFits ? juce::Rectangle<int>{} : bounds.removeFromTop(sf::kNoteRowHeight);
    patternHost_.setVisible(gridFits);
    if (gridFits)
        patternHost_.setBounds(bounds);
}

void PolyStepSequencerUI::expandablePartReturned() {
    if (patternHost_.getParentComponent() != this)
        addChildComponent(patternHost_);
    if (timeBend_.getParentComponent() != this) {
        addChildComponent(timeBend_);
        timeBend_.setVisible(timeBendShown_);
    }
    resized();
    repaint();
}

bool PolyStepSequencerUI::timeBendLent() const {
    return magda::ExpandedFaceplate::getInstance().holds(timeBend_);
}

bool PolyStepSequencerUI::timeBendInFaceplate() const {
    return timeBendShown_ && !timeBendLent();
}

juce::Component* PolyStepSequencerUI::lentPart() const {
    const auto& expanded = magda::ExpandedFaceplate::getInstance();
    if (expanded.holds(patternHost_))
        return expanded.faceplate();
    return expanded.holds(timeBend_) ? expanded.faceplate() : nullptr;
}

// =============================================================================
// Paint
// =============================================================================

void PolyStepSequencerUI::paint(juce::Graphics& g) {
    if (dividerX_ >= 0) {
        g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
        g.drawVerticalLine(dividerX_, 0.0f, static_cast<float>(getHeight()));
    }

    const int count = pattern_.playingLength();
    const auto columns = [](juce::Rectangle<int> row) {
        return row.withTrimmedLeft(sf::kGutterWidth);
    };
    sf::drawRuler(g, columns(rulerArea_), count, currentPlayStep_);

    const auto label = [&g](juce::Rectangle<int> row, const char* text) {
        sf::drawRowLabel(g, row.withWidth(sf::kGutterWidth), text);
    };
    if (!noteArea_.isEmpty()) {
        label(noteArea_, drumViewActive_ ? "Drums" : "Notes");
        const auto row = columns(noteArea_);
        const float colW = static_cast<float>(row.getWidth()) / static_cast<float>(count);
        const auto font = FontManager::getInstance().getMonoFont(colW < 30.0f ? 9.5f : 11.5f);
        g.setFont(font);
        const auto lanes =
            drumViewActive_ ? drumLanesFor(devicePath_, pattern_) : std::vector<DrumLane>{};
        for (int i = 0; i < count; ++i) {
            const auto& step = pattern_.step(i);
            const auto cell = sf::cellBounds(row, i, count);
            sf::drawNoteCell(g, cell,
                             {.text = noteCellText(i, lanes, font, cell.getWidth() - 4.0f),
                              .sounds = step.gate && !(step.tie && i > 0),
                              .playing = i == currentPlayStep_});
        }
    }
    label(gateArea_, "Gate");
    sf::drawToggleRow(g, columns(gateArea_), count, ActiveTheme::DEVICE_GREEN,
                      [this](int i) { return pattern_.step(i).gate; });
    label(tieArea_, "Tie");
    sf::drawToggleRow(g, columns(tieArea_), count, ActiveTheme::DEVICE_BLUE,
                      [this](int i) { return pattern_.step(i).tie; });
    label(velocityArea_, "Velocity");
    sf::drawBarRow(g, columns(velocityArea_), count, ActiveTheme::DEVICE_BLUE, [this](int i) {
        return static_cast<float>(pattern_.step(i).velocity) / 127.0f;
    });
    label(probabilityArea_, "Prob");
    sf::drawBarRow(g, columns(probabilityArea_), count, ActiveTheme::DEVICE_VIOLET,
                   [this](int i) { return pattern_.step(i).probability; });
}

// =============================================================================
// Mouse interaction (per-step rows; the grid handles its own mouse)
// =============================================================================

void PolyStepSequencerUI::applyLaneDrag(const juce::MouseEvent& e) {
    if (activeDragLane_ == DragLane::None)
        return;

    const auto area = (activeDragLane_ == DragLane::Velocity ? velocityArea_ : probabilityArea_)
                          .withTrimmedLeft(sf::kGutterWidth);
    const int step = sf::stepAt(juce::jlimit(area.getX(), area.getRight() - 1, e.x), area,
                                pattern_.playingLength());
    if (step < 0)
        return;

    const float ratio =
        juce::jlimit(0.0f, 1.0f,
                     1.0f - static_cast<float>(e.y - area.getY()) /
                                static_cast<float>(juce::jmax(1, area.getHeight())));

    if (activeDragLane_ == DragLane::Velocity) {
        const int velocity = juce::jlimit(1, 127, juce::roundToInt(ratio * 127.0f));
        editPattern(
            "Set Step Velocity",
            [step, velocity](step_pattern::PolyPattern& p) {
                p.steps[static_cast<size_t>(step)].velocity = velocity;
            },
            magda::StepPatternGesture::Continuous);
    } else {
        editPattern(
            "Set Step Probability",
            [step, ratio](step_pattern::PolyPattern& p) {
                p.steps[static_cast<size_t>(step)].probability = ratio;
            },
            magda::StepPatternGesture::Continuous);
    }
    repaint();
}

void PolyStepSequencerUI::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    const int count = pattern_.playingLength();
    const auto stepIn = [&](juce::Rectangle<int> row) {
        return row.contains(pos) ? sf::stepAt(pos.x, row.withTrimmedLeft(sf::kGutterWidth), count)
                                 : -1;
    };

    // Over the steps' settings row or ruler, the menu holds the settings that have no box.
    if (e.mods.isPopupMenu() && pos.y < rulerArea_.getBottom()) {
        showSettingsMenu();
        return;
    }

    if (const int step = stepIn(noteArea_); step >= 0) {
        openStepPicker(step);
        return;
    }
    if (const int step = stepIn(gateArea_); step >= 0) {
        editPattern("Toggle Step Gate", [step](step_pattern::PolyPattern& p) {
            auto& target = p.steps[static_cast<size_t>(step)];
            target.gate = !target.gate;
        });
        repaint();
        return;
    }
    if (const int step = stepIn(tieArea_); step >= 0) {
        editPattern("Toggle Step Tie", [step](step_pattern::PolyPattern& p) {
            auto& target = p.steps[static_cast<size_t>(step)];
            target.tie = !target.tie;
        });
        repaint();
        return;
    }

    if (velocityArea_.contains(pos))
        activeDragLane_ = DragLane::Velocity;
    else if (probabilityArea_.contains(pos))
        activeDragLane_ = DragLane::Probability;
    applyLaneDrag(e);
}

void PolyStepSequencerUI::mouseDrag(const juce::MouseEvent& e) {
    applyLaneDrag(e);
}

void PolyStepSequencerUI::mouseUp(const juce::MouseEvent&) {
    // The drag is over, so the next one starts its own undo entry rather than
    // merging into the run this one built (#2335).
    if (activeDragLane_ != DragLane::None)
        endPatternGesture();
    activeDragLane_ = DragLane::None;
}

namespace {

/// A chord's name short enough for a step box: a root and a compact quality, or nothing.
juce::String shortChordName(const sdk::sequencer::PolyStep& step) {
    std::vector<music::ChordNote> notes;
    for (int n = 0; n < step.noteCount; ++n)
        notes.emplace_back(step.notes[static_cast<size_t>(n)].noteNumber, 100);
    const auto chord = music::ChordEngine::detect(notes);
    if (chord.name.isEmpty() || chord.name == "none" || chord.name == "unknown" ||
        !chord.exactMatch || !chord.extraPitchClasses.empty())
        return {};

    using Q = music::ChordQuality;
    static const std::map<Q, const char*> suffixes = {
        {Q::Major, ""},     {Q::Minor, "m"},     {Q::Diminished, "dim"}, {Q::Augmented, "+"},
        {Q::Power, "5"},    {Q::Sus2, "sus2"},   {Q::Sus4, "sus4"},      {Q::Major7, "M7"},
        {Q::Minor7, "m7"},  {Q::Dominant7, "7"}, {Q::Major9, "M9"},      {Q::Minor9, "m9"},
        {Q::Dominant9, "9"}};
    const auto suffix = suffixes.find(chord.quality);
    if (suffix == suffixes.end())
        return {};
    return music::ChordUtils::rootToString(chord.root) + suffix->second;
}

}  // namespace

juce::String PolyStepSequencerUI::noteCellText(int index, const std::vector<DrumLane>& lanes,
                                               const juce::Font& font, float width) const {
    const auto& step = pattern_.step(index);
    if (step.noteCount == 0)
        return juce::String::fromUTF8("\xc2\xb7");

    const auto fits = [&font, width](const juce::String& text) {
        return text.isNotEmpty() && juce::GlyphArrangement::getStringWidth(font, text) <= width;
    };
    const auto count = juce::String(step.noteCount);
    if (drumViewActive_) {
        if (step.noteCount > 1)
            return count;
        const int note = step.notes[0].noteNumber;
        for (const auto& lane : lanes)
            if (lane.note == note && fits(lane.label))
                return lane.label;
        return count;
    }
    if (step.noteCount == 1)
        return sf::noteName(step.notes[0].noteNumber);
    const auto chord = shortChordName(step);
    return fits(chord) ? chord : count;
}

void PolyStepSequencerUI::toggleNote(int stepIndex, int note) {
    editPattern("Toggle Step Note", [stepIndex, note](step_pattern::PolyPattern& p) {
        toggleStepNote(p, stepIndex, note);
    });
}

void PolyStepSequencerUI::openStepPicker(int stepIndex) {
    juce::Component::SafePointer<PolyStepSequencerUI> self(this);
    const auto isLit = [self, stepIndex](int note) {
        return self != nullptr && noteVelocity(self->pattern_.step(stepIndex), note) > 0;
    };
    const auto onNote = [self, stepIndex](int note) {
        if (self != nullptr)
            self->toggleNote(stepIndex, note);
    };
    const auto box = sf::cellBounds(noteArea_.withTrimmedLeft(sf::kGutterWidth), stepIndex,
                                    pattern_.playingLength())
                         .toNearestInt();

    if (drumViewActive_) {
        std::vector<sf::DrumPicker::Drum> drums;
        for (const auto& lane : drumLanesFor(devicePath_, pattern_))
            drums.push_back({lane.note, lane.label});
        juce::CallOutBox::launchAsynchronously(
            std::make_unique<sf::DrumPicker>(std::move(drums), isLit, onNote),
            localAreaToGlobal(box), nullptr);
        return;
    }

    const auto& step = pattern_.step(stepIndex);
    const int first = step.noteCount > 0 ? step.notes[0].noteNumber : 60;
    sf::NotePicker::show(*this, box, std::make_unique<sf::NotePicker>(first, isLit, onNote));
}

// =============================================================================
// Settings menu
// =============================================================================

void PolyStepSequencerUI::showSettingsMenu() {
    using Safe = juce::Component::SafePointer<PolyStepSequencerUI>;
    juce::PopupMenu menu;
    menu.addCustomItem(1, std::make_unique<sf::ValueMenuItem>(
                              "Quantize", [safe = Safe(this)](LinkableTextSlider& box) {
                                  box.setRange(0.0, 1.0, 0.01);
                                  box.setValueFormatter([](double v) {
                                      return juce::String(juce::roundToInt(v * 100.0)) + "%";
                                  });
                                  box.setValueParser([](const juce::String& t) {
                                      return t.trimCharactersAtEnd("%").getDoubleValue() / 100.0;
                                  });
                                  box.setValue(safe->quantize_, juce::dontSendNotification);
                                  box.onValueChanged = [safe](double v) {
                                      if (safe == nullptr)
                                          return;
                                      safe->quantize_ = v;
                                      safe->settingsEdited();
                                  };
                              }));
    menu.addCustomItem(
        2, std::make_unique<sf::ValueMenuItem>("Quantize Sub", [safe = Safe(this)](
                                                                   LinkableTextSlider& box) {
            box.setRange(16, 512, 16);
            box.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
            box.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
            box.setValue(safe->quantizeSub_, juce::dontSendNotification);
            box.onValueChanged = [safe](double v) {
                if (safe == nullptr)
                    return;
                safe->quantizeSub_ = juce::roundToInt(v);
                safe->settingsEdited();
            };
        }));
    menu.showMenuAsync(juce::PopupMenu::Options());
}

std::vector<LinkableTextSlider*> PolyStepSequencerUI::getLinkableSliders() {
    magda::ChainNodePath dummy;
    const std::pair<LinkableTextSlider*, int> linked[] = {
        {&strip_.rate, PolySeqPlugin::kRate},
        {&strip_.gate, PolySeqPlugin::kGateLength},
        {&timeBend_.amountSlider(), PolySeqPlugin::kBendAmount},
        {&timeBend_.cyclesSlider(), PolySeqPlugin::kBendCycles}};
    std::vector<LinkableTextSlider*> sliders;
    for (const auto& [slider, slot] : linked) {
        slider->setLinkContext(magda::INVALID_DEVICE_ID, slot, dummy);
        sliders.push_back(slider);
    }
    return sliders;
}

}  // namespace magda::daw::ui
