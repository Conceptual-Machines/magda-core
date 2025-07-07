#pragma once

#include <string>

/**
 * @brief Interface for managing DAW modes and configurations
 * 
 * This interface handles the dual-mode system:
 * 1. View Mode: Performance vs Arrangement (like Ableton Live)
 * 2. Audio Mode: Live vs Studio (buffer sizes and CPU optimization)
 */
class DAWModeInterface {
public:
    virtual ~DAWModeInterface() = default;
    
    // === View Mode Management (Performance vs Arrangement) ===
    
    /**
     * @brief View modes available in the DAW
     */
    enum class ViewMode {
        Arrangement,  // Traditional timeline view
        Performance   // Session/clip launcher view
    };
    
    /**
     * @brief Switch between arrangement and performance views
     */
    virtual void setViewMode(ViewMode mode) = 0;
    
    /**
     * @brief Get current view mode
     */
    virtual ViewMode getViewMode() const = 0;
    
    /**
     * @brief Check if currently in arrangement mode
     */
    virtual bool isArrangementMode() const = 0;
    
    /**
     * @brief Check if currently in performance mode
     */
    virtual bool isPerformanceMode() const = 0;
    
    // === Audio Mode Management (Live vs Studio) ===
    
    /**
     * @brief Audio processing modes
     */
    enum class AudioMode {
        Live,   // Low latency, shorter buffers, real-time focus
        Studio  // Higher quality, larger buffers, CPU intensive
    };
    
    /**
     * @brief Switch between live and studio audio modes
     */
    virtual void setAudioMode(AudioMode mode) = 0;
    
    /**
     * @brief Get current audio mode
     */
    virtual AudioMode getAudioMode() const = 0;
    
    /**
     * @brief Check if currently in live mode
     */
    virtual bool isLiveMode() const = 0;
    
    /**
     * @brief Check if currently in studio mode
     */
    virtual bool isStudioMode() const = 0;
    
    // === Audio Configuration ===
    
    /**
     * @brief Get current buffer size in samples
     */
    virtual int getBufferSize() const = 0;
    
    /**
     * @brief Get current sample rate
     */
    virtual int getSampleRate() const = 0;
    
    /**
     * @brief Get current latency in milliseconds
     */
    virtual double getLatencyMs() const = 0;
    
    /**
     * @brief Get CPU usage percentage
     */
    virtual double getCpuUsage() const = 0;
    
    // === Performance Mode Specific ===
    
    /**
     * @brief Launch a clip in performance mode
     * @param clip_id The clip to launch
     * @param quantize_beats Quantization in beats (0 = immediate, 1 = next beat, etc.)
     */
    virtual void launchClip(const std::string& clip_id, double quantize_beats = 1.0) = 0;
    
    /**
     * @brief Stop a clip in performance mode
     * @param clip_id The clip to stop
     * @param quantize_beats Quantization in beats
     */
    virtual void stopClip(const std::string& clip_id, double quantize_beats = 0.0) = 0;
    
    /**
     * @brief Get all clips available for performance mode
     */
    virtual std::vector<std::string> getPerformanceClips() const = 0;
    
    /**
     * @brief Get currently playing clips in performance mode
     */
    virtual std::vector<std::string> getPlayingClips() const = 0;
    
    // === Mode Change Events ===
    
    /**
     * @brief Callback for view mode changes
     */
    using ViewModeChangedCallback = std::function<void(ViewMode new_mode)>;
    
    /**
     * @brief Register callback for view mode changes
     */
    virtual void onViewModeChanged(ViewModeChangedCallback callback) = 0;
    
    /**
     * @brief Callback for audio mode changes
     */
    using AudioModeChangedCallback = std::function<void(AudioMode new_mode)>;
    
    /**
     * @brief Register callback for audio mode changes
     */
    virtual void onAudioModeChanged(AudioModeChangedCallback callback) = 0;
}; 