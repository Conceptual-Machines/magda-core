#pragma once

/**
 * @brief Interface for controlling DAW transport (playback control)
 * 
 * The TransportInterface provides methods for controlling playback,
 * recording, and position within the DAW timeline.
 */
class TransportInterface {
public:
    virtual ~TransportInterface() = default;
    
    /**
     * @brief Start playback
     */
    virtual void play() = 0;
    
    /**
     * @brief Stop playback
     */
    virtual void stop() = 0;
    
    /**
     * @brief Pause playback (can be resumed)
     */
    virtual void pause() = 0;
    
    /**
     * @brief Start recording
     */
    virtual void record() = 0;
    
    /**
     * @brief Set playback position in seconds
     * @param position_seconds The position to jump to
     */
    virtual void locate(double position_seconds) = 0;
    
    /**
     * @brief Set playback position in bars/beats
     * @param bar The bar number (1-based)
     * @param beat The beat within the bar (1-based)
     * @param tick The tick within the beat (0-based)
     */
    virtual void locateMusical(int bar, int beat, int tick = 0) = 0;
    
    /**
     * @brief Get current playback position in seconds
     */
    virtual double getCurrentPosition() const = 0;
    
    /**
     * @brief Get current musical position
     */
    virtual void getCurrentMusicalPosition(int& bar, int& beat, int& tick) const = 0;
    
    /**
     * @brief Check if currently playing
     */
    virtual bool isPlaying() const = 0;
    
    /**
     * @brief Check if currently recording
     */
    virtual bool isRecording() const = 0;
    
    /**
     * @brief Set tempo in BPM
     */
    virtual void setTempo(double bpm) = 0;
    
    /**
     * @brief Get current tempo in BPM
     */
    virtual double getTempo() const = 0;
    
    /**
     * @brief Set time signature
     * @param numerator Beats per bar
     * @param denominator Note value for each beat (4 = quarter note)
     */
    virtual void setTimeSignature(int numerator, int denominator) = 0;
    
    /**
     * @brief Get current time signature
     */
    virtual void getTimeSignature(int& numerator, int& denominator) const = 0;
    
    /**
     * @brief Enable/disable looping
     */
    virtual void setLooping(bool enabled) = 0;
    
    /**
     * @brief Set loop region in seconds
     */
    virtual void setLoopRegion(double start_seconds, double end_seconds) = 0;
    
    /**
     * @brief Check if looping is enabled
     */
    virtual bool isLooping() const = 0;
}; 