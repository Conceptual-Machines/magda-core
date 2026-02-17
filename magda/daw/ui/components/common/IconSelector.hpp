#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace magda::daw::ui {

/**
 * @brief Horizontal row of icon buttons with radio-button behavior
 *
 * Used for waveform selection, filter type selection, etc.
 * Each option is a small icon drawn from SVG data.
 * Exactly one option is selected at a time.
 */
class IconSelector : public juce::Component, public juce::SettableTooltipClient {
  public:
    IconSelector() = default;

    /**
     * @brief Add an option with SVG icon data
     * @param svgData Pointer to SVG binary data (from BinaryData)
     * @param svgSize Size of SVG data
     * @param tooltip Optional tooltip text
     */
    void addOption(const char* svgData, int svgSize, const juce::String& tooltip = {});

    /**
     * @brief Add a text-only option (no icon)
     * @param text Display text for this option
     * @param tooltip Optional tooltip text
     */
    void addTextOption(const juce::String& text, const juce::String& tooltip = {});

    /**
     * @brief Set the selected index
     * @param index 0-based index into options
     * @param notification Whether to fire onChange callback
     */
    void setSelectedIndex(int index, juce::NotificationType notification = juce::sendNotification);

    int getSelectedIndex() const {
        return selectedIndex_;
    }
    int getNumOptions() const {
        return static_cast<int>(options_.size());
    }

    /** Callback fired when selection changes */
    std::function<void(int)> onChange;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

  private:
    struct Option {
        std::unique_ptr<juce::Drawable> icon;
        juce::String text;  // Display text (used when icon is null)
        juce::String tooltip;
    };

    std::vector<Option> options_;
    int selectedIndex_ = 0;
    int hoveredIndex_ = -1;

    juce::Rectangle<int> getOptionBounds(int index) const;
    int hitTest(juce::Point<int> pos) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IconSelector)
};

}  // namespace magda::daw::ui
