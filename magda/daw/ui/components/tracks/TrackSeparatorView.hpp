// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "core/TypeIds.hpp"

namespace magda {

/**
 * @brief A separator track where it runs vertically: Session View and the Mixer (#3228).
 *
 * A rule and the track's optional name, which is read from the model when it
 * paints. The arrangement draws its own row, where it runs across.
 */
class TrackSeparatorView : public juce::Component {
  public:
    static constexpr int WIDTH = 16;
    static constexpr int MAX_WIDTH = 48;

    explicit TrackSeparatorView(TrackId trackId);

    TrackId getTrackId() const {
        return trackId_;
    }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

    // For a view that reorders its columns by dragging them.
    std::function<void(const juce::MouseEvent&)> onPress;
    std::function<void(const juce::MouseEvent&)> onDrag;
    std::function<void(const juce::MouseEvent&)> onRelease;

  private:
    TrackId trackId_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackSeparatorView)
};

/** @brief The right-click menu every view shows for a separator: rename and delete. */
void showTrackSeparatorMenu(TrackId trackId);

/** @brief Asks for a new name. Empty is a valid answer: the name is optional. */
void promptToRenameTrackSeparator(TrackId trackId);

}  // namespace magda
