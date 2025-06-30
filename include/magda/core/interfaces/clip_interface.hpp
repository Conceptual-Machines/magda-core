#pragma once

#include <string>
#include <vector>

/**
 * @brief Represents a MIDI note
 */
struct MidiNote {
    int note;           // MIDI note number (0-127)
    int velocity;       // Velocity (0-127)
    double start;       // Start time within clip (in beats)
    double duration;    // Duration (in beats)
    
    MidiNote(int n, int v, double s, double d) 
        : note(n), velocity(v), start(s), duration(d) {}
};

/**
 * @brief Interface for managing clips (MIDI and audio segments)
 * 
 * The ClipInterface provides methods for creating, editing, and manipulating
 * clips within tracks. Clips are timed segments that contain MIDI data or
 * reference audio files.
 */
class ClipInterface {
public:
    virtual ~ClipInterface() = default;
    
    /**
     * @brief Add a MIDI clip to a track
     * @param track_id The target track ID
     * @param start_time Start time in seconds
     * @param length Length in seconds
     * @param notes Vector of MIDI notes
     * @return Clip ID
     */
    virtual std::string addMidiClip(const std::string& track_id, 
                                   double start_time, 
                                   double length, 
                                   const std::vector<MidiNote>& notes) = 0;
    
    /**
     * @brief Add an audio clip to a track
     * @param track_id The target track ID
     * @param start_time Start time in seconds
     * @param audio_file_path Path to the audio file
     * @return Clip ID
     */
    virtual std::string addAudioClip(const std::string& track_id,
                                    double start_time,
                                    const std::string& audio_file_path) = 0;
    
    /**
     * @brief Delete a clip
     * @param clip_id The clip ID to delete
     */
    virtual void deleteClip(const std::string& clip_id) = 0;
    
    /**
     * @brief Move a clip to a new position
     * @param clip_id The clip to move
     * @param new_start_time New start time in seconds
     */
    virtual void moveClip(const std::string& clip_id, double new_start_time) = 0;
    
    /**
     * @brief Resize a clip
     * @param clip_id The clip to resize
     * @param new_length New length in seconds
     */
    virtual void resizeClip(const std::string& clip_id, double new_length) = 0;
    
    /**
     * @brief Get clip start time
     */
    virtual double getClipStartTime(const std::string& clip_id) const = 0;
    
    /**
     * @brief Get clip length
     */
    virtual double getClipLength(const std::string& clip_id) const = 0;
    
    /**
     * @brief Add a note to a MIDI clip
     * @param clip_id The MIDI clip ID
     * @param note The note to add
     */
    virtual void addNoteToMidiClip(const std::string& clip_id, const MidiNote& note) = 0;
    
    /**
     * @brief Remove notes from a MIDI clip
     * @param clip_id The MIDI clip ID
     * @param start_time Start of range to clear (in beats within clip)
     * @param end_time End of range to clear (in beats within clip)
     */
    virtual void removeNotesFromMidiClip(const std::string& clip_id, 
                                        double start_time, 
                                        double end_time) = 0;
    
    /**
     * @brief Get all notes from a MIDI clip
     */
    virtual std::vector<MidiNote> getMidiClipNotes(const std::string& clip_id) const = 0;
    
    /**
     * @brief Get all clips on a track
     */
    virtual std::vector<std::string> getTrackClips(const std::string& track_id) const = 0;
    
    /**
     * @brief Check if clip exists
     */
    virtual bool clipExists(const std::string& clip_id) const = 0;
}; 