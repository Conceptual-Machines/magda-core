#include "custom_ui/FaustCodeEditorWindow.hpp"

#include "ui/code/FaustTokeniser.hpp"
#include "ui/code/SyntaxTheme.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/DialogLookAndFeel.hpp"
#include "ui/themes/FontManager.hpp"

namespace magda::daw::ui {

class FaustCodeEditorWindow::Content : public juce::Component {
  public:
    Content(const juce::String& initialSource, CompileFn onCompile)
        : editor_(document_, &tokeniser_), onCompile_(std::move(onCompile)) {
        document_.replaceAllContent(initialSource);

        // Faust source is syntax-highlighted through the shared code vocabulary,
        // so the editor surface and its token colours both follow the active
        // theme's syntaxColours (#1936).
        applyCodeEditorTheme(editor_, tokeniser_);
        editor_.setFont(FontManager::getInstance().getMonoFont(12.0f));
        addAndMakeVisible(editor_);

        compileBtn_.setButtonText("Compile");
        compileBtn_.setLookAndFeel(&lnf_);  // theme font + button styling
        compileBtn_.onClick = [this] { compile(); };
        addAndMakeVisible(compileBtn_);

        // Compiler diagnostics can span several lines and are often useful as
        // input to an AI assistant. A Label cannot be selected, so use a
        // read-only TextEditor: it keeps the output scrollable and allows
        // copying either a selected excerpt (Cmd/Ctrl+C) or the whole report.
        statusOutput_.setMultiLine(true);
        statusOutput_.setReadOnly(true);
        statusOutput_.setScrollbarsShown(true);
        statusOutput_.setCaretVisible(false);
        statusOutput_.setFont(FontManager::getInstance().getMonoFont(11.0f));
        statusOutput_.setColour(juce::TextEditor::backgroundColourId,
                                DarkTheme::getSyntaxColour(SyntaxColourRole::EDITOR_BACKGROUND));
        statusOutput_.setColour(juce::TextEditor::textColourId,
                                DarkTheme::getSyntaxColour(SyntaxColourRole::DSL_OUTPUT_TEXT));
        statusOutput_.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        statusOutput_.setColour(juce::TextEditor::focusedOutlineColourId,
                                juce::Colours::transparentBlack);
        statusOutput_.setColour(juce::TextEditor::highlightColourId,
                                DarkTheme::getColour(DarkTheme::ACCENT_PRIMARY).withAlpha(0.45f));
        statusOutput_.setColour(juce::TextEditor::highlightedTextColourId,
                                DarkTheme::getTextColour());
        statusOutput_.setTooltip("Select compiler output and press Cmd/Ctrl+C to copy");
        addAndMakeVisible(statusOutput_);

        copyStatusBtn_.setButtonText("Copy");
        copyStatusBtn_.setLookAndFeel(&lnf_);
        copyStatusBtn_.setTooltip("Copy all compiler output to the clipboard");
        copyStatusBtn_.setEnabled(false);
        copyStatusBtn_.onClick = [this] {
            juce::SystemClipboard::copyTextToClipboard(statusOutput_.getText());
        };
        addAndMakeVisible(copyStatusBtn_);

        setSize(720, 540);
    }

    ~Content() override {
        compileBtn_.setLookAndFeel(nullptr);
        copyStatusBtn_.setLookAndFeel(nullptr);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(8);
        auto bottom = area.removeFromBottom(112);
        auto actions = bottom.removeFromTop(28);
        compileBtn_.setBounds(actions.removeFromLeft(120));
        actions.removeFromLeft(4);
        copyStatusBtn_.setBounds(actions.removeFromLeft(90));
        bottom.removeFromTop(4);
        statusOutput_.setBounds(bottom);
        editor_.setBounds(area);
    }

  private:
    void compile() {
        const auto src = document_.getAllContent();
        juce::String err;
        if (onCompile_ && onCompile_(src, err)) {
            setCompilerOutput("Compiled OK",
                              DarkTheme::getSyntaxColour(SyntaxColourRole::DSL_OUTPUT_PROMPT));
        } else {
            setCompilerOutput(err, DarkTheme::getSyntaxColour(SyntaxColourRole::DSL_OUTPUT_ERROR));
        }
    }

    void setCompilerOutput(const juce::String& output, juce::Colour colour) {
        statusOutput_.setColour(juce::TextEditor::textColourId, colour);
        statusOutput_.setText(output, false);
        copyStatusBtn_.setEnabled(output.isNotEmpty());
    }

    DialogLookAndFeel lnf_;  // declared before compileBtn_ so it outlives it
    juce::CodeDocument document_;
    FaustTokeniser tokeniser_;  // declared before editor_, which holds a pointer to it
    juce::CodeEditorComponent editor_;
    juce::TextButton compileBtn_;
    juce::TextButton copyStatusBtn_;
    juce::TextEditor statusOutput_;
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

void FaustCodeEditorWindow::lookAndFeelChanged() {
    juce::DocumentWindow::lookAndFeelChanged();
    setBackgroundColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

}  // namespace magda::daw::ui
