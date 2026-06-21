#include "custom_ui/PolyStepSequencerUI.hpp"

#include "audio/plugins/DrumGridPlugin.hpp"
#include "audio/transport/StepClock.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

namespace te = tracktion::engine;

using PolySeqPlugin = daw::audio::PolyStepSequencerPlugin;

namespace {

// Drum Grid chain child type (mirrors DrumGridPlugin's private chainTreeId)
const juce::Identifier DRUM_CHAIN_TYPE("CHAIN");

const char* POLY_NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

juce::String polyNoteNameShort(int noteNumber) {
    if (noteNumber < 0 || noteNumber > 127)
        return "-";
    int octave = (noteNumber / 12) - 2;
    return juce::String(POLY_NOTE_NAMES[noteNumber % 12]) + juce::String(octave);
}

bool isBlackKey(int noteNumber) {
    static const bool isBlack[] = {false, true,  false, true,  false, false,
                                   true,  false, true,  false, true,  false};
    return isBlack[((noteNumber % 12) + 12) % 12];
}

constexpr int kStepRulerHeight = 13;  // mini step ruler above the grid

// Step ruler above the grid: a tick per step, heavier + numbered every 4, with
// the playing step highlighted. Columns align with the cell grid. Shared by the
// keys and drum-lane views.
void drawStepRuler(juce::Graphics& g, juce::Rectangle<int> timelineArea,
                   juce::Rectangle<int> cellArea, int count, float colW, int playStep) {
    if (timelineArea.isEmpty())
        return;

    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.04f));
    g.fillRect(timelineArea);

    const float top = static_cast<float>(timelineArea.getY());
    const float bottom = static_cast<float>(timelineArea.getBottom());

    // Highlight the playing step.
    if (playStep >= 0 && playStep < count) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.45f));
        g.fillRect(
            juce::Rectangle<float>(cellArea.getX() + playStep * colW, top, colW, bottom - top));
    }

    g.setFont(FontManager::getInstance().getUIFont(8.0f));
    for (int i = 0; i < count; ++i) {
        const float x = cellArea.getX() + i * colW;
        const bool group = (i % 4 == 0);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(group ? 0.5f : 0.2f));
        g.drawVerticalLine(juce::roundToInt(x), group ? top : top + 4.0f, bottom);
        if (group) {
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.drawText(
                juce::String(i + 1),
                juce::Rectangle<float>(x + 2.0f, top, colW - 2.0f, bottom - top).toNearestInt(),
                juce::Justification::centredLeft);
        }
    }

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.4f));
    g.drawHorizontalLine(timelineArea.getBottom() - 1, static_cast<float>(cellArea.getX()),
                         static_cast<float>(cellArea.getRight()));
}

}  // namespace

// =============================================================================
// KeysView — piano-roll style pitch x step grid
// =============================================================================

class PolyStepSequencerUI::KeysView : public PolyStepSequencerUI::PatternView {
  public:
    KeysView() = default;

    void setPlugin(PolySeqPlugin* plugin) override {
        plugin_ = plugin;
        repaint();
    }

    void setPlayStep(int step) override {
        if (step != playStep_) {
            playStep_ = step;
            repaint();
        }
    }

    void patternChanged() override {
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        if (bounds.isEmpty() || plugin_ == nullptr)
            return;

        // Mini timeline (step ruler) across the top, above the whole grid.
        timelineArea_ = bounds.removeFromTop(kStepRulerHeight);

        auto arrowStrip = bounds.removeFromLeft(ARROW_STRIP_WIDTH);
        // Left strip, top to bottom: zoom in (+), octave up, octave down,
        // zoom out (-).
        const int cellH = arrowStrip.getHeight() / 4;
        zoomInArea_ = arrowStrip.removeFromTop(cellH);
        octaveUpArea_ = arrowStrip.removeFromTop(cellH);
        octaveDownArea_ = arrowStrip.removeFromTop(cellH);
        zoomOutArea_ = arrowStrip;
        gutterArea_ = bounds.removeFromLeft(LEFT_GUTTER_WIDTH - ARROW_STRIP_WIDTH);
        cellArea_ = bounds;

        drawZoomButton(g, zoomInArea_, true);
        drawOctaveArrow(g, octaveUpArea_, true);
        drawOctaveArrow(g, octaveDownArea_, false);
        drawZoomButton(g, zoomOutArea_, false);

        const int count = juce::jlimit(1, PolySeqPlugin::MAX_STEPS, plugin_->numSteps.get());
        const float rowH = static_cast<float>(cellArea_.getHeight()) / visibleNotes_;
        const float colW = static_cast<float>(cellArea_.getWidth()) / static_cast<float>(count);

        drawStepRuler(g, timelineArea_, cellArea_, count, colW, playStep_);

        g.setFont(FontManager::getInstance().getUIFont(7.0f));

        // --- Pitch gutter: piano-key style rows, note names on C's ---
        for (int row = 0; row < visibleNotes_; ++row) {
            const int note = lowNote_ + (visibleNotes_ - 1 - row);
            auto rowRect = juce::Rectangle<float>(static_cast<float>(gutterArea_.getX()),
                                                  gutterArea_.getY() + row * rowH,
                                                  static_cast<float>(gutterArea_.getWidth()), rowH);

            g.setColour(isBlackKey(note) ? juce::Colours::black.withAlpha(0.75f)
                                         : juce::Colours::white.withAlpha(0.75f));
            g.fillRect(rowRect.reduced(0.0f, 0.5f));

            if (note % 12 == 0) {
                g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
                g.drawText(polyNoteNameShort(note), rowRect.toNearestInt(),
                           juce::Justification::centred);
            }
        }

        // --- Cells ---
        for (int row = 0; row < visibleNotes_; ++row) {
            const int note = lowNote_ + (visibleNotes_ - 1 - row);
            const float y = cellArea_.getY() + row * rowH;

            for (int i = 0; i < count; ++i) {
                const auto step = plugin_->getStep(i);
                const float x = cellArea_.getX() + i * colW;
                auto cellRect =
                    juce::Rectangle<float>(x + 0.5f, y + 0.5f, colW - 1.0f, rowH - 1.0f);

                // Background: black-key rows darker, playhead column highlighted
                juce::Colour bg = DarkTheme::getColour(DarkTheme::BACKGROUND)
                                      .brighter(isBlackKey(note) ? 0.04f : 0.10f);
                if (i == playStep_)
                    bg = bg.overlaidWith(
                        DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.18f));
                if (!step.gate)
                    bg = bg.darker(0.3f);
                g.setColour(bg);
                g.fillRect(cellRect);

                // Active note: opacity from effective velocity (per-note
                // override, falling back to step velocity)
                int noteVel = 0;
                for (int n = 0; n < step.noteCount; ++n) {
                    const auto& sn = step.notes[static_cast<size_t>(n)];
                    if (sn.noteNumber == note) {
                        noteVel = sn.velocity > 0 ? sn.velocity : step.velocity;
                        break;
                    }
                }
                if (noteVel > 0) {
                    const float alpha = 0.35f + 0.6f * static_cast<float>(noteVel) / 127.0f;
                    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(alpha));
                    g.fillRoundedRectangle(cellRect, 1.5f);
                }
            }
        }

        // --- Grid lines: heavier every 4 steps and on C-row boundaries ---
        for (int i = 0; i <= count; ++i) {
            const float x = cellArea_.getX() + i * colW;
            g.setColour(
                DarkTheme::getColour(DarkTheme::BORDER).withAlpha(i % 4 == 0 ? 0.4f : 0.15f));
            g.drawVerticalLine(juce::roundToInt(x), static_cast<float>(cellArea_.getY()),
                               static_cast<float>(cellArea_.getBottom()));
        }
        for (int row = 0; row <= visibleNotes_; ++row) {
            const int noteBelow = lowNote_ + (visibleNotes_ - 1 - row);
            const float y = cellArea_.getY() + row * rowH;
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER)
                            .withAlpha((noteBelow + 1) % 12 == 0 ? 0.4f : 0.1f));
            g.drawHorizontalLine(juce::roundToInt(y), static_cast<float>(cellArea_.getX()),
                                 static_cast<float>(cellArea_.getRight()));
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (plugin_ == nullptr)
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

        plugin_->toggleStepNote(step, note);
        repaint();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        // Modifier + wheel zooms the pitch axis; plain wheel scrolls it.
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
            zoomBy(wheel.deltaY > 0 ? -2 : 2);  // scroll up = zoom in (fewer rows)
        else
            shiftWindow(wheel.deltaY > 0 ? 2 : -2);
    }

  private:
    static constexpr int DEFAULT_VISIBLE_NOTES = 24;  // ~2 octaves
    static constexpr int MIN_VISIBLE_NOTES = 12;      // zoomed in: one octave
    static constexpr int MAX_VISIBLE_NOTES = 48;      // zoomed out: four octaves
    static constexpr int ARROW_STRIP_WIDTH = 14;

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
        if (plugin_ == nullptr || cellArea_.getWidth() <= 0)
            return -1;
        const int count = juce::jlimit(1, PolySeqPlugin::MAX_STEPS, plugin_->numSteps.get());
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
        const auto step = plugin_->getStep(stepIndex);

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

        menu.showMenuAsync(juce::PopupMenu::Options(), [this, stepIndex](int result) {
            if (plugin_ == nullptr)
                return;
            switch (result) {
                case 1: {
                    auto s = plugin_->getStep(stepIndex);
                    plugin_->setStepGate(stepIndex, !s.gate);
                    break;
                }
                case 2:
                    plugin_->clearStep(stepIndex);
                    break;
                case 10:
                    if (plugin_->transposePattern(1))
                        shiftWindow(1);
                    break;
                case 11:
                    if (plugin_->transposePattern(-1))
                        shiftWindow(-1);
                    break;
                case 12:
                    if (plugin_->transposePattern(12))
                        shiftWindow(12);
                    break;
                case 13:
                    if (plugin_->transposePattern(-12))
                        shiftWindow(-12);
                    break;
                case 20:
                    plugin_->clearPattern();
                    break;
                default:
                    return;
            }
            repaint();
        });
    }

    void drawOctaveArrow(juce::Graphics& g, juce::Rectangle<int> area, bool isUp) {
        if (area.isEmpty())
            return;

        auto btn = area.reduced(2);
        const bool canShift = isUp ? (lowNote_ < 127 - visibleNotes_ + 1) : (lowNote_ > 0);

        g.setColour(canShift ? DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.15f)
                             : DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
        g.fillRoundedRectangle(btn.toFloat(), 2.0f);

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
        g.drawRoundedRectangle(btn.toFloat(), 2.0f, 0.5f);

        g.setColour(canShift ? DarkTheme::getTextColour()
                             : DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
        const float cx = static_cast<float>(btn.getCentreX());
        const float cy = static_cast<float>(btn.getCentreY());
        constexpr float arrowSize = 4.0f;
        juce::Path arrow;
        if (isUp) {
            arrow.addTriangle(cx - arrowSize, cy + arrowSize, cx + arrowSize, cy + arrowSize, cx,
                              cy - arrowSize);
        } else {
            arrow.addTriangle(cx - arrowSize, cy - arrowSize, cx + arrowSize, cy - arrowSize, cx,
                              cy + arrowSize);
        }
        g.fillPath(arrow);
    }

    void drawZoomButton(juce::Graphics& g, juce::Rectangle<int> area, bool isIn) {
        if (area.isEmpty())
            return;

        auto btn = area.reduced(2);
        const bool enabled =
            isIn ? (visibleNotes_ > MIN_VISIBLE_NOTES) : (visibleNotes_ < MAX_VISIBLE_NOTES);

        g.setColour(enabled ? DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.15f)
                            : DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
        g.fillRoundedRectangle(btn.toFloat(), 2.0f);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
        g.drawRoundedRectangle(btn.toFloat(), 2.0f, 0.5f);

        g.setColour(enabled ? DarkTheme::getTextColour()
                            : DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
        const float cx = static_cast<float>(btn.getCentreX());
        const float cy = static_cast<float>(btn.getCentreY());
        constexpr float s = 3.0f;
        g.drawLine(cx - s, cy, cx + s, cy, 1.0f);  // minus / plus horizontal bar
        if (isIn)
            g.drawLine(cx, cy - s, cx, cy + s, 1.0f);  // plus vertical bar
    }

    PolySeqPlugin* plugin_ = nullptr;
    int playStep_ = -1;
    int lowNote_ = 48;                          // C2..B3 window — C3 (60) centered
    int visibleNotes_ = DEFAULT_VISIBLE_NOTES;  // pitch rows shown (vertical zoom)

    juce::Rectangle<int> zoomInArea_;
    juce::Rectangle<int> zoomOutArea_;
    juce::Rectangle<int> octaveUpArea_;
    juce::Rectangle<int> octaveDownArea_;
    juce::Rectangle<int> gutterArea_;
    juce::Rectangle<int> cellArea_;
    juce::Rectangle<int> timelineArea_;

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
                                           private juce::ValueTree::Listener {
  public:
    DrumLanesView() = default;

    ~DrumLanesView() override {
        detachStateListeners();
    }

    void setPlugin(PolySeqPlugin* plugin) override {
        detachStateListeners();
        plugin_ = plugin;

        // Watch the owner track's tree so the lane set follows Drum Grid
        // devices being added / removed / reordered in the chain.
        if (plugin_ != nullptr) {
            if (auto* track = plugin_->getOwnerTrack()) {
                trackState_ = track->state;
                trackState_.addListener(this);
            }
        }
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

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();
        if (bounds.isEmpty() || plugin_ == nullptr || lanes_.empty())
            return;

        // Mini timeline (step ruler) across the top, above the whole grid.
        timelineArea_ = bounds.removeFromTop(kStepRulerHeight);

        auto arrowStrip = bounds.removeFromLeft(ARROW_STRIP_WIDTH);
        scrollUpArea_ = arrowStrip.removeFromTop(arrowStrip.getHeight() / 2);
        scrollDownArea_ = arrowStrip;
        labelArea_ = bounds.removeFromLeft(LEFT_GUTTER_WIDTH - ARROW_STRIP_WIDTH);
        cellArea_ = bounds;

        const int numLanes = static_cast<int>(lanes_.size());
        const int visible = visibleLaneCount();
        const float laneH = laneHeight();
        if (visible <= 0)
            return;
        clampScrollOffset();

        drawScrollArrow(g, scrollUpArea_, true);
        drawScrollArrow(g, scrollDownArea_, false);

        g.setFont(FontManager::getInstance().getUIFont(7.0f));

        const int count = juce::jlimit(1, PolySeqPlugin::MAX_STEPS, plugin_->numSteps.get());
        const float colW = static_cast<float>(cellArea_.getWidth()) / static_cast<float>(count);

        drawStepRuler(g, timelineArea_, cellArea_, count, colW, playStep_);

        for (int row = 0; row < visible; ++row) {
            const int laneIdx = scrollOffset_ + (visible - 1 - row);
            if (laneIdx < 0 || laneIdx >= numLanes)
                continue;
            const auto& lane = lanes_[static_cast<size_t>(laneIdx)];
            const float y = cellArea_.getY() + row * laneH;

            // --- Lane label gutter ---
            auto labelRect =
                juce::Rectangle<float>(static_cast<float>(labelArea_.getX()), y,
                                       static_cast<float>(labelArea_.getWidth()), laneH);
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND)
                            .brighter(laneIdx % 2 == 0 ? 0.18f : 0.12f));
            g.fillRect(labelRect.reduced(0.0f, 0.5f));
            g.setColour(lane.orphan ? DarkTheme::getSecondaryTextColour()
                                    : DarkTheme::getTextColour());
            g.drawText(lane.label, labelRect.toNearestInt().reduced(2, 0),
                       juce::Justification::centredLeft);

            // --- Cells ---
            for (int i = 0; i < count; ++i) {
                const auto step = plugin_->getStep(i);
                const float x = cellArea_.getX() + i * colW;
                auto cellRect =
                    juce::Rectangle<float>(x + 0.5f, y + 0.5f, colW - 1.0f, laneH - 1.0f);

                // Background: alternate lane shading, playhead column highlighted
                juce::Colour bg = DarkTheme::getColour(DarkTheme::BACKGROUND)
                                      .brighter(laneIdx % 2 == 0 ? 0.10f : 0.04f);
                if (i == playStep_)
                    bg = bg.overlaidWith(
                        DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.18f));
                if (!step.gate)
                    bg = bg.darker(0.3f);
                g.setColour(bg);
                g.fillRect(cellRect);

                // Active hit: opacity from effective velocity (per-note
                // override, falling back to step velocity)
                int noteVel = 0;
                for (int n = 0; n < step.noteCount; ++n) {
                    const auto& sn = step.notes[static_cast<size_t>(n)];
                    if (sn.noteNumber == lane.note) {
                        noteVel = sn.velocity > 0 ? sn.velocity : step.velocity;
                        break;
                    }
                }
                if (noteVel > 0) {
                    const float alpha = 0.35f + 0.6f * static_cast<float>(noteVel) / 127.0f;
                    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(alpha));
                    g.fillRoundedRectangle(cellRect, 1.5f);
                }
            }
        }

        // --- Grid lines: heavier every 4 steps ---
        for (int i = 0; i <= count; ++i) {
            const float x = cellArea_.getX() + i * colW;
            g.setColour(
                DarkTheme::getColour(DarkTheme::BORDER).withAlpha(i % 4 == 0 ? 0.4f : 0.15f));
            g.drawVerticalLine(juce::roundToInt(x), static_cast<float>(cellArea_.getY()),
                               static_cast<float>(cellArea_.getBottom()));
        }
        for (int row = 0; row <= visible; ++row) {
            const float y = cellArea_.getY() + row * laneH;
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.15f));
            g.drawHorizontalLine(juce::roundToInt(y), static_cast<float>(cellArea_.getX()),
                                 static_cast<float>(cellArea_.getRight()));
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (plugin_ == nullptr)
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

        plugin_->toggleStepNote(step, lanes_[static_cast<size_t>(laneIdx)].note);
        repaint();
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override {
        scrollBy(wheel.deltaY > 0 ? 1 : -1);
    }

  private:
    static constexpr int ARROW_STRIP_WIDTH = 14;
    static constexpr int MIN_LANE_HEIGHT = 14;

    struct Lane {
        int note = 60;        // MIDI note that triggers this lane's sound
        juce::String label;   // Chain name / GM name / note name
        bool orphan = false;  // Pattern data with no matching Drum Grid chain
    };

    // --- Lane discovery ---

    /** Find a Drum Grid downstream of the sequencer on the owner track,
     *  looking inside rack instances (instruments are rack-wrapped). If the
     *  sequencer itself is not on the top-level list, any Drum Grid on the
     *  track is accepted. */
    daw::audio::DrumGridPlugin* findDownstreamDrumGrid() const {
        if (plugin_ == nullptr)
            return nullptr;
        auto* track = plugin_->getOwnerTrack();
        if (track == nullptr)
            return nullptr;

        bool passedSelf = false;
        daw::audio::DrumGridPlugin* fallback = nullptr;
        for (auto* p : track->pluginList) {
            if (p == plugin_) {
                passedSelf = true;
                continue;
            }
            auto* found = dynamic_cast<daw::audio::DrumGridPlugin*>(p);
            if (found == nullptr) {
                if (auto* rackInstance = dynamic_cast<te::RackInstance*>(p)) {
                    if (rackInstance->type != nullptr) {
                        for (auto* inner : rackInstance->type->getPlugins()) {
                            found = dynamic_cast<daw::audio::DrumGridPlugin*>(inner);
                            if (found != nullptr)
                                break;
                        }
                    }
                }
            }
            if (found != nullptr) {
                if (passedSelf)
                    return found;
                if (fallback == nullptr)
                    fallback = found;
            }
        }
        return passedSelf ? nullptr : fallback;
    }

    /** Rebuild the lane list: Drum Grid chains (or GM fallback) + orphan
     *  lanes for pattern notes that match no chain. Sorted note-ascending so
     *  the lowest lane paints at the bottom. */
    void refreshLanes() {
        auto* drumGrid = findDownstreamDrumGrid();

        // (Re)attach the Drum Grid state listener when the target changes,
        // so chain renames / note-range edits refresh the lane labels.
        auto newState = drumGrid != nullptr ? drumGrid->state : juce::ValueTree();
        if (newState != drumGridState_) {
            if (drumGridState_.isValid())
                drumGridState_.removeListener(this);
            drumGridState_ = newState;
            if (drumGridState_.isValid())
                drumGridState_.addListener(this);
        }

        lanes_.clear();
        if (drumGrid != nullptr) {
            for (const auto& chain : drumGrid->getChains()) {
                if (chain == nullptr)
                    continue;
                // The chain's low note is the incoming MIDI note that triggers
                // the pad (single-note chains have lowNote == highNote).
                Lane lane;
                lane.note = chain->lowNote;
                lane.label =
                    chain->name.isNotEmpty() ? chain->name : polyNoteNameShort(chain->lowNote);
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
        if (plugin_ != nullptr) {
            for (int i = 0; i < PolySeqPlugin::MAX_STEPS; ++i) {
                const auto step = plugin_->getStep(i);
                for (int n = 0; n < step.noteCount; ++n) {
                    const int note = step.notes[static_cast<size_t>(n)].noteNumber;
                    if (!hasLaneForNote(note))
                        lanes_.push_back({note, polyNoteNameShort(note), true});
                }
            }
        }

        std::sort(lanes_.begin(), lanes_.end(),
                  [](const Lane& a, const Lane& b) { return a.note < b.note; });
        lanes_.erase(std::unique(lanes_.begin(), lanes_.end(),
                                 [](const Lane& a, const Lane& b) { return a.note == b.note; }),
                     lanes_.end());

        clampScrollOffset();
        repaint();
    }

    bool hasLaneForNote(int note) const {
        for (const auto& lane : lanes_)
            if (lane.note == note)
                return true;
        return false;
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

    void detachStateListeners() {
        if (trackState_.isValid()) {
            trackState_.removeListener(this);
            trackState_ = juce::ValueTree();
        }
        if (drumGridState_.isValid()) {
            drumGridState_.removeListener(this);
            drumGridState_ = juce::ValueTree();
        }
    }

    // ValueTree::Listener — chain membership (track tree) + Drum Grid chains
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&) override {
        if (tree.hasType(DRUM_CHAIN_TYPE))
            triggerLaneRefresh();
    }
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree& child) override {
        if (child.hasType(te::IDs::PLUGIN) || child.hasType(DRUM_CHAIN_TYPE))
            triggerLaneRefresh();
    }
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree& child, int) override {
        if (child.hasType(te::IDs::PLUGIN) || child.hasType(DRUM_CHAIN_TYPE))
            triggerLaneRefresh();
    }
    void valueTreeChildOrderChanged(juce::ValueTree& parent, int, int) override {
        if (parent == trackState_)
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
        if (plugin_ == nullptr || cellArea_.getWidth() <= 0)
            return -1;
        const int count = juce::jlimit(1, PolySeqPlugin::MAX_STEPS, plugin_->numSteps.get());
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
        const auto step = plugin_->getStep(stepIndex);

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

        menu.showMenuAsync(juce::PopupMenu::Options(), [this, stepIndex](int result) {
            if (plugin_ == nullptr)
                return;
            switch (result) {
                case 1: {
                    auto s = plugin_->getStep(stepIndex);
                    plugin_->setStepGate(stepIndex, !s.gate);
                    break;
                }
                case 2:
                    plugin_->clearStep(stepIndex);
                    break;
                case 10:
                    plugin_->transposePattern(1);
                    break;
                case 11:
                    plugin_->transposePattern(-1);
                    break;
                case 12:
                    plugin_->transposePattern(12);
                    break;
                case 13:
                    plugin_->transposePattern(-12);
                    break;
                case 20:
                    plugin_->clearPattern();
                    break;
                default:
                    return;
            }
            repaint();
        });
    }

    void drawScrollArrow(juce::Graphics& g, juce::Rectangle<int> area, bool isUp) {
        if (area.isEmpty())
            return;

        auto btn = area.reduced(2);
        const int maxOffset = juce::jmax(0, static_cast<int>(lanes_.size()) - visibleLaneCount());
        const bool canShift = isUp ? (scrollOffset_ < maxOffset) : (scrollOffset_ > 0);

        g.setColour(canShift ? DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.15f)
                             : DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
        g.fillRoundedRectangle(btn.toFloat(), 2.0f);

        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
        g.drawRoundedRectangle(btn.toFloat(), 2.0f, 0.5f);

        g.setColour(canShift ? DarkTheme::getTextColour()
                             : DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
        const float cx = static_cast<float>(btn.getCentreX());
        const float cy = static_cast<float>(btn.getCentreY());
        constexpr float arrowSize = 4.0f;
        juce::Path arrow;
        if (isUp) {
            arrow.addTriangle(cx - arrowSize, cy + arrowSize, cx + arrowSize, cy + arrowSize, cx,
                              cy - arrowSize);
        } else {
            arrow.addTriangle(cx - arrowSize, cy - arrowSize, cx + arrowSize, cy - arrowSize, cx,
                              cy + arrowSize);
        }
        g.fillPath(arrow);
    }

    PolySeqPlugin* plugin_ = nullptr;
    int playStep_ = -1;
    int scrollOffset_ = 0;  // Index of the bottom-most visible lane
    bool refreshPending_ = false;

    std::vector<Lane> lanes_;  // Sorted note-ascending (lowest paints at the bottom)

    juce::ValueTree trackState_;     // Owner track tree (chain membership)
    juce::ValueTree drumGridState_;  // Discovered Drum Grid state (CHAIN children)

    juce::Rectangle<int> scrollUpArea_;
    juce::Rectangle<int> scrollDownArea_;
    juce::Rectangle<int> labelArea_;
    juce::Rectangle<int> cellArea_;
    juce::Rectangle<int> timelineArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumLanesView)
};

// =============================================================================
// Construction
// =============================================================================

PolyStepSequencerUI::PolyStepSequencerUI() {
    setupLabel(rateLabel_, "RATE");
    setupSlider(rateSlider_, 0, 9, 1);
    static const char* rateNames[] = {"1/4.", "1/4",   "1/4T", "1/8.",  "1/8",
                                      "1/8T", "1/16.", "1/16", "1/16T", "1/32"};
    rateSlider_.setValueFormatter([](double v) {
        int idx = juce::jlimit(0, 9, juce::roundToInt(v));
        return juce::String(rateNames[idx]);
    });
    rateSlider_.setValueParser([](const juce::String&) { return 1.0; });
    rateSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->rate = juce::roundToInt(value);
    };

    setupLabel(stepsLabel_, "STEPS");
    setupSlider(stepsSlider_, 1, PolySeqPlugin::MAX_STEPS, 1);
    stepsSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    stepsSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    stepsSlider_.onValueChanged = [this](double value) {
        if (plugin_) {
            int steps = juce::roundToInt(value);
            plugin_->numSteps = steps;
            rampCurveDisplay_.setNumTicks(steps);
            // Clamp cycles to num steps
            cyclesSlider_.setRange(1.0, static_cast<double>(steps), 1.0);
            if (cyclesSlider_.getValue() > steps)
                cyclesSlider_.setValue(static_cast<double>(steps), juce::sendNotificationSync);
            repaint();
        }
    };

    setupLabel(dirLabel_, "DIR");
    dirCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    dirCombo_.setColour(juce::ComboBox::backgroundColourId,
                        DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    dirCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    dirCombo_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    dirCombo_.addItem("Forward", 1);
    dirCombo_.addItem("Reverse", 2);
    dirCombo_.addItem("Ping-Pong", 3);
    dirCombo_.addItem("Random", 4);
    dirCombo_.onChange = [this] {
        if (plugin_)
            plugin_->direction = dirCombo_.getSelectedId() - 1;
    };
    addAndMakeVisible(dirCombo_);

    setupLabel(swingLabel_, "SWING");
    setupSlider(swingSlider_, 0.0, 1.0, 0.01);
    swingSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; });
    swingSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("%", "").trim().getDoubleValue() / 100.0; });
    swingSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->swing = static_cast<float>(value);
    };

    setupLabel(gateLengthLabel_, "GATE");
    setupSlider(gateLengthSlider_, 0.05, 1.0, 0.01);
    gateLengthSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; });
    gateLengthSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("%", "").trim().getDoubleValue() / 100.0; });
    gateLengthSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->gateLength = static_cast<float>(value);
    };

    // Quantize slider (adaptive snap strength 0-100%)
    setupLabel(quantizeLabel_, "QUANTIZE");
    setupSlider(quantizeSlider_, 0.0, 1.0, 0.01);
    quantizeSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; });
    quantizeSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("%", "").trim().getDoubleValue() / 100.0; });
    quantizeSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->quantize = static_cast<float>(value);
    };

    // Quantize subdivisions (grid resolution, multiples of 16)
    setupLabel(quantizeSubLabel_, "SUB");
    setupSlider(quantizeSubSlider_, 16, 512, 16);
    quantizeSubSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)); });
    quantizeSubSlider_.setValueParser(
        [](const juce::String& t) { return t.trim().getDoubleValue(); });
    quantizeSubSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->quantizeSub = juce::roundToInt(value);
    };

    // --- Ramp curve (time warp) ---
    setupLabel(rampLabel_, "TIME BEND");
    addAndMakeVisible(rampCurveDisplay_);
    rampCurveDisplay_.onCurveChanged = [this](float depth, float skew) {
        if (plugin_) {
            plugin_->ramp = depth;
            plugin_->skew = skew;
        }
    };

    setupLabel(depthLabel_, "DEPTH");
    depthLabel_.setJustificationType(juce::Justification::centred);
    setupSlider(depthSlider_, -1.0, 1.0, 0.01);
    depthSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100)); });
    depthSlider_.setValueParser(
        [](const juce::String& t) { return t.trim().getDoubleValue() / 100.0; });
    depthSlider_.onValueChanged = [this](double value) {
        if (plugin_) {
            plugin_->ramp = static_cast<float>(value);
            rampCurveDisplay_.setValues(static_cast<float>(value), plugin_->skew.get());
        }
    };

    setupLabel(skewLabel_, "SKEW");
    skewLabel_.setJustificationType(juce::Justification::centred);
    setupSlider(skewSlider_, -1.0, 1.0, 0.01);
    skewSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v * 100)); });
    skewSlider_.setValueParser(
        [](const juce::String& t) { return t.trim().getDoubleValue() / 100.0; });
    skewSlider_.onValueChanged = [this](double value) {
        if (plugin_) {
            plugin_->skew = static_cast<float>(value);
            rampCurveDisplay_.setValues(plugin_->ramp.get(), static_cast<float>(value));
        }
    };

    setupLabel(cyclesLabel_, "CYCLES");
    setupSlider(cyclesSlider_, 1.0, static_cast<double>(PolySeqPlugin::MAX_STEPS), 1.0);
    cyclesSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    cyclesSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    cyclesSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->rampCycles = juce::roundToInt(value);
    };

    // Hard angle toggle (right-click on control point)
    rampCurveDisplay_.onHardAngleChanged = [this](bool hardAngle) {
        if (plugin_)
            plugin_->hardAngle = hardAngle;
    };

    // MIDI thru / step record live in the device-slot header, owned by
    // DeviceSlotComponent — not in this body.

    // --- View mode toggle (keys / drum) ---
    viewModeButton_.setButtonText("KEYS");
    viewModeButton_.setClickingTogglesState(true);
    // Theme font + box-style rounding, matching the side-panel sliders/combo.
    viewModeButton_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    viewModeButton_.setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    viewModeButton_.setColour(juce::TextButton::buttonOnColourId,
                              DarkTheme::getAccentColour().withAlpha(0.6f));
    viewModeButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    viewModeButton_.setColour(juce::TextButton::textColourOnId, DarkTheme::getTextColour());
    viewModeButton_.setTooltip("Switch between keys and drum-lane pattern views");
    viewModeButton_.onClick = [this] {
        if (plugin_)
            plugin_->viewMode =
                viewModeButton_.getToggleState() ? juce::String("drum") : juce::String("keys");
        updatePatternViewMode();
    };
    addAndMakeVisible(viewModeButton_);

    // --- Pattern view (keys mode default; swapped by updatePatternViewMode) ---
    patternView_ = std::make_unique<KeysView>();
    addAndMakeVisible(*patternView_);
}

PolyStepSequencerUI::~PolyStepSequencerUI() {
    stopTimer();
    if (watchedState_.isValid())
        watchedState_.removeListener(this);
    dirCombo_.setLookAndFeel(nullptr);
    viewModeButton_.setLookAndFeel(nullptr);
}

// =============================================================================
// Plugin binding
// =============================================================================

void PolyStepSequencerUI::setPlugin(daw::audio::PolyStepSequencerPlugin* plugin) {
    stopTimer();
    if (watchedState_.isValid())
        watchedState_.removeListener(this);

    plugin_ = plugin;
    patternView_->setPlugin(plugin);

    if (plugin_) {
        watchedState_ = plugin_->state;
        watchedState_.addListener(this);
        syncFromPlugin();
        startTimerHz(30);
    }
}

void PolyStepSequencerUI::updatePatternViewMode() {
    const bool wantDrum = plugin_ != nullptr && plugin_->viewMode.get() == "drum";
    if (wantDrum != drumViewActive_) {
        drumViewActive_ = wantDrum;
        patternView_ = wantDrum ? std::unique_ptr<PatternView>(std::make_unique<DrumLanesView>())
                                : std::unique_ptr<PatternView>(std::make_unique<KeysView>());
        addAndMakeVisible(*patternView_);
        patternView_->setPlugin(plugin_);
        patternView_->setPlayStep(currentPlayStep_);
        resized();
    }
    viewModeButton_.setToggleState(drumViewActive_, juce::dontSendNotification);
    viewModeButton_.setButtonText(drumViewActive_ ? "DRUM" : "KEYS");
}

void PolyStepSequencerUI::syncFromPlugin() {
    if (!plugin_)
        return;

    updatePatternViewMode();
    rateSlider_.setValue(static_cast<double>(plugin_->rate.get()), juce::dontSendNotification);
    stepsSlider_.setValue(static_cast<double>(plugin_->numSteps.get()), juce::dontSendNotification);
    dirCombo_.setSelectedId(plugin_->direction.get() + 1, juce::dontSendNotification);
    swingSlider_.setValue(static_cast<double>(plugin_->swing.get()), juce::dontSendNotification);
    gateLengthSlider_.setValue(static_cast<double>(plugin_->gateLength.get()),
                               juce::dontSendNotification);
    depthSlider_.setValue(static_cast<double>(plugin_->ramp.get()), juce::dontSendNotification);
    skewSlider_.setValue(static_cast<double>(plugin_->skew.get()), juce::dontSendNotification);
    rampCurveDisplay_.setValues(plugin_->ramp.get(), plugin_->skew.get());
    rampCurveDisplay_.setHardAngle(plugin_->hardAngle.get());
    int steps = plugin_->numSteps.get();
    rampCurveDisplay_.setNumTicks(steps);
    cyclesSlider_.setRange(1.0, static_cast<double>(steps), 1.0);
    quantizeSlider_.setValue(static_cast<double>(plugin_->quantize.get()),
                             juce::dontSendNotification);
    quantizeSubSlider_.setValue(static_cast<double>(plugin_->quantizeSub.get()),
                                juce::dontSendNotification);
    cyclesSlider_.setValue(static_cast<double>(juce::jlimit(1, steps, plugin_->rampCycles.get())),
                           juce::dontSendNotification);
    patternView_->patternChanged();
    repaint();
}

void PolyStepSequencerUI::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) {
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer(this)] {
        if (safeThis)
            safeThis->syncFromPlugin();
    });
}

void PolyStepSequencerUI::valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) {
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer(this)] {
        if (safeThis)
            safeThis->syncFromPlugin();
    });
}

void PolyStepSequencerUI::valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) {
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer(this)] {
        if (safeThis)
            safeThis->syncFromPlugin();
    });
}

void PolyStepSequencerUI::timerCallback() {
    if (!plugin_)
        return;

    int step = plugin_->currentPlayStep_.load(std::memory_order_relaxed);
    if (step != currentPlayStep_) {
        currentPlayStep_ = step;
        patternView_->setPlayStep(step);
        // Update curve display sweep
        int numSteps = juce::jlimit(1, PolySeqPlugin::MAX_STEPS, plugin_->numSteps.get());
        float pos = (step >= 0) ? static_cast<float>(step) / static_cast<float>(numSteps) : -1.0f;
        rampCurveDisplay_.setPlaybackPosition(pos,
                                              juce::jlimit(1, numSteps, plugin_->rampCycles.get()));
    }
}

// =============================================================================
// Layout
// =============================================================================

void PolyStepSequencerUI::resized() {
    auto bounds = getLocalBounds().reduced(PADDING);

    // --- Right-side control panel (controls + time bend) ---
    // All the sequencer controls live in a column on the right, styled like the
    // device mod/macro side panel. This hands the full editor height to the
    // pattern grid, which now only loses the per-step lanes at the bottom.
    constexpr int SIDE_PANEL_WIDTH = 150;
    constexpr int SIDE_GAP = 6;
    sidePanelArea_ = bounds.removeFromRight(SIDE_PANEL_WIDTH);
    bounds.removeFromRight(SIDE_GAP);
    auto panel = sidePanelArea_.reduced(6, 5);

    // --- Top controls: 2-column grid, label above each control ---
    //   RATE  | STEPS
    //   SWING | GATE
    //   QUANT | SUB
    //   DIR   | [KEYS] [thru]
    constexpr int CELL_LABEL_H = 14;
    constexpr int GRID_CELL_H = CONTROL_ROW_HEIGHT + CELL_LABEL_H;
    constexpr int COL_GAP = 6;
    const auto gridCell = [&](juce::Label& label, juce::Component& ctrl,
                              juce::Rectangle<int> cell) {
        label.setBounds(cell.removeFromTop(CELL_LABEL_H));
        ctrl.setBounds(cell);
    };
    const auto gridRow = [&](juce::Label& l1, juce::Component& c1, juce::Label& l2,
                             juce::Component& c2) {
        auto row = panel.removeFromTop(GRID_CELL_H);
        const int half = (row.getWidth() - COL_GAP) / 2;
        gridCell(l1, c1, row.removeFromLeft(half));
        row.removeFromLeft(COL_GAP);
        gridCell(l2, c2, row);
        panel.removeFromTop(ROW_GAP);
    };
    gridRow(rateLabel_, rateSlider_, stepsLabel_, stepsSlider_);
    gridRow(swingLabel_, swingSlider_, gateLengthLabel_, gateLengthSlider_);
    gridRow(quantizeLabel_, quantizeSlider_, quantizeSubLabel_, quantizeSubSlider_);

    // DIR on the left, view-mode (keys/drum) toggle on the right.
    {
        auto row = panel.removeFromTop(GRID_CELL_H);
        const int half = (row.getWidth() - COL_GAP) / 2;
        gridCell(dirLabel_, dirCombo_, row.removeFromLeft(half));
        row.removeFromLeft(COL_GAP);
        row.removeFromTop(CELL_LABEL_H);  // align button with the combo, not the label
        viewModeButton_.setBounds(row.removeFromLeft(juce::jmin(48, row.getWidth())).reduced(0, 2));
        panel.removeFromTop(ROW_GAP);
    }

    // --- TIME BEND: label, DEPTH/SKEW/CYCLES row, then the curve fills the
    // rest of the panel height (as tall as the panel allows) ---
    constexpr int LABEL_H = 14;
    constexpr int CELL_H = CONTROL_ROW_HEIGHT + LABEL_H;
    panel.removeFromTop(ROW_GAP + 2);
    rampLabel_.setBounds(panel.removeFromTop(LABEL_H));
    {
        auto boxRow = panel.removeFromTop(CELL_H);
        const int cellW = boxRow.getWidth() / 3;
        gridCell(depthLabel_, depthSlider_, boxRow.removeFromLeft(cellW));
        gridCell(skewLabel_, skewSlider_, boxRow.removeFromLeft(cellW));
        gridCell(cyclesLabel_, cyclesSlider_, boxRow);
    }
    panel.removeFromTop(2);
    rampCurveDisplay_.setBounds(panel);  // curve takes all remaining height

    // --- Main area (left): pattern grid + per-step lanes ---
    // Per-step lanes from the bottom up: PROB, VEL, TIE, GATE
    probabilityArea_ = bounds.removeFromBottom(LANE_HEIGHT);
    bounds.removeFromBottom(ROW_GAP);
    velocityArea_ = bounds.removeFromBottom(LANE_HEIGHT);
    bounds.removeFromBottom(ROW_GAP);
    tieArea_ = bounds.removeFromBottom(TOGGLE_ROW_HEIGHT);
    bounds.removeFromBottom(ROW_GAP);
    gateArea_ = bounds.removeFromBottom(TOGGLE_ROW_HEIGHT);
    bounds.removeFromBottom(ROW_GAP + 2);

    // Pattern grid fills the remaining space
    patternView_->setBounds(bounds);
}

// =============================================================================
// Paint
// =============================================================================

void PolyStepSequencerUI::paint(juce::Graphics& g) {
    // Control side panel: subtle card + left separator, matching the device
    // mod/macro side panels.
    if (!sidePanelArea_.isEmpty()) {
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
        g.fillRect(sidePanelArea_);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
        g.drawVerticalLine(sidePanelArea_.getX(), static_cast<float>(sidePanelArea_.getY()),
                           static_cast<float>(sidePanelArea_.getBottom()));
    }

    drawToggleRow(g, gateArea_, "GAT", false);
    drawToggleRow(g, tieArea_, "TIE", true);
    drawBarLane(g, velocityArea_, "VEL", false);
    drawBarLane(g, probabilityArea_, "PRB", true);
}

void PolyStepSequencerUI::drawToggleRow(juce::Graphics& g, juce::Rectangle<int> area,
                                        const juce::String& label, bool isTieRow) {
    if (!plugin_ || area.isEmpty())
        return;

    int count = juce::jlimit(1, PolySeqPlugin::MAX_STEPS, plugin_->numSteps.get());
    g.setFont(FontManager::getInstance().getUIFont(7.0f));

    // Row label, aligned with the grid's left gutter
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.drawText(label, area.removeFromLeft(LEFT_GUTTER_WIDTH), juce::Justification::centredLeft);

    float boxW = static_cast<float>(area.getWidth()) / static_cast<float>(count);

    for (int i = 0; i < count; ++i) {
        auto step = plugin_->getStep(i);
        float x = static_cast<float>(area.getX()) + i * boxW;
        auto rect =
            juce::Rectangle<float>(x + 1.0f, static_cast<float>(area.getY()) + 1.0f, boxW - 2.0f,
                                   static_cast<float>(area.getHeight()) - 2.0f);

        bool on = isTieRow ? step.tie : step.gate;
        if (on) {
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.7f));
            g.fillRoundedRectangle(rect, 2.0f);
            g.setColour(DarkTheme::getTextColour());
            g.drawText(isTieRow ? "T" : "G", rect.toNearestInt(), juce::Justification::centred);
        } else {
            g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.2f));
            g.fillRoundedRectangle(rect, 2.0f);
        }
    }
}

void PolyStepSequencerUI::drawBarLane(juce::Graphics& g, juce::Rectangle<int> area,
                                      const juce::String& label, bool isProbability) {
    if (!plugin_ || area.isEmpty())
        return;

    int count = juce::jlimit(1, PolySeqPlugin::MAX_STEPS, plugin_->numSteps.get());
    g.setFont(FontManager::getInstance().getUIFont(7.0f));

    // Lane label, aligned with the grid's left gutter
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.drawText(label, area.removeFromLeft(LEFT_GUTTER_WIDTH), juce::Justification::centredLeft);

    float boxW = static_cast<float>(area.getWidth()) / static_cast<float>(count);

    for (int i = 0; i < count; ++i) {
        auto step = plugin_->getStep(i);
        float x = static_cast<float>(area.getX()) + i * boxW;
        auto rect =
            juce::Rectangle<float>(x + 1.0f, static_cast<float>(area.getY()) + 1.0f, boxW - 2.0f,
                                   static_cast<float>(area.getHeight()) - 2.0f);

        // Background
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.2f));
        g.fillRoundedRectangle(rect, 2.0f);

        // Value bar from the bottom
        float ratio = isProbability ? juce::jlimit(0.0f, 1.0f, step.probability)
                                    : static_cast<float>(step.velocity) / 127.0f;
        if (ratio > 0.0f) {
            auto bar = rect.withTrimmedTop(rect.getHeight() * (1.0f - ratio));
            g.setColour((isProbability ? DarkTheme::getColour(DarkTheme::ACCENT_ORANGE)
                                       : DarkTheme::getColour(DarkTheme::ACCENT_BLUE))
                            .withAlpha(0.7f));
            g.fillRoundedRectangle(bar, 2.0f);
        }
    }
}

// =============================================================================
// Mouse interaction (per-step lanes; the grid handles its own mouse)
// =============================================================================

int PolyStepSequencerUI::getStepAtX(int x, int areaX, int areaWidth, int numSteps) const {
    if (numSteps <= 0 || areaWidth <= 0)
        return -1;
    int relX = x - areaX;
    if (relX < 0 || relX >= areaWidth)
        return -1;
    return relX * numSteps / areaWidth;
}

void PolyStepSequencerUI::applyLaneDrag(const juce::MouseEvent& e) {
    if (!plugin_ || activeDragLane_ == DragLane::None)
        return;

    auto area = (activeDragLane_ == DragLane::Velocity ? velocityArea_ : probabilityArea_)
                    .withTrimmedLeft(LEFT_GUTTER_WIDTH);
    int count = juce::jlimit(1, PolySeqPlugin::MAX_STEPS, plugin_->numSteps.get());
    int step = getStepAtX(juce::jlimit(area.getX(), area.getRight() - 1, e.x), area.getX(),
                          area.getWidth(), count);
    if (step < 0 || step >= count)
        return;

    float ratio = 1.0f - static_cast<float>(e.y - area.getY()) /
                             static_cast<float>(juce::jmax(1, area.getHeight()));
    ratio = juce::jlimit(0.0f, 1.0f, ratio);

    if (activeDragLane_ == DragLane::Velocity)
        plugin_->setStepVelocity(step, juce::jlimit(1, 127, juce::roundToInt(ratio * 127.0f)));
    else
        plugin_->setStepProbability(step, ratio);
    repaint();
}

void PolyStepSequencerUI::mouseDown(const juce::MouseEvent& e) {
    if (!plugin_)
        return;
    auto pos = e.getPosition();
    int count = juce::jlimit(1, PolySeqPlugin::MAX_STEPS, plugin_->numSteps.get());

    // Gate row — toggle gate
    if (gateArea_.contains(pos)) {
        auto contentArea = gateArea_.withTrimmedLeft(LEFT_GUTTER_WIDTH);
        int step = getStepAtX(pos.x, contentArea.getX(), contentArea.getWidth(), count);
        if (step >= 0 && step < count) {
            auto s = plugin_->getStep(step);
            plugin_->setStepGate(step, !s.gate);
            repaint();
        }
        return;
    }

    // Tie row — toggle tie
    if (tieArea_.contains(pos)) {
        auto contentArea = tieArea_.withTrimmedLeft(LEFT_GUTTER_WIDTH);
        int step = getStepAtX(pos.x, contentArea.getX(), contentArea.getWidth(), count);
        if (step >= 0 && step < count) {
            auto s = plugin_->getStep(step);
            plugin_->setStepTie(step, !s.tie);
            repaint();
        }
        return;
    }

    // Velocity / probability lanes — start bar drag
    if (velocityArea_.contains(pos)) {
        activeDragLane_ = DragLane::Velocity;
        applyLaneDrag(e);
        return;
    }
    if (probabilityArea_.contains(pos)) {
        activeDragLane_ = DragLane::Probability;
        applyLaneDrag(e);
        return;
    }
}

void PolyStepSequencerUI::mouseDrag(const juce::MouseEvent& e) {
    applyLaneDrag(e);
}

void PolyStepSequencerUI::mouseUp(const juce::MouseEvent&) {
    activeDragLane_ = DragLane::None;
}

// =============================================================================
// Setup helpers
// =============================================================================

void PolyStepSequencerUI::setupLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);
}

void PolyStepSequencerUI::setupSlider(LinkableTextSlider& slider, double min, double max,
                                      double step) {
    slider.setRange(min, max, step);
    addAndMakeVisible(slider);
}

std::vector<LinkableTextSlider*> PolyStepSequencerUI::getLinkableSliders() {
    magda::ChainNodePath dummy;
    // Param indices match AutomatableParameter registration order:
    // 0=rate, 1=direction, 2=swing, 3=gatelength, 4=ramp, 5=skew
    rateSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 0, dummy);
    swingSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 2, dummy);
    gateLengthSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 3, dummy);
    depthSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 4, dummy);
    skewSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 5, dummy);
    return {&rateSlider_, &swingSlider_, &gateLengthSlider_, &depthSlider_, &skewSlider_};
}

}  // namespace magda::daw::ui
