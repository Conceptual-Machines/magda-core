#include "custom_ui/StrumUI.hpp"

#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

using Strum = daw::audio::MidiStrumPlugin;

// Layout constants (match ArpeggiatorUI).
static constexpr int ROW_HEIGHT = 22;
static constexpr int ROW_GAP = 4;
static constexpr int LABEL_WIDTH = 52;
static constexpr int PADDING = 6;
static constexpr int COLUMN_GAP = 10;

// Onset-distribution preview: enough ticks to read the timing shape without
// pretending to be an exact chord size.
static constexpr int PREVIEW_TICKS = 12;

// ---------------------------------------------------------------------------
// OnsetStrip
// ---------------------------------------------------------------------------
void StrumUI::OnsetStrip::setOnsets(std::vector<float> onsets) {
    onsets_ = std::move(onsets);
    repaint();
}

void StrumUI::OnsetStrip::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    if (b.getWidth() < 4.0f || b.getHeight() < 4.0f)
        return;

    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.08f));
    g.fillRoundedRectangle(b, 2.0f);
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.3f));
    g.drawRoundedRectangle(b.reduced(0.5f), 2.0f, 0.5f);

    auto inner = b.reduced(8.0f, 6.0f);
    // Baseline.
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.4f));
    g.drawLine(inner.getX(), inner.getBottom(), inner.getRight(), inner.getBottom(), 1.0f);

    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_POSITIVE).withAlpha(0.7f));
    for (float u : onsets_) {
        float tx = inner.getX() + juce::jlimit(0.0f, 1.0f, u) * inner.getWidth();
        g.drawLine(tx, inner.getY(), tx, inner.getBottom(), 1.5f);
    }
}

// ---------------------------------------------------------------------------
// StrumUI
// ---------------------------------------------------------------------------
StrumUI::StrumUI() {
    setupLabel(triggerLabel_, "TRIGGER");
    setupCombo(triggerCombo_);
    triggerCombo_.addItem("Chord", 1);
    triggerCombo_.addItem("Loop", 2);
    triggerCombo_.onChange = [this] {
        trigger_ = triggerCombo_.getSelectedId() - 1;
        sendChange(Strum::kTrigger, static_cast<float>(trigger_));
        updateLoopControls();
    };

    setupLabel(orderLabel_, "ORDER");
    setupCombo(orderCombo_);
    orderCombo_.addItem("Up", 1);
    orderCombo_.addItem("Down", 2);
    orderCombo_.addItem("Up/Down", 3);
    orderCombo_.addItem("As Played", 4);
    orderCombo_.onChange = [this] {
        sendChange(Strum::kOrder, static_cast<float>(orderCombo_.getSelectedId() - 1));
    };

    setupLabel(shapeLabel_, "SHAPE");
    setupCombo(shapeCombo_);
    static const char* shapeNames[] = {"Linear", "Ease In", "Ease Out",  "Snap",
                                       "Spike",  "S-Curve", "Overshoot", "Bounce"};
    for (int i = 0; i < 8; ++i)
        shapeCombo_.addItem(shapeNames[i], i + 1);
    shapeCombo_.onChange = [this] {
        shape_ = shapeCombo_.getSelectedId() - 1;
        sendChange(Strum::kShape, static_cast<float>(shape_));
        refreshOnsets();
    };

    setupLabel(cyclesLabel_, "CYCLES");
    setupSlider(cyclesSlider_, 1, 8, 1);
    cyclesSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    cyclesSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    cyclesSlider_.onValueChanged = [this](double value) {
        cycles_ = juce::roundToInt(value) - 1;  // display 1..8 -> stored 0..7
        sendChange(Strum::kCycles, static_cast<float>(cycles_));
        refreshOnsets();
    };

    setupLabel(lengthLabel_, "LENGTH");
    setupSlider(lengthSlider_, 1, 400, 1);
    lengthSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)) + " ms"; });
    lengthSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("ms", "").trim().getDoubleValue(); });
    lengthSlider_.onValueChanged = [this](double value) {
        sendChange(Strum::kStrumLength, static_cast<float>(value));
    };

    setupLabel(loopModeLabel_, "LOOP BY");
    setupCombo(loopModeCombo_);
    loopModeCombo_.addItem("Time", 1);
    loopModeCombo_.addItem("Beat", 2);
    loopModeCombo_.onChange = [this] {
        loopSync_ = loopModeCombo_.getSelectedId() - 1;
        sendChange(Strum::kLoopSync, static_cast<float>(loopSync_));
        updateLoopControls();
    };

    setupLabel(loopLabel_, "LOOP");
    setupSlider(syncSlider_, 60, 2000, 1);
    syncSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)) + " ms"; });
    syncSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("ms", "").trim().getDoubleValue(); });
    syncSlider_.onValueChanged = [this](double value) {
        sendChange(Strum::kSyncInterval, static_cast<float>(value));
    };

    setupCombo(loopRateCombo_);
    static const char* rateNames[] = {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T"};
    for (int i = 0; i < 8; ++i)
        loopRateCombo_.addItem(rateNames[i], i + 1);
    loopRateCombo_.onChange = [this] {
        sendChange(Strum::kLoopRate, static_cast<float>(loopRateCombo_.getSelectedId() - 1));
    };

    setupLabel(vizLabel_, "ONSETS");
    onsetStrip_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(onsetStrip_);

    updateLoopControls();
    refreshOnsets();
}

StrumUI::~StrumUI() {
    triggerCombo_.setLookAndFeel(nullptr);
    orderCombo_.setLookAndFeel(nullptr);
    shapeCombo_.setLookAndFeel(nullptr);
    loopModeCombo_.setLookAndFeel(nullptr);
    loopRateCombo_.setLookAndFeel(nullptr);
}

void StrumUI::sendChange(int paramIndex, float value) {
    if (onParameterChanged)
        onParameterChanged(paramIndex, value);
}

void StrumUI::updateFromParameters(const std::vector<magda::ParameterInfo>& params) {
    const auto display = [&params](int index, float fallback) {
        return index < static_cast<int>(params.size())
                   ? params[static_cast<size_t>(index)].currentValue
                   : fallback;
    };

    trigger_ = juce::roundToInt(display(Strum::kTrigger, 0.0f));
    shape_ = juce::roundToInt(display(Strum::kShape, 1.0f));
    cycles_ = juce::roundToInt(display(Strum::kCycles, 0.0f));
    loopSync_ = juce::roundToInt(display(Strum::kLoopSync, 0.0f));

    triggerCombo_.setSelectedId(trigger_ + 1, juce::dontSendNotification);
    orderCombo_.setSelectedId(juce::roundToInt(display(Strum::kOrder, 0.0f)) + 1,
                              juce::dontSendNotification);
    shapeCombo_.setSelectedId(shape_ + 1, juce::dontSendNotification);
    cyclesSlider_.setValue(static_cast<double>(cycles_ + 1), juce::dontSendNotification);
    lengthSlider_.setValue(static_cast<double>(display(Strum::kStrumLength, 90.0f)),
                           juce::dontSendNotification);
    loopModeCombo_.setSelectedId(loopSync_ + 1, juce::dontSendNotification);
    loopRateCombo_.setSelectedId(juce::roundToInt(display(Strum::kLoopRate, 2.0f)) + 1,
                                 juce::dontSendNotification);
    syncSlider_.setValue(static_cast<double>(display(Strum::kSyncInterval, 500.0f)),
                         juce::dontSendNotification);
    updateLoopControls();
    refreshOnsets();
}

void StrumUI::refreshOnsets() {
    onsetStrip_.setOnsets(Strum::curveOnsetPreview(shape_, cycles_, PREVIEW_TICKS));
}

void StrumUI::updateLoopControls() {
    const bool loop = static_cast<Strum::Trigger>(trigger_) == Strum::Trigger::Loop;
    const bool beat = static_cast<Strum::LoopSync>(loopSync_) == Strum::LoopSync::Beat;

    // The loop controls only matter in Loop mode; grey them out otherwise.
    loopModeCombo_.setEnabled(loop);
    loopModeCombo_.setAlpha(loop ? 1.0f : 0.3f);
    loopModeLabel_.setAlpha(loop ? 1.0f : 0.3f);
    loopLabel_.setAlpha(loop ? 1.0f : 0.3f);

    // Time mode shows the ms slider; Beat mode shows the division combo. They
    // share the same row slot, so only one is visible at a time.
    syncSlider_.setVisible(!beat);
    syncSlider_.setEnabled(loop);
    syncSlider_.setAlpha(loop ? 1.0f : 0.3f);
    loopRateCombo_.setVisible(beat);
    loopRateCombo_.setEnabled(loop);
    loopRateCombo_.setAlpha(loop ? 1.0f : 0.3f);
}

void StrumUI::paint(juce::Graphics&) {
    // No chrome — content laid out directly.
}

void StrumUI::resized() {
    auto bounds = getLocalBounds().reduced(PADDING);
    int colWidth = (bounds.getWidth() - COLUMN_GAP) / 2;

    auto layoutRow = [](juce::Rectangle<int>& col, juce::Label& label, juce::Component& control) {
        auto row = col.removeFromTop(ROW_HEIGHT);
        label.setBounds(row.removeFromLeft(LABEL_WIDTH));
        control.setBounds(row);
        col.removeFromTop(ROW_GAP);
    };

    int topRowsHeight = 3 * (ROW_HEIGHT + ROW_GAP);
    auto topSection = bounds.removeFromTop(topRowsHeight);

    auto leftCol = topSection.removeFromLeft(colWidth);
    topSection.removeFromLeft(COLUMN_GAP);
    auto rightCol = topSection;

    // Left column
    layoutRow(leftCol, triggerLabel_, triggerCombo_);
    layoutRow(leftCol, orderLabel_, orderCombo_);
    layoutRow(leftCol, shapeLabel_, shapeCombo_);

    // Right column
    layoutRow(rightCol, cyclesLabel_, cyclesSlider_);
    layoutRow(rightCol, lengthLabel_, lengthSlider_);
    layoutRow(rightCol, loopModeLabel_, loopModeCombo_);

    // Full-width LOOP interval row: the ms slider and the division combo share
    // the same slot - updateLoopControls() shows whichever matches LOOP BY.
    bounds.removeFromTop(ROW_GAP);
    auto loopRow = bounds.removeFromTop(ROW_HEIGHT);
    loopLabel_.setBounds(loopRow.removeFromLeft(LABEL_WIDTH));
    syncSlider_.setBounds(loopRow);
    loopRateCombo_.setBounds(loopRow);

    bounds.removeFromTop(ROW_GAP);
    auto vizLabelRow = bounds.removeFromTop(ROW_HEIGHT);
    vizLabel_.setBounds(vizLabelRow.removeFromLeft(LABEL_WIDTH));
    bounds.removeFromTop(ROW_GAP);
    if (bounds.getHeight() > 12)
        onsetStrip_.setBounds(bounds);
}

void StrumUI::setupLabel(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(9.0f));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);
}

void StrumUI::setupCombo(juce::ComboBox& combo) {
    combo.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    combo.setColour(juce::ComboBox::backgroundColourId,
                    DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
    combo.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    combo.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    addAndMakeVisible(combo);
}

void StrumUI::setupSlider(LinkableTextSlider& slider, double min, double max, double step) {
    slider.setRange(min, max, step);
    addAndMakeVisible(slider);
}

void StrumUI::lookAndFeelChanged() {
    // Re-apply cached theme colours after a live theme switch.
    for (auto* label : {&triggerLabel_, &orderLabel_, &shapeLabel_, &cyclesLabel_, &lengthLabel_,
                        &loopModeLabel_, &loopLabel_, &vizLabel_})
        label->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());

    for (auto* combo :
         {&triggerCombo_, &orderCombo_, &shapeCombo_, &loopModeCombo_, &loopRateCombo_}) {
        combo->setColour(juce::ComboBox::backgroundColourId,
                         DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.1f));
        combo->setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
        combo->setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    }

    repaint();
}

std::vector<LinkableTextSlider*> StrumUI::getLinkableSliders() {
    // Param registration order: 0=trigger, 1=order, 2=shape, 3=cycles,
    // 4=loopsync, 5=looprate, 6=strumlength, 7=syncinterval. Only the sliders are
    // macro-linkable; the combos (trigger/order/shape/loop-mode/loop-rate) are not.
    magda::ChainNodePath dummy;
    cyclesSlider_.setLinkContext(magda::INVALID_DEVICE_ID, Strum::kCycles, dummy);
    lengthSlider_.setLinkContext(magda::INVALID_DEVICE_ID, Strum::kStrumLength, dummy);
    syncSlider_.setLinkContext(magda::INVALID_DEVICE_ID, Strum::kSyncInterval, dummy);
    return {&cyclesSlider_, &lengthSlider_, &syncSlider_};
}

}  // namespace magda::daw::ui
