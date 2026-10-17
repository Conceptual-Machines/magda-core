#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace magda::daw::ui {

/**
 * @brief The AI console's log: your messages as bubbles, MAGDA's replies as text under a label.
 *
 * The log stays one plain string so the console can stream into it and rewrite its tail;
 * blocks are split from it on change. A line starting "● " opens one of your messages,
 * "◆ " one of MAGDA's, and every other line continues the block above it.
 */
class ConsoleTranscript : public juce::Component {
  public:
    ConsoleTranscript();
    ~ConsoleTranscript() override;

    juce::String getText() const {
        return text_;
    }
    void setText(const juce::String& text);
    void append(const juce::String& text);
    /// Appends lines drawn dimmed: display-only notes such as the mix caveat.
    void appendDim(const juce::String& text);

    /// The log without display markers, for the clipboard.
    juce::String getPlainText() const;

    /// Where your next message goes ("Bass"), shown as "YOU → Bass" above its bubble.
    void noteNextUserTarget(const juce::String& target);

    /// Shown centred while the log holds nothing but the greeting.
    void setEmptyHint(const juce::String& hint);

    void resized() override;
    void lookAndFeelChanged() override;

  private:
    enum class Role { User, Magda };
    struct Block {
        Role role = Role::Magda;
        juce::String text;
        bool dim = false;
        bool error = false;
        juce::String target;
        juce::TextLayout layout;
        juce::Rectangle<int> bounds;  // bubble or text, in content coordinates
        bool showLabel = false;
    };

    class Content;

    void rebuild();
    void layoutBlocks();

    juce::String text_;
    juce::StringArray userTargets_;
    juce::String emptyHint_;
    std::vector<Block> blocks_;
    std::unique_ptr<Content> content_;
    juce::Viewport viewport_;
};

}  // namespace magda::daw::ui
