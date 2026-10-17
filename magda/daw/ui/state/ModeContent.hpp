/**
 * @file ModeContent.hpp
 * @brief Content a view mode shows in place of the arrangement's track headers and lanes.
 *
 * The ruler, markers, grid, playhead and the side and bottom panels stay; only the track area
 * is replaced. Registered by extensions (#3077); a mode with nothing registered is unchanged.
 */
#pragma once

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>

#include "core/ViewModeState.hpp"

namespace magda {

class TimelineController;

/** @brief What a mode's content is built against. */
struct ModeContentContext {
    TimelineController& timeline;
};

/** @brief The component a mode shows; told where the header column and the lanes fall inside it. */
class ModeContentComponent : public juce::Component {
  public:
    /** @brief Local rectangles: lane x lines up with the ruler, so draw time from @p lanes' left.
     */
    virtual void setTrackAreaLayout(juce::Rectangle<int> headers, juce::Rectangle<int> lanes) {
        juce::ignoreUnused(headers, lanes);
    }
};

/** @brief A registered mode: its footer entry and the component that fills the track area. */
struct ModeContent {
    /// Footer entry for the mode, beside Live / Arrange / Mix; no icon, no entry.
    juce::String label;
    juce::String iconSvg;
    juce::Colour accent;
    /// The footer's way out of an exclusive mode; a text button when empty.
    juce::String exitIconSvg;
    /// Entered and left explicitly; see ViewModeController::setExclusive().
    bool exclusive = false;
    /// Called the first time the mode is shown in a MainView, which then owns the component.
    std::function<std::unique_ptr<ModeContentComponent>(ModeContentContext&)> create;
};

/** @brief The registered mode contents. Message thread only; broadcasts when one is added. */
class ModeContentRegistry : public juce::ChangeBroadcaster {
  public:
    static ModeContentRegistry& getInstance();

    void add(ViewMode mode, ModeContent content);
    const ModeContent* find(ViewMode mode) const;
    const std::map<ViewMode, ModeContent>& all() const {
        return contents_;
    }

  private:
    std::map<ViewMode, ModeContent> contents_;
};

}  // namespace magda
