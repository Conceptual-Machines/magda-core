#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace tracktion { inline namespace engine { class AutomatableParameter; }}

namespace magda::daw::audio {
class FaustPlugin;
}

namespace magda::daw::ui {

// Generic UI for FaustPlugin: a header row with the loaded DSP name and a
// "Load .dsp" button, plus a row of sliders for whatever
// AutomatableParameters the plugin currently exposes — so the same UI works
// for any .dsp the FaustPlugin happens to host.
class FaustUI : public juce::Component, private juce::Timer {
  public:
    FaustUI();
    ~FaustUI() override;

    void setPlugin(magda::daw::audio::FaustPlugin* plugin);

    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    void timerCallback() override;
    void rebuildFromPlugin();
    void showLoadMenu();
    void loadFromFile();
    void showCodeEditor();
    bool tryLoad(const juce::String& name, const juce::String& source);

    struct ParamSlot {
        juce::Label label;
        juce::Slider slider;
        tracktion::engine::AutomatableParameter* param = nullptr;
    };

    magda::daw::audio::FaustPlugin* plugin_ = nullptr;
    std::vector<std::unique_ptr<ParamSlot>> slots_;
    juce::Label nameLabel_;
    juce::Label errorLabel_;
    juce::TextButton loadButton_{"Load"};
    juce::TextButton editButton_{"Edit"};
    std::unique_ptr<juce::FileChooser> fileChooser_;
    std::unique_ptr<class FaustCodeEditorWindow> editorWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaustUI)
};

}  // namespace magda::daw::ui
