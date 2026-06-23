#pragma once

#include <functional>
#include <vector>

#include "SvgButton.hpp"
#include "core/TrackInfo.hpp"
#include "core/TypeIds.hpp"

namespace magda {

// Track input-monitor control: a single monitor glyph cycling the track's
// InputMonitorMode.
//
//   Off  - off glyph on the surface chip (grey)
//   In   - on glyph on a green chip  (monitor input always)
//   Auto - on glyph on a blue chip   (monitor input only while armed)
//
// Left-click cycles Off -> In -> Auto; right-click opens a dropdown spelling out
// the modes. Like ChordAuditionControl it is stateless: it reads the mode from
// TrackManager and writes changes through the undo system, so every surface that
// shows it (track header, inspector, chain header, mixer) stays in sync.
class MonitorControl : public SvgButton {
  public:
    MonitorControl();

    // Supplies the track this control acts on (INVALID_TRACK_ID if none). Its
    // mode drives the cycle and the glyph.
    std::function<TrackId()> getTrackId;

    // Optional: the full set of tracks a change applies to (e.g. a multi-track
    // selection). Defaults to just getTrackId() when unset.
    std::function<std::vector<TrackId>()> getTargets;

    // Re-read the track's monitor mode and repaint the glyph/chip to match.
    void refresh();

    void mouseDown(const juce::MouseEvent& e) override;

  private:
    void applyMode(InputMonitorMode mode);
    void showModeMenu();
    void updateVisual(InputMonitorMode mode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MonitorControl)
};

}  // namespace magda
