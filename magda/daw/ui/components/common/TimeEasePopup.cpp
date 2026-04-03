#include "TimeEasePopup.hpp"

#include "audio/StepClock.hpp"
#include "core/ClipManager.hpp"

namespace magda::daw::ui {

static constexpr int ROW_HEIGHT = 22;
static constexpr int LABEL_WIDTH = 44;
static constexpr int PADDING = 8;
static constexpr int GAP = 4;
static constexpr int BUTTON_HEIGHT = 26;

TimeEasePopup::TimeEasePopup(magda::ClipId clipId, std::vector<size_t> noteIndices)
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
                     juce::roundToInt(cyclesSlider_.getValue()));
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
                     juce::roundToInt(cyclesSlider_.getValue()));
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
                     juce::roundToInt(cyclesSlider_.getValue()));
    };
    addAndMakeVisible(cyclesSlider_);

    // Sync curve → sliders + live preview
    curveDisplay_.onCurveChanged = [this](float depth, float skew) {
        depthSlider_.setValue(static_cast<double>(depth), juce::dontSendNotification);
        skewSlider_.setValue(static_cast<double>(skew), juce::dontSendNotification);
        applyPreview(depth, skew, juce::roundToInt(cyclesSlider_.getValue()));
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
                    juce::roundToInt(cyclesSlider_.getValue()));
        if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
            callout->dismiss();
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
    };
    addAndMakeVisible(cancelButton_);

    setSize(280, 370);
}

TimeEasePopup::~TimeEasePopup() {
    // If dismissed without Apply or Cancel (e.g. clicked outside), restore originals
    if (!applied_)
        restoreOriginals();
}

void TimeEasePopup::applyPreview(float depth, float skew, int cycles) {
    auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    if (!clip || clip->type != magda::ClipType::MIDI || originalStartBeats_.size() < 2)
        return;

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
        double tLocalEased = daw::audio::StepClock::applyRampCurve(tLocal, depth, skew);
        double tEased = (seg + tLocalEased) * segLen;
        clip->midiNotes[index].startBeat = minBeat + tEased * span;
    }

    magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(clipId_);
}

void TimeEasePopup::restoreOriginals() {
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

void TimeEasePopup::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));
}

void TimeEasePopup::resized() {
    auto bounds = getLocalBounds().reduced(PADDING);

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
