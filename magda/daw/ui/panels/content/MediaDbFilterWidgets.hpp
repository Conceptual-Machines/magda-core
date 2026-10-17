#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace magda::daw::ui {

/** @brief A "label value ▾" pill that picks one of a closed set from a menu; tinted when set. */
class FilterPill : public juce::Component, public juce::SettableTooltipClient {
  public:
    /** @param options Index 0 is the no-filter choice ("all", "any"). */
    FilterPill(juce::String label, juce::StringArray options);

    int getSelectedIndex() const {
        return selected_;
    }
    void setSelectedIndex(int index, juce::NotificationType notification);

    /// Width that fits the label and the longest option.
    int getIdealWidth() const;

    std::function<void()> onChange;

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    juce::String label_;
    juce::StringArray options_;
    int selected_ = 0;
};

/** @brief "bpm min – max" in one bordered field; tinted while either end is set. */
class BpmRangeField : public juce::Component {
  public:
    BpmRangeField();

    std::optional<double> getMin() const;
    std::optional<double> getMax() const;

    /// Fires on Return or focus loss.
    std::function<void()> onChange;

    void applyThemeColours();
    void paint(juce::Graphics& g) override;
    void resized() override;

  private:
    juce::TextEditor min_;
    juce::TextEditor max_;
};

/**
 * @brief Tag filter as removable chips plus a text field that suggests library tags.
 *
 * Return, comma or space commits the typed tag; Backspace in an empty field removes the
 * last chip. Up/Down/Tab pick from the suggestion list.
 */
class TagChipInput : public juce::Component, private juce::TextEditor::Listener {
  public:
    TagChipInput();
    ~TagChipInput() override;

    const juce::StringArray& getTags() const {
        return tags_;
    }
    void setTags(const juce::StringArray& tags, juce::NotificationType notification);

    /// Library tags starting with the typed prefix.
    std::function<juce::StringArray(const juce::String& prefix)> suggest;
    std::function<void()> onChange;

    void applyThemeColours();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;

  private:
    class Suggestions;
    class Input;

    void commitText();
    void addTag(const juce::String& tag);
    void removeTag(int index);
    void refreshSuggestions();
    void hideSuggestions();
    bool handleKey(const juce::KeyPress& key);
    void textEditorTextChanged(juce::TextEditor&) override;
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorFocusLost(juce::TextEditor&) override;

    juce::StringArray tags_;
    std::unique_ptr<Input> input_;
    std::unique_ptr<Suggestions> suggestions_;
    juce::Array<juce::Rectangle<int>> chipBounds_;
    juce::Array<juce::Rectangle<int>> closeBounds_;
};

}  // namespace magda::daw::ui
