#include "ValueLabelControl.hpp"

#include <cmath>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"

namespace magda::daw::ui {

ValueLabelControl::ValueLabelControl() : font_(FontManager::getInstance().getUIFont(10.0f)) {
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void ValueLabelControl::setShowText(bool show) {
    if (showText_ == show)
        return;
    showText_ = show;
    repaint();
}

void ValueLabelControl::setEditorBoundsProvider(std::function<juce::Rectangle<int>()> provider) {
    editorBoundsProvider_ = std::move(provider);
    if (editor_)
        editor_->setBounds(editorBounds());
}

void ValueLabelControl::setRange(double min, double max) {
    minValue_ = min;
    maxValue_ = max;
    repaint();
}

void ValueLabelControl::setValue(double value) {
    value_ = value;
    repaint();
}

void ValueLabelControl::setDisplayText(juce::String text) {
    displayText_ = std::move(text);
    repaint();
}

void ValueLabelControl::setTextOverride(juce::String text) {
    textOverride_ = std::move(text);
    repaint();
}

void ValueLabelControl::clearTextOverride() {
    textOverride_.clear();
    repaint();
}

void ValueLabelControl::setFillMode(FillMode mode) {
    fillMode_ = mode;
    repaint();
}

void ValueLabelControl::setShowFillIndicator(bool show) {
    showFillIndicator_ = show;
    repaint();
}

void ValueLabelControl::setDrawBackground(bool draw) {
    drawBackground_ = draw;
    repaint();
}

void ValueLabelControl::setDrawBorder(bool draw) {
    drawBorder_ = draw;
    repaint();
}

void ValueLabelControl::setFont(const juce::Font& font) {
    font_ = font;
    repaint();
}

void ValueLabelControl::setFontSize(float size) {
    font_ = FontManager::getInstance().getUIFont(size);
    repaint();
}

void ValueLabelControl::setTextColour(juce::Colour colour) {
    customTextColour_ = colour;
    repaint();
}

void ValueLabelControl::clearTextColour() {
    customTextColour_.reset();
    repaint();
}

void ValueLabelControl::setFillColour(juce::Colour colour) {
    customFillColour_ = colour;
    repaint();
}

void ValueLabelControl::clearFillColour() {
    customFillColour_.reset();
    repaint();
}

void ValueLabelControl::setBackgroundColour(juce::Colour colour) {
    customBackgroundColour_ = colour;
    repaint();
}

void ValueLabelControl::clearBackgroundColour() {
    customBackgroundColour_.reset();
    repaint();
}

void ValueLabelControl::setJustification(juce::Justification justification) {
    justification_ = justification;
    repaint();
}

void ValueLabelControl::setDragging(bool dragging) {
    dragging_ = dragging;
    repaint();
}

void ValueLabelControl::setCoEditing(bool coEditing) {
    coEditing_ = coEditing;
    repaint();
}

void ValueLabelControl::setTintState(TintState state) {
    tintState_ = state;
    repaint();
}

void ValueLabelControl::setVertical(bool vertical) {
    vertical_ = vertical;
    repaint();
}

void ValueLabelControl::setEditorBoundsOverride(std::optional<juce::Rectangle<int>> bounds) {
    editorBoundsOverride_ = bounds;
    if (editor_)
        editor_->setBounds(editorBounds());
}

bool ValueLabelControl::isEditing() const {
    return editor_ != nullptr;
}

void ValueLabelControl::showEditor(const juce::String& initialText) {
    if (editor_)
        return;

    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setBounds(editorBounds());
    editor_->setFont(font_);
    editor_->setText(initialText, false);
    editor_->selectAll();
    editor_->setJustification(justification_);
    editor_->setColour(juce::TextEditor::backgroundColourId,
                       DarkTheme::getColour(DarkTheme::SURFACE));
    editor_->setColour(juce::TextEditor::textColourId,
                       customTextColour_.value_or(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY)));
    editor_->setColour(juce::TextEditor::highlightColourId,
                       DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    editor_->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor_->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);

    editor_->onReturnKey = [this]() { finishEditing(); };
    editor_->onEscapeKey = [this]() { cancelEditing(); };
    editor_->onFocusLost = [this]() { finishEditing(); };

    addAndMakeVisible(*editor_);
    editor_->grabKeyboardFocus();
    repaint();
}

void ValueLabelControl::cancelEditing() {
    if (!editor_)
        return;

    editor_ = nullptr;
    if (onEditCancel)
        onEditCancel();
    repaint();
}

void ValueLabelControl::finishEditing() {
    if (!editor_)
        return;

    auto text = editor_->getText();
    editor_ = nullptr;
    if (onEditCommit)
        onEditCommit(text);
    repaint();
}

juce::Rectangle<int> ValueLabelControl::editorBounds() const {
    if (editorBoundsProvider_) {
        auto bounds = editorBoundsProvider_();
        if (!bounds.isEmpty())
            return bounds.getIntersection(getLocalBounds());
    }
    if (editorBoundsOverride_)
        return *editorBoundsOverride_;
    return getLocalBounds().reduced(1);
}

void ValueLabelControl::paint(juce::Graphics& g) {
    if (getWidth() < 1 || getHeight() < 1)
        return;

    auto bounds = getLocalBounds().toFloat();
    const float alpha = isEnabled() ? 1.0f : 0.4f;

    if (drawBackground_) {
        g.setColour(customBackgroundColour_.value_or(DarkTheme::getColour(DarkTheme::SURFACE))
                        .withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(bounds, 2.0f);
    }

    if (showFillIndicator_) {
        auto fillBase = customFillColour_.value_or(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        const float fillAlpha = customFillColour_ ? fillBase.getFloatAlpha() : 0.3f;
        g.setColour(fillBase.withAlpha(fillAlpha * alpha));

        if (fillMode_ == FillMode::PanCentre) {
            const float centreX = bounds.getCentreX();
            const float normalizedPan = static_cast<float>(juce::jlimit(-1.0, 1.0, value_));

            if (std::abs(normalizedPan) < 0.01f) {
                g.fillRect(centreX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight());
            } else if (normalizedPan < 0.0f) {
                const float fillWidth = (centreX - bounds.getX()) * (-normalizedPan);
                g.fillRect(centreX - fillWidth, bounds.getY(), fillWidth, bounds.getHeight());
            } else {
                const float fillWidth = (bounds.getRight() - centreX) * normalizedPan;
                g.fillRect(centreX, bounds.getY(), fillWidth, bounds.getHeight());
            }
        } else if (fillMode_ == FillMode::BottomToTop && maxValue_ > minValue_) {
            const double norm =
                juce::jlimit(0.0, 1.0, (value_ - minValue_) / (maxValue_ - minValue_));
            if (norm > 0.0) {
                float fillH = static_cast<float>(bounds.getHeight() * norm);
                g.fillRoundedRectangle(bounds.getX(), bounds.getBottom() - fillH, bounds.getWidth(),
                                       fillH, 2.0f);
            }
        } else if (maxValue_ > minValue_) {
            const double normalizedValue =
                juce::jlimit(0.0, 1.0, (value_ - minValue_) / (maxValue_ - minValue_));

            if (normalizedValue > 0.0) {
                auto fillBounds =
                    bounds.withWidth(static_cast<float>(bounds.getWidth() * normalizedValue));
                g.fillRoundedRectangle(fillBounds, 2.0f);
            }
        }
    }

    const bool hasTint = tintState_ != TintState::None;
    const juce::Colour tintColour = tintState_ == TintState::Overridden
                                        ? juce::Colour(DarkTheme::TEXT_DISABLED)
                                        : juce::Colour(DarkTheme::ACCENT_PURPLE);

    if (hasTint && drawBackground_) {
        g.setColour(tintColour.withAlpha(0.18f * alpha));
        g.fillRoundedRectangle(bounds, 2.0f);
    }

    if (drawBorder_) {
        juce::Colour borderColour;
        if (hasTint)
            borderColour = tintColour;
        else if (dragging_ || coEditing_)
            borderColour = DarkTheme::getColour(DarkTheme::ACCENT_BLUE);
        else
            borderColour = DarkTheme::getColour(DarkTheme::BORDER);

        g.setColour(borderColour.withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 2.0f, hasTint ? 1.5f : 1.0f);
    }

    if (!editor_ && showText_) {
        g.setColour(customTextColour_.value_or(DarkTheme::getColour(DarkTheme::TEXT_PRIMARY))
                        .withMultipliedAlpha(alpha));
        g.setFont(font_);
        const auto text = textOverride_.isNotEmpty() ? textOverride_ : displayText_;
        if (vertical_) {
            g.saveState();
            g.addTransform(juce::AffineTransform::rotation(
                -juce::MathConstants<float>::halfPi, bounds.getCentreX(), bounds.getCentreY()));
            auto rotBounds = juce::Rectangle<float>(bounds.getCentreX() - bounds.getHeight() * 0.5f,
                                                    bounds.getCentreY() - bounds.getWidth() * 0.5f,
                                                    bounds.getHeight(), bounds.getWidth());
            g.drawText(text, rotBounds.reduced(2.0f, 1.0f), justification_, false);
            g.restoreState();
        } else {
            g.drawText(text, bounds.reduced(2.0f, 0.0f), justification_, false);
        }
    }
}

void ValueLabelControl::resized() {
    if (editor_)
        editor_->setBounds(editorBounds());
}

void ValueLabelControl::mouseDown(const juce::MouseEvent& e) {
    if (onMouseDown)
        onMouseDown(e);
}

void ValueLabelControl::mouseDrag(const juce::MouseEvent& e) {
    if (onMouseDrag)
        onMouseDrag(e);
}

void ValueLabelControl::mouseUp(const juce::MouseEvent& e) {
    if (onMouseUp)
        onMouseUp(e);
}

void ValueLabelControl::mouseDoubleClick(const juce::MouseEvent& e) {
    if (onMouseDoubleClick)
        onMouseDoubleClick(e);
}

void ValueLabelControl::mouseWheelMove(const juce::MouseEvent& e,
                                       const juce::MouseWheelDetails& wheel) {
    if (onMouseWheel)
        onMouseWheel(e, wheel);
    else
        juce::Component::mouseWheelMove(e, wheel);
}

}  // namespace magda::daw::ui
