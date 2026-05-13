#include "custom_ui/FaustCodeEditorWindow.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

class FaustCodeEditorWindow::Content : public juce::Component {
  public:
    Content(const juce::String& initialSource, CompileFn onCompile)
        : editor_(document_, nullptr), onCompile_(std::move(onCompile)) {
        document_.replaceAllContent(initialSource);

        editor_.setColour(juce::CodeEditorComponent::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
        editor_.setColour(juce::CodeEditorComponent::defaultTextColourId,
                          DarkTheme::getTextColour());
        editor_.setFont(FontManager::getInstance().getMonoFont(12.0f));
        addAndMakeVisible(editor_);

        compileBtn_.setButtonText("Compile");
        compileBtn_.onClick = [this] { compile(); };
        addAndMakeVisible(compileBtn_);

        statusLabel_.setFont(FontManager::getInstance().getMonoFont(11.0f));
        statusLabel_.setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(statusLabel_);

        setSize(720, 540);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(8);
        auto bottom = area.removeFromBottom(80);
        compileBtn_.setBounds(bottom.removeFromTop(28).removeFromLeft(120));
        bottom.removeFromTop(4);
        statusLabel_.setBounds(bottom);
        editor_.setBounds(area);
    }

  private:
    void compile() {
        const auto src = document_.getAllContent();
        juce::String err;
        if (onCompile_ && onCompile_(src, err)) {
            statusLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
            statusLabel_.setText("Compiled OK", juce::dontSendNotification);
        } else {
            statusLabel_.setColour(juce::Label::textColourId, juce::Colours::red);
            statusLabel_.setText(err, juce::dontSendNotification);
        }
    }

    juce::CodeDocument document_;
    juce::CodeEditorComponent editor_;
    juce::TextButton compileBtn_;
    juce::Label statusLabel_;
    CompileFn onCompile_;
};

FaustCodeEditorWindow::FaustCodeEditorWindow(const juce::String& title,
                                             const juce::String& initialSource, CompileFn onCompile)
    : juce::DocumentWindow(title, DarkTheme::getColour(DarkTheme::BACKGROUND),
                           juce::DocumentWindow::allButtons) {
    content_ = std::make_unique<Content>(initialSource, std::move(onCompile));
    setContentNonOwned(content_.get(), true);
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    centreWithSize(720, 540);
    setVisible(true);
}

FaustCodeEditorWindow::~FaustCodeEditorWindow() {
    setContentNonOwned(nullptr, false);
}

void FaustCodeEditorWindow::closeButtonPressed() {
    setVisible(false);
}

}  // namespace magda::daw::ui
