#include "SessionClipEditor.hpp"

#include "../../audio/AudioThumbnailManager.hpp"
#include "../state/TimelineController.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "core/ClipDisplayInfo.hpp"

namespace magda {

// ============================================================================
// WaveformDisplay - Inner class for waveform rendering
// ============================================================================

class SessionClipEditor::WaveformDisplay : public juce::Component {
  public:
    WaveformDisplay(ClipId clipId) : clipId_(clipId) {}

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds();

        // Background
        g.fillAll(DarkTheme::getColour(DarkTheme::BACKGROUND));

        // Border
        g.setColour(DarkTheme::getBorderColour());
        g.drawRect(bounds, 1);

        // Get clip info
        const auto* clip = ClipManager::getInstance().getClip(clipId_);
        if (!clip || clip->type != ClipType::Audio || clip->audioFilePath.isEmpty()) {
            // No waveform to show
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(14.0f));
            g.drawText("No audio source", bounds, juce::Justification::centred);
            return;
        }

        auto waveformBounds = bounds.reduced(MARGIN);

        // Get waveform from cache
        auto* thumbnail =
            magda::AudioThumbnailManager::getInstance().getThumbnail(clip->audioFilePath);
        if (thumbnail && thumbnail->getTotalLength() > 0.0) {
            // Build display info using project BPM
            double bpm = 120.0;
            if (auto* controller = TimelineController::getCurrent()) {
                bpm = controller->getState().tempo.bpm;
            }
            auto di = ClipDisplayInfo::from(*clip, bpm);

            // Calculate visible time range based on clip length and offset
            double startTime = di.sourceFileStart;
            double endTime = di.sourceFileEnd;

            // Draw waveform
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            thumbnail->drawChannels(g, waveformBounds, startTime, endTime, 1.0f);

            // Draw loop region if enabled
            if (di.isLooped()) {
                double loopSourceLength = di.loopLengthSeconds / di.stretchFactor;
                double loopEndTime = startTime + loopSourceLength;

                if (loopEndTime <= endTime) {
                    // Calculate loop region bounds (loop starts at clip beginning)
                    double visibleDuration = endTime - startTime;

                    int loopStartX = waveformBounds.getX();
                    int loopEndX = waveformBounds.getX() +
                                   static_cast<int>(loopSourceLength / visibleDuration *
                                                    waveformBounds.getWidth());

                    // Draw loop region overlay
                    juce::Rectangle<int> loopRegion(loopStartX, waveformBounds.getY(),
                                                    loopEndX - loopStartX,
                                                    waveformBounds.getHeight());

                    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.2f));
                    g.fillRect(loopRegion);

                    // Draw loop boundaries
                    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
                    g.drawVerticalLine(loopStartX, waveformBounds.getY(),
                                       waveformBounds.getBottom());
                    g.drawVerticalLine(loopEndX, waveformBounds.getY(), waveformBounds.getBottom());

                    // Draw "L" label
                    g.setFont(FontManager::getInstance().getUIFontBold(10.0f));
                    g.drawText("L", loopStartX + 2, waveformBounds.getY(), 20, 20,
                               juce::Justification::centredLeft);
                }
            }
        } else {
            // Waveform loading
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(14.0f));
            g.drawText("Loading waveform...", bounds, juce::Justification::centred);
        }
    }

    void setClip(ClipId clipId) {
        if (clipId_ != clipId) {
            clipId_ = clipId;
            repaint();
        }
    }

  private:
    ClipId clipId_;
    static constexpr int MARGIN = 4;
};

// ============================================================================
// SessionClipEditor
// ============================================================================

SessionClipEditor::SessionClipEditor(ClipId clipId) : clipId_(clipId) {
    // Register as listener
    ClipManager::getInstance().addListener(this);

    // Cache clip info
    updateClipCache();

    // Setup UI components
    setupHeader();
    setupWaveform();
    setupFooter();

    // Update controls to reflect current clip state
    updateControls();

    setSize(600, 400);
}

SessionClipEditor::~SessionClipEditor() {
    ClipManager::getInstance().removeListener(this);
}

void SessionClipEditor::setupHeader() {
    // Clip name label
    clipNameLabel_ = std::make_unique<juce::Label>();
    clipNameLabel_->setFont(FontManager::getInstance().getUIFontBold(16.0f));
    clipNameLabel_->setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    clipNameLabel_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*clipNameLabel_);

    // Loop toggle
    loopToggle_ = std::make_unique<juce::ToggleButton>("Loop");
    loopToggle_->setColour(juce::ToggleButton::textColourId, DarkTheme::getTextColour());
    loopToggle_->setColour(juce::ToggleButton::tickColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    loopToggle_->onClick = [this]() {
        auto& clipManager = ClipManager::getInstance();
        double bpm = 120.0;
        if (auto* controller = TimelineController::getCurrent()) {
            bpm = controller->getState().tempo.bpm;
        }
        clipManager.setClipLoopEnabled(clipId_, loopToggle_->getToggleState(), bpm);
    };
    addAndMakeVisible(*loopToggle_);

    // Length label
    lengthLabel_ = std::make_unique<juce::Label>();
    lengthLabel_->setFont(FontManager::getInstance().getUIFont(12.0f));
    lengthLabel_->setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    lengthLabel_->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*lengthLabel_);

    // Close button
    closeButton_ = std::make_unique<juce::TextButton>("✕");
    closeButton_->setColour(juce::TextButton::buttonColourId,
                            DarkTheme::getColour(DarkTheme::BUTTON_NORMAL));
    closeButton_->setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
    closeButton_->onClick = [this]() {
        if (onCloseRequested)
            onCloseRequested();
    };
    addAndMakeVisible(*closeButton_);
}

void SessionClipEditor::setupWaveform() {
    waveformDisplay_ = std::make_unique<WaveformDisplay>(clipId_);
    addAndMakeVisible(*waveformDisplay_);
}

void SessionClipEditor::setupFooter() {
    // Offset label
    offsetLabel_ = std::make_unique<juce::Label>();
    offsetLabel_->setText("Offset (s):", juce::dontSendNotification);
    offsetLabel_->setFont(FontManager::getInstance().getUIFont(12.0f));
    offsetLabel_->setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    offsetLabel_->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*offsetLabel_);

    // Offset slider
    offsetSlider_ =
        std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    offsetSlider_->setRange(0.0, 60.0, 0.01);  // 0-60 seconds
    offsetSlider_->setColour(juce::Slider::backgroundColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    offsetSlider_->setColour(juce::Slider::thumbColourId,
                             DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    offsetSlider_->setColour(juce::Slider::trackColourId,
                             DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.5f));
    offsetSlider_->setColour(juce::Slider::textBoxTextColourId, DarkTheme::getTextColour());
    offsetSlider_->setColour(juce::Slider::textBoxBackgroundColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    offsetSlider_->onValueChange = [this]() {
        ClipManager::getInstance().setAudioOffset(clipId_, offsetSlider_->getValue());
        waveformDisplay_->repaint();
    };
    addAndMakeVisible(*offsetSlider_);
}

void SessionClipEditor::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));

    // Draw header background
    auto headerBounds = getLocalBounds().removeFromTop(HEADER_HEIGHT);
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRect(headerBounds);

    // Draw footer background
    auto footerBounds = getLocalBounds().withTop(getHeight() - FOOTER_HEIGHT);
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRect(footerBounds);
}

void SessionClipEditor::resized() {
    auto bounds = getLocalBounds();

    // Header
    auto headerBounds = bounds.removeFromTop(HEADER_HEIGHT);
    headerBounds.reduce(MARGIN, MARGIN);

    closeButton_->setBounds(headerBounds.removeFromRight(30));
    headerBounds.removeFromRight(MARGIN);

    loopToggle_->setBounds(headerBounds.removeFromRight(80));
    headerBounds.removeFromRight(MARGIN * 2);

    lengthLabel_->setBounds(headerBounds.removeFromRight(120));
    headerBounds.removeFromRight(MARGIN);

    clipNameLabel_->setBounds(headerBounds);

    // Footer
    auto footerBounds = bounds.removeFromBottom(FOOTER_HEIGHT);
    footerBounds.reduce(MARGIN, MARGIN);

    offsetLabel_->setBounds(footerBounds.removeFromLeft(80));
    footerBounds.removeFromLeft(MARGIN);

    offsetSlider_->setBounds(footerBounds);

    // Waveform takes remaining space
    bounds.reduce(MARGIN, MARGIN);
    waveformDisplay_->setBounds(bounds);
}

void SessionClipEditor::clipsChanged() {
    // Check if our clip still exists
    if (ClipManager::getInstance().getClip(clipId_) == nullptr) {
        // Clip was deleted, close editor
        if (onCloseRequested)
            onCloseRequested();
    }
}

void SessionClipEditor::clipPropertyChanged(ClipId clipId) {
    if (clipId == clipId_) {
        updateClipCache();
        updateControls();
        waveformDisplay_->repaint();
    }
}

void SessionClipEditor::updateClipCache() {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip) {
        cachedClip_ = *clip;
    }
}

void SessionClipEditor::updateControls() {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip)
        return;

    // Update clip name
    clipNameLabel_->setText(clip->name, juce::dontSendNotification);

    // Update loop toggle
    loopToggle_->setToggleState(clip->internalLoopEnabled, juce::dontSendNotification);

    // Update length label
    lengthLabel_->setText(juce::String(clip->length, 2) + " beats", juce::dontSendNotification);

    // Update offset slider
    if (clip->audioFilePath.isNotEmpty()) {
        offsetSlider_->setValue(clip->audioOffset, juce::dontSendNotification);
    }
}

// ============================================================================
// SessionClipEditorWindow
// ============================================================================

SessionClipEditorWindow::SessionClipEditorWindow(ClipId clipId, const juce::String& clipName)
    : DocumentWindow("Edit Clip: " + clipName, DarkTheme::getColour(DarkTheme::BACKGROUND),
                     DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);

    editor_ = std::make_unique<SessionClipEditor>(clipId);
    editor_->onCloseRequested = [this]() { closeButtonPressed(); };

    setContentNonOwned(editor_.get(), true);
    setResizable(true, false);
    centreWithSize(600, 400);
    setVisible(true);
}

SessionClipEditorWindow::~SessionClipEditorWindow() = default;

void SessionClipEditorWindow::closeButtonPressed() {
    if (isCurrentlyModal())
        exitModalState(0);
    else
        setVisible(false);
}

}  // namespace magda
