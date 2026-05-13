#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace magda::daw::ui {

// Modeless top-level editor window for a Faust .dsp source. Compile button
// invokes the supplied callback; errors render inline in a label below the
// editor so the user can iterate without dialog popups.
class FaustCodeEditorWindow : public juce::DocumentWindow {
  public:
    using CompileFn = std::function<bool(const juce::String& source, juce::String& errorOut)>;

    FaustCodeEditorWindow(const juce::String& title, const juce::String& initialSource,
                          CompileFn onCompile);
    ~FaustCodeEditorWindow() override;

    void closeButtonPressed() override;

  private:
    class Content;
    std::unique_ptr<Content> content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustCodeEditorWindow)
};

}  // namespace magda::daw::ui
