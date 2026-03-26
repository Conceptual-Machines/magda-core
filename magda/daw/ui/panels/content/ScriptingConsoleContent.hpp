#pragma once

#include "PanelContent.hpp"

namespace magda::daw::ui {

/**
 * @brief Scripting console panel content (placeholder)
 *
 * The DSL REPL is now part of the AI Chat console (DSL tab).
 * This panel is kept for backward compatibility.
 */
class ScriptingConsoleContent : public PanelContent {
  public:
    ScriptingConsoleContent();
    ~ScriptingConsoleContent() override = default;

    PanelContentType getContentType() const override {
        return PanelContentType::ScriptingConsole;
    }

    PanelContentInfo getContentInfo() const override {
        return {PanelContentType::ScriptingConsole, "Script", "Script editor/REPL", "Script"};
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void onActivated() override;
    void onDeactivated() override;

  private:
    juce::Label placeholderLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScriptingConsoleContent)
};

}  // namespace magda::daw::ui
