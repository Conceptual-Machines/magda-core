#include "TimeBendPopup.hpp"

#include "audio/StepClock.hpp"
#include "core/ClipManager.hpp"

namespace magda::daw::ui {

static constexpr int ROW_HEIGHT = 22;
static constexpr int LABEL_WIDTH = 44;
static constexpr int PADDING = 8;
static constexpr int GAP = 4;
static constexpr int BUTTON_HEIGHT = 26;

juce::Component::SafePointer<TimeBendPopup> TimeBendPopup::currentPopup_;

void TimeBendPopup::dismissCurrent() {
    if (auto* p = currentPopup_.getComponent()) {
        delete p;  // destructor handles applied_ / restoreOriginals
    }
    currentPopup_ = nullptr;
}

TimeBendPopup::TimeBendPopup(magda::ClipId clipId, std::vector<size_t> noteIndices)
    : clipId_(clipId), noteIndices_(std::move(noteIndices)) {
    // Capture original positions for preview/restore
    auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    if (clip && clip->type == magda::ClipType::MIDI) {
        originalStartBeats_.reserve(noteIndices_.size());
        for (size_t index : noteIndices_) {
            if (index < clip->midiNotes.size())
                originalStartBeats_.push_back(clip->midiNotes[index].startBeat);
        }
    }

    // Curve display
    curveDisplay_.setMouseCursor(juce::MouseCursor::CrosshairCursor);
    curveDisplay_.setTooltip("Drag the handle to shape note timing. Double-click to reset.");
    addAndMakeVisible(curveDisplay_);

    // Depth slider
    depthLabel_.setText("DEPTH", juce::dontSendNotification);
    depthLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    depthLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    depthLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(depthLabel_);

    depthSlider_.setRange(-1.0, 1.0, 0.01);
    depthSlider_.setValueFormatter([](double v) { return juce::String(v, 2); });
    depthSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    depthSlider_.onValueChanged = [this](double value) {
        curveDisplay_.setValues(static_cast<float>(value), curveDisplay_.getSkew());
        applyPreview(curveDisplay_.getDepth(), curveDisplay_.getSkew(),
                     juce::roundToInt(cyclesSlider_.getValue()),
                     static_cast<float>(quantizeSlider_.getValue()),
                     juce::roundToInt(quantizeSubSlider_.getValue()), curveDisplay_.getHardAngle());
    };
    addAndMakeVisible(depthSlider_);

    // Skew slider
    skewLabel_.setText("SKEW", juce::dontSendNotification);
    skewLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    skewLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    skewLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(skewLabel_);

    skewSlider_.setRange(-1.0, 1.0, 0.01);
    skewSlider_.setValueFormatter([](double v) { return juce::String(v, 2); });
    skewSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    skewSlider_.onValueChanged = [this](double value) {
        curveDisplay_.setValues(curveDisplay_.getDepth(), static_cast<float>(value));
        applyPreview(curveDisplay_.getDepth(), curveDisplay_.getSkew(),
                     juce::roundToInt(cyclesSlider_.getValue()),
                     static_cast<float>(quantizeSlider_.getValue()),
                     juce::roundToInt(quantizeSubSlider_.getValue()), curveDisplay_.getHardAngle());
    };
    addAndMakeVisible(skewSlider_);

    // Cycles slider
    cyclesLabel_.setText("CYCLES", juce::dontSendNotification);
    cyclesLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    cyclesLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    cyclesLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(cyclesLabel_);

    cyclesSlider_.setRange(1.0, 8.0, 1.0);
    cyclesSlider_.setValue(1.0, juce::dontSendNotification);
    cyclesSlider_.setValueFormatter([](double v) { return juce::String(juce::roundToInt(v)); });
    cyclesSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    cyclesSlider_.onValueChanged = [this](double) {
        applyPreview(curveDisplay_.getDepth(), curveDisplay_.getSkew(),
                     juce::roundToInt(cyclesSlider_.getValue()),
                     static_cast<float>(quantizeSlider_.getValue()),
                     juce::roundToInt(quantizeSubSlider_.getValue()), curveDisplay_.getHardAngle());
    };
    addAndMakeVisible(cyclesSlider_);

    // Quantize slider
    quantizeLabel_.setText("QUANT", juce::dontSendNotification);
    quantizeLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    quantizeLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    quantizeLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(quantizeLabel_);

    quantizeSlider_.setRange(0.0, 1.0, 0.01);
    quantizeSlider_.setValue(0.0, juce::dontSendNotification);
    quantizeSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; });
    quantizeSlider_.setValueParser(
        [](const juce::String& t) { return t.trimCharactersAtEnd("%").getDoubleValue() / 100.0; });
    quantizeSlider_.onValueChanged = [this](double) {
        applyPreview(curveDisplay_.getDepth(), curveDisplay_.getSkew(),
                     juce::roundToInt(cyclesSlider_.getValue()),
                     static_cast<float>(quantizeSlider_.getValue()),
                     juce::roundToInt(quantizeSubSlider_.getValue()), curveDisplay_.getHardAngle());
    };
    addAndMakeVisible(quantizeSlider_);

    // Quantize subdivisions slider
    quantizeSubLabel_.setText("SUB", juce::dontSendNotification);
    quantizeSubLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    quantizeSubLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    quantizeSubLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(quantizeSubLabel_);

    quantizeSubSlider_.setRange(16.0, 512.0, 16.0);
    quantizeSubSlider_.setValue(64.0, juce::dontSendNotification);
    quantizeSubSlider_.setValueFormatter(
        [](double v) { return juce::String(juce::roundToInt(v)); });
    quantizeSubSlider_.setValueParser([](const juce::String& t) { return t.getDoubleValue(); });
    quantizeSubSlider_.onValueChanged = [this](double) {
        applyPreview(curveDisplay_.getDepth(), curveDisplay_.getSkew(),
                     juce::roundToInt(cyclesSlider_.getValue()),
                     static_cast<float>(quantizeSlider_.getValue()),
                     juce::roundToInt(quantizeSubSlider_.getValue()), curveDisplay_.getHardAngle());
    };
    addAndMakeVisible(quantizeSubSlider_);

    // Sync curve → sliders + live preview
    curveDisplay_.onCurveChanged = [this](float depth, float skew) {
        depthSlider_.setValue(static_cast<double>(depth), juce::dontSendNotification);
        skewSlider_.setValue(static_cast<double>(skew), juce::dontSendNotification);
        applyPreview(depth, skew, juce::roundToInt(cyclesSlider_.getValue()),
                     static_cast<float>(quantizeSlider_.getValue()),
                     juce::roundToInt(quantizeSubSlider_.getValue()), curveDisplay_.getHardAngle());
    };

    // Hard angle toggle → live preview
    curveDisplay_.onHardAngleChanged = [this](bool) {
        applyPreview(curveDisplay_.getDepth(), curveDisplay_.getSkew(),
                     juce::roundToInt(cyclesSlider_.getValue()),
                     static_cast<float>(quantizeSlider_.getValue()),
                     juce::roundToInt(quantizeSubSlider_.getValue()), curveDisplay_.getHardAngle());
    };

    // Apply button
    applyButton_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(0.6f));
    applyButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    applyButton_.onClick = [this] {
        applied_ = true;
        // Restore originals so the command captures them for undo
        restoreOriginals();
        if (onApply)
            onApply(curveDisplay_.getDepth(), curveDisplay_.getSkew(),
                    juce::roundToInt(cyclesSlider_.getValue()),
                    static_cast<float>(quantizeSlider_.getValue()),
                    juce::roundToInt(quantizeSubSlider_.getValue()), curveDisplay_.getHardAngle());
        if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
            callout->dismiss();
        else
            delete this;
    };
    addAndMakeVisible(applyButton_);

    // Cancel button
    cancelButton_.setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.15f));
    cancelButton_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    cancelButton_.onClick = [this] {
        restoreOriginals();
        applied_ = true;  // prevent destructor double-restore
        if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
            callout->dismiss();
        else
            delete this;
    };
    addAndMakeVisible(cancelButton_);

    setSize(280, 370 + 2 * (ROW_HEIGHT + GAP) + TITLE_BAR_HEIGHT);
}

TimeBendPopup::~TimeBendPopup() {
    // If dismissed without Apply or Cancel (e.g. clicked outside), restore originals
    if (!applied_)
        restoreOriginals();
}

void TimeBendPopup::applyPreview(float depth, float skew, int cycles, float quantize,
                                 int quantizeSub, bool hardAngle) {
    auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    if (!clip || clip->type != magda::ClipType::MIDI || originalStartBeats_.size() < 2)
        return;

    // Identity curve with no quantize — restore exact originals to avoid float drift
    if (std::abs(depth) < 0.001f && std::abs(skew) < 0.001f && quantize < 0.001f) {
        restoreOriginals();
        return;
    }

    // Find span from originals
    double minBeat = *std::min_element(originalStartBeats_.begin(), originalStartBeats_.end());
    double maxBeat = *std::max_element(originalStartBeats_.begin(), originalStartBeats_.end());
    double span = maxBeat - minBeat;
    if (span < 1e-9)
        return;

    // Apply curve to each note (with cycles)
    int c = std::max(1, cycles);
    double segLen = 1.0 / static_cast<double>(c);
    for (size_t i = 0; i < noteIndices_.size() && i < originalStartBeats_.size(); ++i) {
        size_t index = noteIndices_[i];
        if (index >= clip->midiNotes.size())
            continue;
        double t = (originalStartBeats_[i] - minBeat) / span;
        int seg = std::min(static_cast<int>(t / segLen), c - 1);
        double tLocal = (t - seg * segLen) / segLen;
        double tLocalEased = daw::audio::StepClock::applyRampCurve(tLocal, depth, skew, hardAngle);
        double tEased = (seg + tLocalEased) * segLen;
        double newBeat = minBeat + tEased * span;

        // Apply quantize snap
        if (quantize > 0.0f && quantizeSub > 0) {
            double gridSpacing = span / static_cast<double>(quantizeSub);
            double snapped = std::round((newBeat - minBeat) / gridSpacing) * gridSpacing + minBeat;
            newBeat += (snapped - newBeat) * static_cast<double>(quantize);
        }

        clip->midiNotes[index].startBeat = newBeat;
    }

    magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

void TimeBendPopup::restoreOriginals() {
    auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    if (!clip || clip->type != magda::ClipType::MIDI)
        return;

    for (size_t i = 0; i < noteIndices_.size() && i < originalStartBeats_.size(); ++i) {
        size_t index = noteIndices_[i];
        if (index < clip->midiNotes.size())
            clip->midiNotes[index].startBeat = originalStartBeats_[i];
    }

    magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

void TimeBendPopup::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Title bar
    auto titleArea = getLocalBounds().removeFromTop(TITLE_BAR_HEIGHT);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.08f));
    g.fillRect(titleArea);
    g.setColour(DarkTheme::getSecondaryTextColour());
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.drawText("TIME BEND", titleArea.reduced(6, 0), juce::Justification::centredLeft);
    // Separator
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
    g.drawHorizontalLine(TITLE_BAR_HEIGHT, 0.0f, static_cast<float>(getWidth()));
}

void TimeBendPopup::mouseDown(const juce::MouseEvent& e) {
    if (e.y < TITLE_BAR_HEIGHT)
        dragger_.startDraggingComponent(this, e);
}

void TimeBendPopup::mouseDrag(const juce::MouseEvent& e) {
    if (e.mouseWasClicked())
        return;
    dragger_.dragComponent(this, e, nullptr);
}

void TimeBendPopup::showAbove(std::unique_ptr<TimeBendPopup> popup, juce::Component* anchor) {
    // Singleton: dismiss any existing popup before showing a new one so old
    // foreground popups don't accumulate.
    dismissCurrent();

    auto* raw = popup.release();
    auto screenBounds = anchor->getScreenBounds();
    int x = screenBounds.getCentreX() - raw->getWidth() / 2;
    int y = screenBounds.getY() - raw->getHeight() - 4;
    raw->setTopLeftPosition(x, y);
    // Always-on-top so it stays visible during playback even when the main
    // window keeps focus and repaints frequently (playhead, etc).
    raw->setAlwaysOnTop(true);
    raw->addToDesktop(juce::ComponentPeer::windowHasDropShadow);
    raw->setVisible(true);
    raw->toFront(true);
    currentPopup_ = raw;
}

void TimeBendPopup::resized() {
    auto bounds = getLocalBounds();
    bounds.removeFromTop(TITLE_BAR_HEIGHT);
    bounds.reduce(PADDING, PADDING);

    // Slider rows at top
    auto depthRow = bounds.removeFromTop(ROW_HEIGHT);
    depthLabel_.setBounds(depthRow.removeFromLeft(LABEL_WIDTH));
    depthSlider_.setBounds(depthRow);

    bounds.removeFromTop(GAP);

    auto skewRow = bounds.removeFromTop(ROW_HEIGHT);
    skewLabel_.setBounds(skewRow.removeFromLeft(LABEL_WIDTH));
    skewSlider_.setBounds(skewRow);

    bounds.removeFromTop(GAP);

    auto cyclesRow = bounds.removeFromTop(ROW_HEIGHT);
    cyclesLabel_.setBounds(cyclesRow.removeFromLeft(LABEL_WIDTH));
    cyclesSlider_.setBounds(cyclesRow);

    bounds.removeFromTop(GAP);

    auto quantizeRow = bounds.removeFromTop(ROW_HEIGHT);
    quantizeLabel_.setBounds(quantizeRow.removeFromLeft(LABEL_WIDTH));
    quantizeSlider_.setBounds(quantizeRow);

    bounds.removeFromTop(GAP);

    auto quantizeSubRow = bounds.removeFromTop(ROW_HEIGHT);
    quantizeSubLabel_.setBounds(quantizeSubRow.removeFromLeft(LABEL_WIDTH));
    quantizeSubSlider_.setBounds(quantizeSubRow);

    bounds.removeFromTop(GAP);

    // Buttons at bottom
    auto buttonRow = bounds.removeFromBottom(BUTTON_HEIGHT);
    int buttonWidth = (buttonRow.getWidth() - GAP) / 2;
    cancelButton_.setBounds(buttonRow.removeFromLeft(buttonWidth));
    buttonRow.removeFromLeft(GAP);
    applyButton_.setBounds(buttonRow);

    bounds.removeFromBottom(GAP);

    // Curve display fills remaining space
    curveDisplay_.setBounds(bounds);
}

}  // namespace magda::daw::ui
