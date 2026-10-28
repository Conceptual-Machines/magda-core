// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "custom_ui/PolyStepSequencerUI.hpp"

#include <algorithm>

#include "core/GestureRouter.hpp"
#include "core/RackInfo.hpp"
#include "core/TrackManager.hpp"

namespace magda::daw::ui {

using PolySeqPlugin = daw::audio::PolyStepSequencerPlugin;
namespace sf = sequencer_faceplate;

std::atomic<int> PolyStepSequencerUI::nextPatternGesture_{magda::kNoStepPatternGesture + 1};

namespace {

bool isBlackKey(int noteNumber) {
    static const bool isBlack[] = {false, true,  false, true,  false, false,
                                   true,  false, true,  false, true,  false};
    return isBlack[((noteNumber % 12) + 12) % 12];
}

enum class Glyph { Up, Down, Plus, Minus };

/// Two stacked buttons in one well, as the grid's octave and zoom controls.
void drawButtonPair(juce::Graphics& g, juce::Rectangle<int> box, Glyph top, bool topEnabled,
                    Glyph bottom, bool bottomEnabled) {
    if (box.isEmpty())
        return;
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
    g.fillRoundedRectangle(box.toFloat(), 4.0f);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_FIELD_BORDER));
    g.drawRoundedRectangle(box.toFloat().reduced(0.5f), 4.0f, 1.0f);

    const auto draw = [&g](juce::Rectangle<int> half, Glyph glyph, bool enabled) {
        g.setColour(ActiveTheme::getColour(enabled ? ActiveTheme::DEVICE_TEXT
                                                   : ActiveTheme::DEVICE_ICON_INACTIVE));
        const auto c = half.getCentre().toFloat();
        constexpr float r = 4.0f;
        juce::Path path;
        switch (glyph) {
            case Glyph::Up:
                path.addTriangle(c.x - r, c.y + r * 0.6f, c.x + r, c.y + r * 0.6f, c.x,
                                 c.y - r * 0.7f);
                g.fillPath(path);
                break;
            case Glyph::Down:
                path.addTriangle(c.x - r, c.y - r * 0.6f, c.x + r, c.y - r * 0.6f, c.x,
                                 c.y + r * 0.7f);
                g.fillPath(path);
                break;
            case Glyph::Plus:
                g.drawLine(c.x, c.y - r, c.x, c.y + r, 1.4f);
                [[fallthrough]];
            case Glyph::Minus:
                g.drawLine(c.x - r, c.y, c.x + r, c.y, 1.4f);
                break;
        }
    };
    draw(box.removeFromTop(box.getHeight() / 2), top, topEnabled);
    draw(box, bottom, bottomEnabled);
}

/// The grid's floor and lines: a heavier line every four steps, a dimmed column for a rest.
void drawGridFloor(juce::Graphics& g, juce::Rectangle<int> cells,
                   const step_pattern::PolyPattern& pattern, int rows,
                   const std::function<bool(int row)>& isDarkRow) {
    const int count = pattern.playingLength();
    const float colW = static_cast<float>(cells.getWidth()) / static_cast<float>(count);
    const float rowH = static_cast<float>(cells.getHeight()) / static_cast<float>(rows);

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_BG));
    g.fillRect(cells);
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_WELL));
    for (int row = 0; row < rows; ++row)
        if (isDarkRow(row))
            g.fillRect(juce::Rectangle<float>(static_cast<float>(cells.getX()),
                                              cells.getY() + row * rowH,
                                              static_cast<float>(cells.getWidth()), rowH));
    for (int i = 0; i < count; ++i) {
        if (pattern.step(i).gate)
            continue;
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.fillRect(juce::Rectangle<float>(cells.getX() + i * colW, static_cast<float>(cells.getY()),
                                          colW, static_cast<float>(cells.getHeight())));
    }

    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GRAPH_GRID));
    for (int row = 1; row < rows; ++row)
        g.drawHorizontalLine(juce::roundToInt(cells.getY() + row * rowH),
                             static_cast<float>(cells.getX()),
                             static_cast<float>(cells.getRight()));
    for (int i = 1; i < count; ++i) {
        g.setColour(ActiveTheme::getColour(i % 4 == 0 ? ActiveTheme::DEVICE_LINE
                                                      : ActiveTheme::DEVICE_GRAPH_GRID));
        g.drawVerticalLine(juce::roundToInt(cells.getX() + i * colW),
                           static_cast<float>(cells.getY()), static_cast<float>(cells.getBottom()));
    }
}

/// The playing step's left edge, a lit line down the grid.
void drawPlayhead(juce::Graphics& g, juce::Rectangle<int> cells, int count, int playStep) {
    if (playStep < 0 || playStep >= count)
        return;
    const float x = cells.getX() + playStep * static_cast<float>(cells.getWidth()) / count;
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_GREEN));
    g.fillRect(juce::Rectangle<float>(x, static_cast<float>(cells.getY()), 1.5f,
                                      static_cast<float>(cells.getHeight())));
}

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

/// A hit as a bar across its steps, lit by its velocity; a held tie's own notes only outlined.
void drawNoteBar(juce::Graphics& g, juce::Rectangle<float> bar, int velocity, bool sounds) {
    const auto green = ActiveTheme::getColour(ActiveTheme::DEVICE_GREEN);
    if (!sounds) {
        g.setColour(green.withAlpha(0.45f));
        g.drawRoundedRectangle(bar.reduced(0.5f), 2.5f, 1.0f);
        return;
    }
    g.setColour(green.withAlpha(0.45f + 0.55f * static_cast<float>(velocity) / 127.0f));
    g.fillRoundedRectangle(bar, 2.5f);
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

        drawButtonPair(g, octaveUpArea_.getUnion(octaveDownArea_), Glyph::Up,
                       lowNote_ < 127 - visibleNotes_ + 1, Glyph::Down, lowNote_ > 0);
        drawButtonPair(g, zoomInArea_.getUnion(zoomOutArea_), Glyph::Plus,
                       visibleNotes_ > MIN_VISIBLE_NOTES, Glyph::Minus,
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
            g.setColour(isBlackKey(note) ? sf::kBlackKey : sf::kWhiteKey);
            g.fillRect(key.reduced(0.0f, 0.5f));
            if (note % 12 == 0) {
                g.setColour(sf::kBlackKey.withAlpha(0.7f));
                g.drawText(sf::noteName(note), key.withTrimmedLeft(3.0f).toNearestInt(),
                           juce::Justification::centredLeft, false);
            }
        }

        drawGridFloor(g, cellArea_, pat, visibleNotes_,
                      [&](int row) { return isBlackKey(noteOfRow(row)); });

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
                drawNoteBar(g, bar, noteVelocity(step, note), step.gate && !held);
            }
        }

        drawPlayhead(g, cellArea_, count, playStep_);
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
        drawButtonPair(g, scrollUpArea_.getUnion(scrollDownArea_), Glyph::Up,
                       scrollOffset_ < maxOffset, Glyph::Down, scrollOffset_ > 0);

        const auto& pat = pattern();
        const int count = pat.playingLength();
        const float colW = static_cast<float>(cellArea_.getWidth()) / static_cast<float>(count);
        const auto laneOfRow = [&](int row) { return scrollOffset_ + (visible - 1 - row); };

        drawGridFloor(g, cellArea_, pat, visible, [&](int row) { return laneOfRow(row) % 2 == 1; });

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
                    drawNoteBar(g,
                                juce::Rectangle<float>(cellArea_.getX() + i * colW, y, colW, laneH)
                                    .reduced(juce::jmin(3.0f, colW * 0.1f), 3.0f),
                                velocity, step.gate && !(step.tie && i > 0));
            }
        }

        drawPlayhead(g, cellArea_, count, playStep_);
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

    struct Lane {
        int note = 60;        // MIDI note that triggers this lane's sound
        juce::String label;   // Chain name / GM name / note name
        bool orphan = false;  // Pattern data with no matching Drum Grid chain
    };

    // --- Lane discovery ---

    /** Rebuild the lane list: Drum Grid chains (or GM fallback) + orphan
     *  lanes for pattern notes that match no chain. Sorted note-ascending so
     *  the lowest lane paints at the bottom. */
    void refreshLanes() {
        const auto* drumGrid =
            magda::TrackManager::getInstance().findPadDeviceDownstreamOf(context_.devicePath);

        lanes_.clear();
        if (drumGrid != nullptr && drumGrid->pads) {
            for (const auto& pad : drumGrid->pads->chains) {
                // The pad's low note is the incoming MIDI note that triggers
                // it (single-note pads have lowNote == highNote).
                Lane lane;
                lane.note = pad.lowNote;
                lane.label = pad.name.isNotEmpty() ? pad.name : sf::noteName(pad.lowNote);
                lanes_.push_back(std::move(lane));
            }
        }

        if (lanes_.empty()) {
            // No Drum Grid downstream (or it has no pads yet): GM drum map
            static const struct {
                int note;
                const char* name;
            } gmLanes[] = {{36, "Kick"}, {38, "Snare"}, {42, "CHat"},  {46, "OHat"},
                           {41, "LTom"}, {47, "MTom"},  {49, "Crash"}, {51, "Ride"}};
            for (const auto& gm : gmLanes)
                lanes_.push_back({gm.note, gm.name, false});
        }

        // Pattern notes with no matching lane stay visible and editable
        for (int i = 0; i < PolySeqPlugin::MAX_STEPS; ++i) {
            const auto& step = pattern().step(i);
            for (int n = 0; n < step.noteCount; ++n) {
                const int note = step.notes[static_cast<size_t>(n)].noteNumber;
                if (!hasLaneForNote(note))
                    lanes_.push_back({note, sf::noteName(note), true});
            }
        }

        std::ranges::sort(lanes_, {}, &Lane::note);
        const auto duplicates = std::ranges::unique(lanes_, {}, &Lane::note);
        lanes_.erase(duplicates.begin(), duplicates.end());

        clampScrollOffset();
        repaint();
    }

    bool hasLaneForNote(int note) const {
        const auto hasNote = [note](const auto& lane) { return lane.note == note; };
        return std::ranges::any_of(lanes_, hasNote);
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

// =============================================================================
// Construction
// =============================================================================

PolyStepSequencerUI::PolyStepSequencerUI() {
    panel_.rate.onValueChanged = [this](double value) {
        sendChange(PolySeqPlugin::kRate, static_cast<float>(juce::roundToInt(value)));
    };
    panel_.steps.onValueChanged = [this](double value) {
        const int steps = juce::jlimit(1, PolySeqPlugin::MAX_STEPS, juce::roundToInt(value));
        // How many steps play is part of the pattern, so it is a pattern edit. A drag coalesces
        // into one undo entry; a typed or clicked value stays discrete (#2335).
        editPattern(
            "Set Step Count", [steps](step_pattern::PolyPattern& p) { p.length = steps; },
            panel_.steps.isBeingDragged() ? magda::StepPatternGesture::Continuous
                                          : magda::StepPatternGesture::Discrete);
        repaint();
    };
    panel_.steps.getSlider().onDragEnd = [this] { endPatternGesture(); };
    panel_.swing.onValueChanged = [this](double value) {
        sendChange(PolySeqPlugin::kSwing, static_cast<float>(value));
    };
    panel_.gate.onValueChanged = [this](double value) {
        sendChange(PolySeqPlugin::kGateLength, static_cast<float>(value));
    };
    panel_.direction.onChange = [this] {
        sendChange(PolySeqPlugin::kDirection,
                   static_cast<float>(panel_.direction.getSelectedId() - 1));
    };
    panel_.quantize.onValueChanged = [this](double) { settingsEdited(); };
    panel_.subdivision.onValueChanged = [this](double) { settingsEdited(); };

    panel_.bendAmount.onValueChanged = [this](double value) {
        auto bend = panel_.timeBend.getBend();
        bend.amount = static_cast<float>(value);
        panel_.timeBend.setBend(bend);
        sendChange(PolySeqPlugin::kBendAmount, bend.amount);
    };
    panel_.bendCycles.onValueChanged = [this](double value) {
        auto bend = panel_.timeBend.getBend();
        bend.cycles = juce::roundToInt(value);
        panel_.timeBend.setBend(bend);
        sendChange(PolySeqPlugin::kBendCycles, static_cast<float>(bend.cycles));
    };
    panel_.timeBend.onCommit = [this](const auto& before, const auto& after) {
        timeBendEdited(before, after);
    };

    viewChoice_.setOptions({"Keys", "Drums"});
    viewChoice_.setSelectedIndex(0);
    viewChoice_.onChange = [this](int index) {
        drumViewActive_ = index == 1;
        settingsEdited();
        applyPatternViewMode();
    };
    panel_.addExtraTile("View", viewChoice_, 2);
    addAndMakeVisible(panel_);

    // MIDI thru and step record live in the device-slot header.

    patternView_ = std::make_unique<KeysView>();
    addAndMakeVisible(*patternView_);
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

    panel_.rate.setValue(static_cast<double>(display(PolySeqPlugin::kRate, 7.0f)),
                         juce::dontSendNotification);
    panel_.direction.setSelectedId(juce::roundToInt(display(PolySeqPlugin::kDirection, 0.0f)) + 1,
                                   juce::dontSendNotification);
    panel_.swing.setValue(static_cast<double>(display(PolySeqPlugin::kSwing, 0.0f)),
                          juce::dontSendNotification);
    panel_.gate.setValue(static_cast<double>(display(PolySeqPlugin::kGateLength, 0.8f)),
                         juce::dontSendNotification);

    auto bend = panel_.timeBend.getBend();
    bend.amount = display(PolySeqPlugin::kBendAmount, 1.0f);
    bend.cycles = juce::roundToInt(display(PolySeqPlugin::kBendCycles, 1.0f));
    panel_.timeBend.setBend(bend);
    panel_.bendAmount.setValue(bend.amount, juce::dontSendNotification);
    panel_.bendCycles.setValue(bend.cycles, juce::dontSendNotification);

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
    panel_.steps.setValue(static_cast<double>(steps), juce::dontSendNotification);

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
    bend.amount = panel_.timeBend.getBend().amount;
    bend.cycles = panel_.timeBend.getBend().cycles;
    panel_.timeBend.setBend(bend);
    panel_.quantize.setValue(static_cast<double>(device_->quantize.load(std::memory_order_relaxed)),
                             juce::dontSendNotification);
    panel_.subdivision.setValue(
        static_cast<double>(device_->quantizeSub.load(std::memory_order_relaxed)),
        juce::dontSendNotification);
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
                 juce::String(sdk::sequencer::timeBendToText(panel_.timeBend.getBend())));
    settings.set(IDs::quantize, static_cast<float>(panel_.quantize.getValue()));
    settings.set(IDs::quantizeSub, juce::roundToInt(panel_.subdivision.getValue()));
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
        addAndMakeVisible(*patternView_);
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
    panel_.timeBend.setPlayheadPhase(device_->bendPhase_.load(std::memory_order_relaxed));
    panel_.timeBend.setCycle(device_->bendCycleBeats_.load(std::memory_order_relaxed),
                             device_->bendBeatsPerBar_.load(std::memory_order_relaxed));
    panel_.timeBend.setDefaultDivision(device_->bendStepBeats_.load(std::memory_order_relaxed));

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
    if (shown == panel_.showsTimeBend())
        return;
    panel_.setShowsTimeBend(shown);
    resized();
    repaint();
}

int PolyStepSequencerUI::preferredWidth() const {
    return 2 * sf::kInsetX + sf::kStepsWidth + sf::kStepsGap + panel_.preferredWidth();
}

void PolyStepSequencerUI::resized() {
    auto bounds = getLocalBounds().reduced(sf::kInsetX, sf::kInsetY);

    panel_.setBounds(bounds.removeFromRight(panel_.preferredWidth()));
    bounds.removeFromRight(sf::kStepsGap);
    dividerX_ = bounds.getRight() + sf::kStepsGap / 2;

    rulerArea_ = bounds.removeFromTop(sf::kRulerHeight);
    bounds.removeFromTop(4);

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

    patternView_->setBounds(bounds);
}

// =============================================================================
// Paint
// =============================================================================

void PolyStepSequencerUI::paint(juce::Graphics& g) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::DEVICE_LINE));
    g.drawVerticalLine(dividerX_, 0.0f, static_cast<float>(getHeight()));

    const int count = pattern_.playingLength();
    const auto columns = [](juce::Rectangle<int> row) {
        return row.withTrimmedLeft(sf::kGutterWidth);
    };
    sf::drawRuler(g, columns(rulerArea_), count, currentPlayStep_);

    const auto label = [&g](juce::Rectangle<int> row, const char* text) {
        sf::drawRowLabel(g, row.withWidth(sf::kGutterWidth), text);
    };
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

std::vector<LinkableTextSlider*> PolyStepSequencerUI::getLinkableSliders() {
    magda::ChainNodePath dummy;
    const std::pair<LinkableTextSlider*, int> linked[] = {
        {&panel_.rate, PolySeqPlugin::kRate},
        {&panel_.swing, PolySeqPlugin::kSwing},
        {&panel_.gate, PolySeqPlugin::kGateLength},
        {&panel_.bendAmount, PolySeqPlugin::kBendAmount},
        {&panel_.bendCycles, PolySeqPlugin::kBendCycles}};
    std::vector<LinkableTextSlider*> sliders;
    for (const auto& [slider, slot] : linked) {
        slider->setLinkContext(magda::INVALID_DEVICE_ID, slot, dummy);
        sliders.push_back(slider);
    }
    return sliders;
}

}  // namespace magda::daw::ui
