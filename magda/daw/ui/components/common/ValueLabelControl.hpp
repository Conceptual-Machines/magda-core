#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace magda::daw::ui {

/**
 * Shared value-label surface used by drag/edit controls.
 *
 * This component owns only presentation and text-edit lifecycle. It deliberately
 * does not know about parameters, tracks, automation targets, or value mapping.
 */
class ValueLabelControl : public juce::Component {
  public:
    enum class FillMode { LeftToRight, PanCentre, BottomToTop };
    enum class TintState { None, Automated, Overridden };

    ValueLabelControl();
    ~ValueLabelControl() override = default;

    void setRange(double min, double max);
    void setValue(double value);
    void setDisplayText(juce::String text);
    void setTextOverride(juce::String text);
    void clearTextOverride();
    void setFillMode(FillMode mode);
    void setShowFillIndicator(bool show);
    void setDrawBackground(bool draw);
    void setDrawBorder(bool draw);
    void setFont(const juce::Font& font);
    void setFontSize(float size);
    void setTextColour(juce::Colour colour);
    void clearTextColour();
    void setFillColour(juce::Colour colour);
    void clearFillColour();
    void setBackgroundColour(juce::Colour colour);
    void clearBackgroundColour();
    void setJustification(juce::Justification justification);
    void setDragging(bool dragging);
    void setCoEditing(bool coEditing);
    void setTintState(TintState state);
    void setVertical(bool vertical);
    void setShowText(bool show);

    bool isEditing() const;
    void showEditor(const juce::String& initialText);
    void cancelEditing();

    std::function<void(const juce::MouseEvent&)> onMouseDown;
    std::function<void(const juce::MouseEvent&)> onMouseDrag;
    std::function<void(const juce::MouseEvent&)> onMouseUp;
    std::function<void(const juce::MouseEvent&)> onMouseDoubleClick;
    std::function<void(const juce::MouseEvent&, const juce::MouseWheelDetails&)> onMouseWheel;
    std::function<void(const juce::String&)> onEditCommit;
    std::function<void()> onEditCancel;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

  private:
    void finishEditing();

    double value_ = 0.0;
    double minValue_ = 0.0;
    double maxValue_ = 1.0;
    juce::String displayText_;
    juce::String textOverride_;
    FillMode fillMode_ = FillMode::LeftToRight;
    TintState tintState_ = TintState::None;
    bool showFillIndicator_ = true;
    bool drawBackground_ = true;
    bool drawBorder_ = true;
    bool dragging_ = false;
    bool coEditing_ = false;
    bool vertical_ = false;
    bool showText_ = true;
    juce::Font font_;
    juce::Justification justification_ = juce::Justification::centred;
    std::optional<juce::Colour> customTextColour_;
    std::optional<juce::Colour> customFillColour_;
    std::optional<juce::Colour> customBackgroundColour_;
    std::unique_ptr<juce::TextEditor> editor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ValueLabelControl)
};

}  // namespace magda::daw::ui
