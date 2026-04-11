#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "core/TrackManager.hpp"

namespace magda {

/**
 * @brief Dialog showing the track chain as a hierarchical tree view
 *
 * Displays the entire signal chain structure for a track:
 * - Track (root)
 *   - Devices (leaf nodes)
 *   - Racks (containers)
 *     - Chains (containers within racks)
 *       - Devices or nested Racks
 */
class ChainTreeDialog : public juce::DialogWindow {
  public:
    explicit ChainTreeDialog(TrackId trackId);
    ~ChainTreeDialog() override;

    void closeButtonPressed() override;

    TrackId getTrackId() const {
        return trackId_;
    }

    /**
     * @brief Show the dialog for a given track
     * @param trackId The track whose chain to display
     */
    static void show(TrackId trackId);

  private:
    class ContentComponent;
    std::unique_ptr<ContentComponent> content_;
    TrackId trackId_;

    // Tracks the currently-open instance so repeat clicks re-focus instead
    // of spawning duplicate windows.
    static juce::Component::SafePointer<ChainTreeDialog> currentInstance_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChainTreeDialog)
};

}  // namespace magda
