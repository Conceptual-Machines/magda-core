#pragma once

#include <string>
#include <vector>

/**
 * @brief Interface for managing DAW tracks
 * 
 * The TrackInterface provides methods for creating, deleting, and configuring
 * audio and MIDI tracks within the DAW.
 */
class TrackInterface {
public:
    virtual ~TrackInterface() = default;
    
    /**
     * @brief Create a new audio track
     * @param name The name of the track
     * @return Track ID
     */
    virtual std::string createAudioTrack(const std::string& name) = 0;
    
    /**
     * @brief Create a new MIDI track
     * @param name The name of the track
     * @return Track ID
     */
    virtual std::string createMidiTrack(const std::string& name) = 0;
    
    /**
     * @brief Delete a track
     * @param track_id The ID of the track to delete
     */
    virtual void deleteTrack(const std::string& track_id) = 0;
    
    /**
     * @brief Set track name
     */
    virtual void setTrackName(const std::string& track_id, const std::string& name) = 0;
    
    /**
     * @brief Get track name
     */
    virtual std::string getTrackName(const std::string& track_id) const = 0;
    
    /**
     * @brief Mute/unmute a track
     */
    virtual void setTrackMuted(const std::string& track_id, bool muted) = 0;
    
    /**
     * @brief Check if track is muted
     */
    virtual bool isTrackMuted(const std::string& track_id) const = 0;
    
    /**
     * @brief Solo/unsolo a track
     */
    virtual void setTrackSolo(const std::string& track_id, bool solo) = 0;
    
    /**
     * @brief Check if track is soloed
     */
    virtual bool isTrackSolo(const std::string& track_id) const = 0;
    
    /**
     * @brief Arm/disarm track for recording
     */
    virtual void setTrackArmed(const std::string& track_id, bool armed) = 0;
    
    /**
     * @brief Check if track is armed for recording
     */
    virtual bool isTrackArmed(const std::string& track_id) const = 0;
    
    /**
     * @brief Set track color (RGB values 0-255)
     */
    virtual void setTrackColor(const std::string& track_id, int r, int g, int b) = 0;
    
    /**
     * @brief Get list of all track IDs
     */
    virtual std::vector<std::string> getAllTrackIds() const = 0;
    
    /**
     * @brief Check if track exists
     */
    virtual bool trackExists(const std::string& track_id) const = 0;
}; 