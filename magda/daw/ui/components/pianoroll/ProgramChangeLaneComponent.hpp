// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

#include "core/ClipInfo.hpp"
#include "core/ClipTypes.hpp"

namespace magda {

/** @brief The text a program change block shows, e.g. "Program 12 · Bank 1/3". */
juce::String programChangeLabel(const MidiProgramChangeData& change);

/**
 * @brief The MIDI editor lane for a clip's program changes (#3412).
 *
 * Each change is a block from its position to the next change or the clip end. The block's
 * label at its start is the handle: drag it to move the change, double-click it to edit, and
 * Delete removes the selected one. Dragging the line between two blocks sets where the first
 * ends. A click anywhere else adds a change there.
 */
class ProgramChangeLaneComponent : public juce::Component {
  public:
    ProgramChangeLaneComponent();

    void setClip(ClipId clipId);
    void setPixelsPerBeat(double ppb);
    void setScrollOffset(int offsetX);
    void setLeftPadding(int padding);
    void setRelativeMode(bool relative);
    void setClipStartBeats(double startBeats);
    void setClipLengthBeats(double lengthBeats);
    void setLoopRegion(double offsetBeats, double lengthBeats, bool enabled);
    void refreshEvents();

    juce::String getLaneName() const {
        return "Program";
    }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

  private:
    /// One change as drawn: its index in the clip's list and where its block runs.
    struct Block {
        size_t index = 0;
        double startBeat = 0.0;  // in the lane's display beats
        double endBeat = 0.0;
    };

    std::vector<Block> blocks() const;
    juce::Rectangle<int> handleArea(const Block& block, const MidiProgramChangeData& change) const;
    std::optional<size_t> handleAt(juce::Point<int> position) const;
    /// The change whose start is the line under @p x, between it and the block before.
    std::optional<size_t> boundaryAt(int x) const;
    double displayBeat(double clipBeat) const;
    double clipBeat(double displayBeat) const;
    double laneEndBeat() const;
    /// True from the clip's first beat to its end: where a click may add a change.
    bool insideClip(int x) const;
    int beatToX(double beat) const;
    double xToBeat(int x) const;

    void openEditor(std::optional<size_t> index, double clipBeatPosition);
    void commit(std::vector<MidiProgramChangeData> changes, const juce::String& description);

    ClipId clipId_ = INVALID_CLIP_ID;
    double pixelsPerBeat_ = 50.0;
    int scrollOffsetX_ = 0;
    int leftPadding_ = 2;
    bool relativeMode_ = true;
    double clipStartBeats_ = 0.0;
    double clipLengthBeats_ = 0.0;

    std::optional<size_t> selected_;
    bool dragging_ = false;
    size_t dragIndex_ = 0;
    double dragOffsetBeats_ = 0.0;
    std::optional<double> dragPreviewBeat_;
    /// Where a click would add a change, while the mouse is off every handle.
    std::optional<int> addHoverX_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProgramChangeLaneComponent)
};

}  // namespace magda
