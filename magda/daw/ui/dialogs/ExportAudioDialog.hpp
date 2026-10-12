#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

#include "../../engine/AudioEngine.hpp"

namespace magda {

/**
 * @brief Dialog for exporting/bouncing audio to a file
 *
 * Provides options for:
 * - Audio format (WAV 16/24/32-bit, FLAC, Opus, MP3)
 * - Sample rate (44.1kHz, 48kHz, 96kHz, 192kHz)
 * - Dither (off, TPDF, noise shaped) for fixed-point formats
 * - Bitrate for lossy formats
 * - Normalization (peak to 0dB)
 * - Time range (entire arrangement or selection)
 */
class ExportAudioDialog : public juce::Component {
  public:
    enum class ExportRange { EntireSong, TimeSelection, LoopRegion };

    struct Settings {
        juce::File outputFile;
        juce::String format;            // "WAV", "FLAC", "OPUS", "MP3"
        int bitDepth = 24;              // WAV and FLAC; 32 is float
        int bitrateKbps = 0;            // Lossy formats only
        std::optional<int> vbrQuality;  // MP3 only: a LAME VBR level in place of a bitrate
        double sampleRate = 48000.0;
        OfflineRenderDither dither = OfflineRenderDither::Tpdf;
        bool normalize = false;
        bool realTimeRender = false;
        double leadInSilence = 0.0;  // Seconds of silence before audio (0-2s)
        ExportRange exportRange = ExportRange::EntireSong;
    };

    explicit ExportAudioDialog();
    ~ExportAudioDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    // The hosting window's background is a colour override handed over once at
    // construction, so it does not follow a live theme switch on its own.
    void lookAndFeelChanged() override;

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
    void updateFormatRows();
    bool isLossySelected() const;
    bool isMp3Selected() const;
    juce::String selectedFormat() const;

    /// Fills the bit depth combo with the selected format's depths, keeping the
    /// current one where the format offers it.
    void populateBitDepthOptions();

    /// Fills the bitrate combo with the selected lossy format's choices.
    void populateBitrateOptions();

    // Format selection
    juce::Label formatLabel_;
    juce::ComboBox formatComboBox_;

    // Sample rate selection
    juce::Label sampleRateLabel_;
    juce::ComboBox sampleRateComboBox_;

    // Bit depth, for the formats that have one; item ids are the depths
    juce::Label bitDepthLabel_;
    juce::ComboBox bitDepthComboBox_;

    // Dither, for the formats that round to a fixed-point grid
    juce::Label ditherLabel_;
    juce::ComboBox ditherComboBox_;

    // Bitrate, shown in place of bit depth and dither for a lossy format
    juce::Label bitrateLabel_;
    juce::ComboBox bitrateComboBox_;

    // Normalization option
    juce::ToggleButton normalizeCheckbox_;

    // Real-time render option
    juce::ToggleButton realTimeRenderCheckbox_;

    // Lead-in silence
    juce::Label leadInSilenceLabel_;
    juce::Slider leadInSilenceSlider_;

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
