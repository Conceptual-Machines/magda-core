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

    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.7f));
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
        if (plugin_) {
            plugin_->trigger = triggerCombo_.getSelectedId() - 1;
            updateLoopControls();
        }
    };

    setupLabel(orderLabel_, "ORDER");
    setupCombo(orderCombo_);
    orderCombo_.addItem("Up", 1);
    orderCombo_.addItem("Down", 2);
    orderCombo_.addItem("Up/Down", 3);
    orderCombo_.addItem("As Played", 4);
    orderCombo_.onChange = [this] {
        if (plugin_)
            plugin_->order = orderCombo_.getSelectedId() - 1;
    };

    setupLabel(shapeLabel_, "SHAPE");
    setupCombo(shapeCombo_);
    static const char* shapeNames[] = {"Linear", "Ease In", "Ease Out",  "Snap",
                                       "Spike",  "S-Curve", "Overshoot", "Bounce"};
    for (int i = 0; i < 8; ++i)
        shapeCombo_.addItem(shapeNames[i], i + 1);
    shapeCombo_.onChange = [this] {
        if (plugin_) {
            plugin_->shape = shapeCombo_.getSelectedId() - 1;
            refreshOnsets();
        }
    };

    setupLabel(cyclesLabel_, "CYCLES");
    setupSlider(cyclesSlider_, 1, 8, 1);
    cyclesSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    cyclesSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    cyclesSlider_.onValueChanged = [this](double value) {
        if (plugin_) {
            plugin_->cycles = juce::roundToInt(value) - 1;  // display 1..8 -> stored 0..7
            refreshOnsets();
        }
    };

    setupLabel(lengthLabel_, "LENGTH");
    setupSlider(lengthSlider_, 1, 400, 1);
    lengthSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)) + " ms"; });
    lengthSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("ms", "").trim().getDoubleValue(); });
    lengthSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->strumLength = static_cast<float>(value);
    };

    setupLabel(loopModeLabel_, "LOOP BY");
    setupCombo(loopModeCombo_);
    loopModeCombo_.addItem("Time", 1);
    loopModeCombo_.addItem("Beat", 2);
    loopModeCombo_.onChange = [this] {
        if (plugin_) {
            plugin_->loopSync = loopModeCombo_.getSelectedId() - 1;
            updateLoopControls();
        }
    };

    setupLabel(loopLabel_, "LOOP");
    setupSlider(syncSlider_, 60, 2000, 1);
    syncSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)) + " ms"; });
    syncSlider_.setValueParser(
        [](const juce::String& t) { return t.replace("ms", "").trim().getDoubleValue(); });
    syncSlider_.onValueChanged = [this](double value) {
        if (plugin_)
            plugin_->syncInterval = static_cast<float>(value);
    };

    setupCombo(loopRateCombo_);
    static const char* rateNames[] = {"1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T"};
    for (int i = 0; i < 8; ++i)
        loopRateCombo_.addItem(rateNames[i], i + 1);
    loopRateCombo_.onChange = [this] {
        if (plugin_)
            plugin_->loopRate = loopRateCombo_.getSelectedId() - 1;
    };

    setupLabel(vizLabel_, "ONSETS");
    onsetStrip_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(onsetStrip_);
}

StrumUI::~StrumUI() {
    if (watchedState_.isValid())
        watchedState_.removeListener(this);
    triggerCombo_.setLookAndFeel(nullptr);
    orderCombo_.setLookAndFeel(nullptr);
    shapeCombo_.setLookAndFeel(nullptr);
    loopModeCombo_.setLookAndFeel(nullptr);
    loopRateCombo_.setLookAndFeel(nullptr);
}

void StrumUI::setPlugin(daw::audio::MidiStrumPlugin* plugin) {
    if (watchedState_.isValid())
        watchedState_.removeListener(this);

    plugin_ = plugin;

    if (plugin_) {
        watchedState_ = plugin_->state;
        watchedState_.addListener(this);
        syncFromPlugin();
    }
}

void StrumUI::syncFromPlugin() {
    if (!plugin_)
        return;

    triggerCombo_.setSelectedId(plugin_->trigger.get() + 1, juce::dontSendNotification);
    orderCombo_.setSelectedId(plugin_->order.get() + 1, juce::dontSendNotification);
    shapeCombo_.setSelectedId(plugin_->shape.get() + 1, juce::dontSendNotification);
    cyclesSlider_.setValue(static_cast<double>(plugin_->cycles.get() + 1),
                           juce::dontSendNotification);
    lengthSlider_.setValue(static_cast<double>(plugin_->strumLength.get()),
                           juce::dontSendNotification);
    loopModeCombo_.setSelectedId(plugin_->loopSync.get() + 1, juce::dontSendNotification);
    loopRateCombo_.setSelectedId(plugin_->loopRate.get() + 1, juce::dontSendNotification);
    syncSlider_.setValue(static_cast<double>(plugin_->syncInterval.get()),
                         juce::dontSendNotification);
    updateLoopControls();
    refreshOnsets();
}

void StrumUI::refreshOnsets() {
    if (plugin_)
        onsetStrip_.setOnsets(plugin_->curveOnsetPreview(PREVIEW_TICKS));
}

void StrumUI::updateLoopControls() {
    if (!plugin_)
        return;
    const bool loop = static_cast<Strum::Trigger>(plugin_->trigger.get()) == Strum::Trigger::Loop;
    const bool beat =
        static_cast<Strum::LoopSync>(plugin_->loopSync.get()) == Strum::LoopSync::Beat;

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

void StrumUI::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) {
    juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer(this)] {
        if (safeThis)
            safeThis->syncFromPlugin();
    });
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

std::vector<LinkableTextSlider*> StrumUI::getLinkableSliders() {
    // Param registration order: 0=trigger, 1=order, 2=shape, 3=cycles,
    // 4=loopsync, 5=looprate, 6=strumlength, 7=syncinterval. Only the sliders are
    // macro-linkable; the combos (trigger/order/shape/loop-mode/loop-rate) are not.
    magda::ChainNodePath dummy;
    cyclesSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 3, dummy);
    lengthSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 6, dummy);
    syncSlider_.setLinkContext(magda::INVALID_DEVICE_ID, 7, dummy);
    return {&cyclesSlider_, &lengthSlider_, &syncSlider_};
}

}  // namespace magda::daw::ui
