#pragma once

#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include <map>

#include "../core/TypeIds.hpp"

namespace magda {

// Forward declarations
namespace te = tracktion;

/**
 * @brief Manages mixer controls (volume and pan) for tracks and master channel
 *
 * Responsibilities:
 * - Track volume/pan control (linear gain and pan position)
 * - Master volume/pan control
 * - Conversion between linear gain and decibels
 *
 * Thread Safety:
 * - All operations run on message thread (UI thread)
 * - Delegates to Tracktion Engine's VolumeAndPanPlugin
 */
class MixerController {
  public:
    MixerController() = default;
    ~MixerController() = default;

    /**
     * @brief Set track volume (linear gain)
     * @param edit Tracktion Engine edit
     * @param trackMapping Map from TrackId to TE AudioTrack pointer
     * @param trackId The track ID
     * @param volume Linear gain (0.0 = silence, 1.0 = unity, 2.0 = +6dB)
     */
    void setTrackVolume(te::Edit& edit, const std::map<TrackId, te::AudioTrack*>& trackMapping,
                        TrackId trackId, float volume);

    /**
     * @brief Get track volume (linear gain)
     * @param edit Tracktion Engine edit
     * @param trackMapping Map from TrackId to TE AudioTrack pointer
     * @param trackId The track ID
     * @return Linear gain (1.0 = unity)
     */
    float getTrackVolume(te::Edit& edit, const std::map<TrackId, te::AudioTrack*>& trackMapping,
                         TrackId trackId) const;

    /**
     * @brief Set track pan position
     * @param edit Tracktion Engine edit
     * @param trackMapping Map from TrackId to TE AudioTrack pointer
     * @param trackId The track ID
     * @param pan Pan position (-1.0 = full left, 0.0 = center, 1.0 = full right)
     */
    void setTrackPan(te::Edit& edit, const std::map<TrackId, te::AudioTrack*>& trackMapping,
                     TrackId trackId, float pan);

    /**
     * @brief Get track pan position
     * @param edit Tracktion Engine edit
     * @param trackMapping Map from TrackId to TE AudioTrack pointer
     * @param trackId The track ID
     * @return Pan position (-1.0 to 1.0, 0.0 = center)
     */
    float getTrackPan(te::Edit& edit, const std::map<TrackId, te::AudioTrack*>& trackMapping,
                      TrackId trackId) const;

    /**
     * @brief Set master channel volume (linear gain)
     * @param edit Tracktion Engine edit
     * @param volume Linear gain (0.0 = silence, 1.0 = unity, 2.0 = +6dB)
     */
    void setMasterVolume(te::Edit& edit, float volume);

    /**
     * @brief Get master channel volume (linear gain)
     * @param edit Tracktion Engine edit
     * @return Linear gain (1.0 = unity)
     */
    float getMasterVolume(te::Edit& edit) const;

    /**
     * @brief Set master channel pan position
     * @param edit Tracktion Engine edit
     * @param pan Pan position (-1.0 = full left, 0.0 = center, 1.0 = full right)
     */
    void setMasterPan(te::Edit& edit, float pan);

    /**
     * @brief Get master channel pan position
     * @param edit Tracktion Engine edit
     * @return Pan position (-1.0 to 1.0, 0.0 = center)
     */
    float getMasterPan(te::Edit& edit) const;
};

}  // namespace magda
