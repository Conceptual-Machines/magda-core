// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

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
class ConsoleTranscript : public juce::Component, private juce::Timer {
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

    /// A spinning wheel and @p activity under the last block, such as "Running clips.get";
    /// empty hides it. The seconds shown beside it restart when the part before
    /// activityDetailSeparator() changes.
    void setActivity(const juce::String& activity);

    /// Opens a line that draws dimmed, for text streamed in rather than appended with appendDim.
    static juce::String dimMark() {
        return juce::String::charToString(0x200B);
    }

    /// Joins an activity to a detail that changes under it, such as a percentage.
    static juce::String activityDetailSeparator() {
        return juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "));
    }

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
    void timerCallback() override;

    juce::String text_;
    juce::StringArray userTargets_;
    juce::String emptyHint_;
    std::vector<Block> blocks_;
    juce::String activity_;
    juce::uint32 activityStartMs_ = 0;
    juce::Rectangle<int> activityBounds_;
    float spinnerTurn_ = 0.0f;
    std::unique_ptr<Content> content_;
    juce::Viewport viewport_;
};

}  // namespace magda::daw::ui
