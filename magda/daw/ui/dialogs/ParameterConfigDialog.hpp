#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "core/DeviceInfo.hpp"
#include "core/ParameterDetector.hpp"

namespace magda::daw::ui {

/**
 * @brief Mock parameter info for UI mockup
 */
struct MockParameterInfo {
    juce::String name;
    float defaultValue = 0.5f;
    bool isVisible = true;
    juce::String unit;  // Hz, dB, ms, %, semitones, custom
    float rangeMin = 0.0f;
    float rangeMax = 1.0f;
    float rangeCenter = 0.5f;
    magda::ParameterScale scale = magda::ParameterScale::Linear;
    std::vector<juce::String> choices;     // For discrete params
    std::vector<juce::String> valueTable;  // Full getText() lookup table
};

/**
 * @brief Dialog for configuring plugin parameters
 *
 * Shows a table with columns:
 * - Parameter name
 * - Visible toggle
 * - Custom unit
 * - Custom range (min/max/center)
 */
class ParameterConfigDialog : public juce::Component,
                              public juce::TableListBoxModel,
                              private juce::Timer {
  public:
    ParameterConfigDialog(const juce::String& pluginName);
    ~ParameterConfigDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // TableListBoxModel interface
    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height,
                            bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height,
                   bool rowIsSelected) override;
    Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected,
                                       Component* existingComponentToUpdate) override;

    // Show dialog modally
    static void show(const juce::String& pluginName, juce::Component* parent);

    // Show dialog for a specific plugin (loads real parameters)
    static void showForPlugin(const juce::String& uniqueId, const juce::String& pluginName,
                              juce::Component* parent);

    // Load saved parameter configuration and apply to DeviceInfo
    static bool applyConfigToDevice(const juce::String& uniqueId, magda::DeviceInfo& device);

  private:
    juce::String pluginName_;
    juce::String pluginUniqueId_;  // For saving/loading parameter configuration
    std::vector<MockParameterInfo> parameters_;
    std::vector<int> filteredIndices_;  // Indices of filtered parameters
    juce::String currentSearchText_;

    juce::TableListBox table_;
    juce::TextButton okButton_;
    juce::TextButton cancelButton_;
    juce::TextButton applyButton_;
    juce::TextButton selectAllButton_;
    juce::TextButton deselectAllButton_;
    juce::TextButton aiDetectButton_;
    juce::Label aiStatusLabel_;
    bool detecting_ = false;
    int dotCount_ = 0;
    int aiTotal_ = 0;
    int aiResolved_ = 0;
    std::shared_ptr<std::atomic<bool>> cancelFlag_;
    juce::Label titleLabel_;
    juce::TextEditor searchBox_;
    juce::Label searchLabel_;

    // Column IDs
    enum ColumnIds { ParamName = 1, Visible, Unit, Range };

    // Scan inputs cached from loadParameters for detection
    std::vector<magda::ParameterScanInput> scanInputs_;

    void timerCallback() override;
    void setDetecting(bool detecting);
    void updateTitle();
    void buildMockParameters();
    void loadParameters(const juce::String& uniqueId);
    void runDetection();
    void applyDetectionResults(const std::vector<magda::DetectedParameterInfo>& results);
    void saveParameterConfiguration();
    void loadParameterConfiguration();
    void selectAllParameters();
    void deselectAllParameters();
    void filterParameters(const juce::String& searchText);
    void rebuildFilteredList();
    int getParamIndexForRow(int row) const;

    // Custom cell components
    class ToggleCell;
    class ComboCell;
    class RangeCell;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterConfigDialog)
};

}  // namespace magda::daw::ui
