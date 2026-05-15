#include "NoteSlicePopup.hpp"

namespace magda::daw::ui {

static constexpr int ROW_HEIGHT = 24;
static constexpr int LABEL_WIDTH = 76;
static constexpr int PADDING = 8;
static constexpr int GAP = 6;
static constexpr int BUTTON_HEIGHT = 26;
static constexpr int PREVIEW_HEIGHT = 36;

juce::Component::SafePointer<NoteSlicePopup> NoteSlicePopup::currentPopup_;

void NoteSlicePopup::dismissCurrent() {
    if (auto* p = currentPopup_.getComponent())
        delete p;
    currentPopup_ = nullptr;
}

NoteSlicePopup::NoteSlicePopup(magda::ClipId clipId, size_t noteCount)
    : clipId_(clipId), noteCount_(noteCount) {
    setWantsKeyboardFocus(true);

    countLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    countLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    countLabel_.setJustificationType(juce::Justification::centredLeft);
    countLabel_.setText(juce::String(noteCount_) + (noteCount_ == 1 ? " note" : " notes"),
                        juce::dontSendNotification);
    addAndMakeVisible(countLabel_);

    subdivisionsLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    subdivisionsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    subdivisionsLabel_.setJustificationType(juce::Justification::centredLeft);
    subdivisionsLabel_.setText("SLICES", juce::dontSendNotification);
    addAndMakeVisible(subdivisionsLabel_);

    subdivisionsSlider_.setRange(2.0, 32.0, 1.0);
    subdivisionsSlider_.setValue(4.0, juce::dontSendNotification);
    subdivisionsSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)); });
    subdivisionsSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    subdivisionsSlider_.onValueChanged = [this](double) { repaint(); };
    addAndMakeVisible(subdivisionsSlider_);

    applyButton_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.6f));
    applyButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    applyButton_.onClick = [this] { apply(); };
    addAndMakeVisible(applyButton_);

    cancelButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.15f));
    cancelButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    cancelButton_.onClick = [this] { cancel(); };
    addAndMakeVisible(cancelButton_);

    setSize(240, TITLE_BAR_HEIGHT + PADDING * 2 + ROW_HEIGHT * 2 + PREVIEW_HEIGHT + GAP * 4 +
                     BUTTON_HEIGHT);
}

void NoteSlicePopup::apply() {
    if (onApply)
        onApply(juce::roundToInt(subdivisionsSlider_.getValue()));

    if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
        callout->dismiss();
    else
        delete this;
}

void NoteSlicePopup::cancel() {
    if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
        callout->dismiss();
    else
        delete this;
}

bool NoteSlicePopup::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::returnKey) {
        apply();
        return true;
    }

    if (key == juce::KeyPress::escapeKey) {
        cancel();
        return true;
    }

    return false;
}

void NoteSlicePopup::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    auto titleArea = getLocalBounds().removeFromTop(TITLE_BAR_HEIGHT);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.08f));
    g.fillRect(titleArea);
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.drawText("NOTE SLICE", titleArea.reduced(6, 0), juce::Justification::centredLeft);

    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
    g.drawHorizontalLine(TITLE_BAR_HEIGHT, 0.0f, static_cast<float>(getWidth()));

    auto preview = getLocalBounds();
    preview.removeFromTop(TITLE_BAR_HEIGHT + PADDING + ROW_HEIGHT + GAP + ROW_HEIGHT + GAP);
    preview = preview.removeFromTop(PREVIEW_HEIGHT).reduced(PADDING, 4);

    auto note = preview.reduced(4, 8).toFloat();
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.16f));
    g.fillRoundedRectangle(note, 3.0f);
    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.85f));
    g.drawRoundedRectangle(note, 3.0f, 1.0f);

    const int subdivisions = juce::roundToInt(subdivisionsSlider_.getValue());
    for (int i = 1; i < subdivisions; ++i) {
        const float x = note.getX() +
                        note.getWidth() * static_cast<float>(i) / static_cast<float>(subdivisions);
        g.drawLine(x, note.getY() - 4.0f, x, note.getBottom() + 4.0f, 1.0f);
    }
}

void NoteSlicePopup::mouseDown(const juce::MouseEvent& e) {
    if (e.y < TITLE_BAR_HEIGHT)
        dragger_.startDraggingComponent(this, e);
}

void NoteSlicePopup::mouseDrag(const juce::MouseEvent& e) {
    if (e.mouseWasClicked())
        return;
    dragger_.dragComponent(this, e, nullptr);
}

void NoteSlicePopup::showAbove(std::unique_ptr<NoteSlicePopup> popup, juce::Component* anchor) {
    dismissCurrent();

    auto* raw = popup.release();
    auto screenBounds = anchor->getScreenBounds();
    int x = screenBounds.getCentreX() - raw->getWidth() / 2;
    int y = screenBounds.getY() - raw->getHeight() - 4;
    raw->setTopLeftPosition(x, y);
    raw->setAlwaysOnTop(true);
    raw->addToDesktop(juce::ComponentPeer::windowHasDropShadow);
    raw->setVisible(true);
    raw->toFront(true);
    raw->grabKeyboardFocus();
    currentPopup_ = raw;
}

void NoteSlicePopup::resized() {
    auto bounds = getLocalBounds();
    bounds.removeFromTop(TITLE_BAR_HEIGHT);
    bounds.reduce(PADDING, PADDING);

    countLabel_.setBounds(bounds.removeFromTop(ROW_HEIGHT));
    bounds.removeFromTop(GAP);

    auto row = bounds.removeFromTop(ROW_HEIGHT);
    subdivisionsLabel_.setBounds(row.removeFromLeft(LABEL_WIDTH));
    subdivisionsSlider_.setBounds(row);
    bounds.removeFromTop(GAP + PREVIEW_HEIGHT + GAP);

    auto buttonRow = bounds.removeFromBottom(BUTTON_HEIGHT);
    const int buttonWidth = (buttonRow.getWidth() - GAP) / 2;
    cancelButton_.setBounds(buttonRow.removeFromLeft(buttonWidth));
    buttonRow.removeFromLeft(GAP);
    applyButton_.setBounds(buttonRow);
}

}  // namespace magda::daw::ui
