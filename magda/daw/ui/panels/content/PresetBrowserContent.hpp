#pragma once

#include "PanelContent.hpp"
#include "SearchTextEditor.hpp"

namespace magda::daw::ui {

/**
 * @brief Preset browser panel content
 *
 * Browse and manage presets for plugins and instruments.
 */
class PresetBrowserContent : public PanelContent {
  public:
    PresetBrowserContent();
    ~PresetBrowserContent() override = default;

    PanelContentType getContentType() const override {
        return PanelContentType::PresetBrowser;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::PresetBrowser, "Presets", "Browse presets", "Preset"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    /** @brief Cmd+F focuses the search box while the panel has focus. */
    bool keyPressed(const juce::KeyPress& key) override;

    void onActivated() override;
    void onDeactivated() override;

  private:
    SearchTextEditor searchBox_{"Search presets"};
    juce::Label titleLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowserContent)
};

}  // namespace magda::daw::ui
