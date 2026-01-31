#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda {

/**
 * @brief Dialog for exporting/bouncing audio to a file
 *
 * Provides options for:
 * - Audio format (WAV 16/24/32-bit, FLAC)
 * - Sample rate (44.1kHz, 48kHz, 96kHz, 192kHz)
 * - Normalization (peak to 0dB)
 * - Time range (entire arrangement or selection)
 */
class ExportAudioDialog : public juce::Component {
  public:
    enum class ExportRange { EntireSong, TimeSelection, LoopRegion };

    struct Settings {
        juce::File outputFile;
        juce::String format;  // "WAV16", "WAV24", "WAV32", "FLAC"
        double sampleRate;
        bool normalize;
        ExportRange exportRange;
    };

    explicit ExportAudioDialog();
    ~ExportAudioDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // Get current settings from dialog
    Settings getSettings() const;

    // Callback when user clicks Export button
    std::function<void(const Settings&)> onExport;

    // Set which export options should be enabled
    void setTimeSelectionAvailable(bool available);
    void setLoopRegionAvailable(bool available);

    // Static method to show as modal dialog
    static void showDialog(juce::Component* parent,
                           std::function<void(const Settings&)> exportCallback,
                           bool hasTimeSelection = false, bool hasLoopRegion = false);

  private:
    void onFormatChanged();
    void updateBitDepthOptions();

    // Format selection
    juce::Label formatLabel_;
    juce::ComboBox formatComboBox_;

    // Sample rate selection
    juce::Label sampleRateLabel_;
    juce::ComboBox sampleRateComboBox_;

    // Bit depth (auto-populated based on format)
    juce::Label bitDepthLabel_;
    juce::Label bitDepthValueLabel_;

    // Normalization option
    juce::ToggleButton normalizeCheckbox_;

    // Time range options
    juce::Label timeRangeLabel_;
    juce::ToggleButton exportEntireSongButton_;
    juce::ToggleButton exportTimeSelectionButton_;
    juce::ToggleButton exportLoopRegionButton_;

    // Buttons
    juce::TextButton exportButton_;
    juce::TextButton cancelButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportAudioDialog)
};

}  // namespace magda
