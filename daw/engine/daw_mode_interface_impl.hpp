#pragma once

#include "../interfaces/daw_mode_interface.hpp"
#include <tracktion_engine/tracktion_engine.h>
#include <functional>
#include <vector>

namespace magica {

/**
 * @brief Implementation of DAW mode interface using Tracktion Engine
 * 
 * Handles:
 * - View mode switching (Arrangement vs Performance)
 * - Audio mode switching (Live vs Studio)
 * - Real-time audio configuration
 * - Performance clip launching
 */
class DAWModeInterfaceImpl : public DAWModeInterface {
public:
    explicit DAWModeInterfaceImpl(tracktion::Engine* engine, tracktion::Edit* edit);
    ~DAWModeInterfaceImpl() override = default;
    
    // === View Mode Implementation ===
    void setViewMode(ViewMode mode) override;
    ViewMode getViewMode() const override { return currentViewMode_; }
    bool isArrangementMode() const override { return currentViewMode_ == ViewMode::Arrangement; }
    bool isPerformanceMode() const override { return currentViewMode_ == ViewMode::Performance; }
    
    // === Audio Mode Implementation ===
    void setAudioMode(AudioMode mode) override;
    AudioMode getAudioMode() const override { return currentAudioMode_; }
    bool isLiveMode() const override { return currentAudioMode_ == AudioMode::Live; }
    bool isStudioMode() const override { return currentAudioMode_ == AudioMode::Studio; }
    
    // === Audio Configuration ===
    int getBufferSize() const override;
    int getSampleRate() const override;
    double getLatencyMs() const override;
    double getCpuUsage() const override;
    
    // === Performance Mode Implementation ===
    void launchClip(const std::string& clip_id, double quantize_beats = 1.0) override;
    void stopClip(const std::string& clip_id, double quantize_beats = 0.0) override;
    std::vector<std::string> getPerformanceClips() const override;
    std::vector<std::string> getPlayingClips() const override;
    
    // === Event Callbacks ===
    void onViewModeChanged(ViewModeChangedCallback callback) override;
    void onAudioModeChanged(AudioModeChangedCallback callback) override;
    
private:
    // Tracktion Engine components
    tracktion::Engine* engine_;
    tracktion::Edit* edit_;
    
    // Current modes
    ViewMode currentViewMode_ = ViewMode::Arrangement;
    AudioMode currentAudioMode_ = AudioMode::Studio;
    
    // Audio configuration
    struct AudioConfig {
        int bufferSize = 512;
        int sampleRate = 44100;
        double latencyMs = 11.6; // 512 samples at 44.1kHz
    };
    
    AudioConfig liveConfig_ = {256, 44100, 5.8};   // Low latency
    AudioConfig studioConfig_ = {1024, 48000, 21.3}; // High quality
    
    // Performance mode state
    std::vector<std::string> performanceClips_;
    std::vector<std::string> playingClips_;
    
    // Callbacks
    std::vector<ViewModeChangedCallback> viewModeCallbacks_;
    std::vector<AudioModeChangedCallback> audioModeCallbacks_;
    
    // Helper methods
    void applyAudioConfiguration(const AudioConfig& config);
    void notifyViewModeChanged();
    void notifyAudioModeChanged();
    void updatePerformanceClips();
    
    // Tracktion Engine helpers
    tracktion::Clip* findClipById(const std::string& clip_id) const;
    void scheduleClipLaunch(tracktion::Clip* clip, double quantize_beats);
    void scheduleClipStop(tracktion::Clip* clip, double quantize_beats);
};

} // namespace magica 