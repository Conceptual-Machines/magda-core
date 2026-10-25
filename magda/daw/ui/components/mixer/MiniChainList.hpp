#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "MiniChainRow.hpp"
#include "core/TrackManager.hpp"

namespace magda {

/**
 * @brief The compact FX chain of one track: a MiniChainRow per top-level element.
 *
 * Shared by the mixer strips and the track inspector (#1002). A rack collapses to one
 * row labelled with its name.
 */
class MiniChainList : public juce::Component {
  public:
    static constexpr int kRowGap = 1;

    /// Rebuilds the rows only when the track's top-level elements changed; the owner
    /// relayouts after, since the height may have changed.
    void sync(const TrackInfo& track, AudioEngine* engine);

    /// Re-entrant from a row's own bypass toggle: repaints, never rebuilds.
    void syncBypass(DeviceId deviceId, bool bypassed);

    /// Sum of the rows' heights, each followed by kRowGap.
    int preferredHeight() const;

    bool isEmpty() const {
        return rows_.empty();
    }

    void resized() override;

    /// Fires while a row expands or collapses, so the owner can relayout.
    std::function<void()> onHeightChanged;

  private:
    struct SignatureEntry {
        bool isRack = false;
        int id = INVALID_DEVICE_ID;
        juce::String name;
        std::vector<int> miniMixerParameters;

        bool operator==(const SignatureEntry&) const = default;
    };

    static std::vector<SignatureEntry> buildSignature(const TrackInfo& track);
    void rebuild(const TrackInfo& track, AudioEngine* engine);

    std::vector<std::unique_ptr<MiniChainRow>> rows_;
    std::vector<SignatureEntry> signature_;
    TrackId trackId_ = INVALID_TRACK_ID;
};

}  // namespace magda
