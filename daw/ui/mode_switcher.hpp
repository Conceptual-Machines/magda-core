#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../interfaces/daw_mode_interface.hpp"

/**
 * @brief UI component for switching between DAW modes
 * 
 * Provides buttons to switch between:
 * - View modes: Arrangement vs Performance
 * - Audio modes: Live vs Studio
 */
class ModeSwitcher : public juce::Component {
public:
    ModeSwitcher();
    ~ModeSwitcher() override = default;
    
    /**
     * @brief Set the DAW mode interface to control
     */
    void setDAWModeInterface(DAWModeInterface* interface);
    
    /**
     * @brief Update the UI to reflect current mode states
     */
    void updateModeDisplay();
    
    // juce::Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;
    
private:
    // UI Components
    juce::TextButton arrangementButton_;
    juce::TextButton performanceButton_;
    juce::TextButton liveButton_;
    juce::TextButton studioButton_;
    
    // Mode indicators
    juce::Label viewModeLabel_;
    juce::Label audioModeLabel_;
    juce::Label latencyLabel_;
    juce::Label cpuLabel_;
    
    // DAW interface
    DAWModeInterface* dawModeInterface_ = nullptr;
    
    // Button event handlers
    void onArrangementClicked();
    void onPerformanceClicked();
    void onLiveClicked();
    void onStudioClicked();
    
    // Update timer for real-time stats
    void timerCallback() override;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModeSwitcher)
}; 