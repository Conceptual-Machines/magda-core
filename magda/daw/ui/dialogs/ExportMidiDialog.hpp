#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace magda {

class ExportMidiDialog : public juce::Component {
  public:
    enum class ExportRange { EntireSong, TimeSelection, LoopRegion };

    struct Settings {
        juce::File outputFile;
        int midiFormat = 1;  // 0 = single track, 1 = multi-track
        ExportRange exportRange = ExportRange::EntireSong;
    };

    explicit ExportMidiDialog();
    ~ExportMidiDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    Settings getSettings() const;

    std::function<void(const Settings&)> onExport;

    void setTimeSelectionAvailable(bool available);
    void setLoopRegionAvailable(bool available);

    static void showDialog(juce::Component* parent,
                           std::function<void(const Settings&)> exportCallback,
                           bool hasTimeSelection = false, bool hasLoopRegion = false);

  private:
    // MIDI format selection
    juce::Label formatLabel_;
    juce::ComboBox formatComboBox_;

    // Export range
    juce::Label rangeLabel_;
    juce::ToggleButton exportEntireSongButton_;
    juce::ToggleButton exportTimeSelectionButton_;
    juce::ToggleButton exportLoopRegionButton_;

    // Buttons
    juce::TextButton exportButton_;
    juce::TextButton cancelButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportMidiDialog)
};

}  // namespace magda
