#include "SessionClipEditor.hpp"

#include "../../audio/AudioThumbnailManager.hpp"
#include "../state/TimelineController.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "BinaryData.h"
#include "core/ClipDisplayInfo.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/UndoManager.hpp"

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
        if (!clip || !clip->isAudio() || clip->audio().source.filePath.isEmpty()) {
            // No waveform to show
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(FontManager::getInstance().getUIFont(14.0f));
            g.drawText("No audio source", bounds, juce::Justification::centred);
            return;
        }

        auto waveformBounds = bounds.reduced(MARGIN);

        // Get waveform from cache
        auto* thumbnail =
            magda::AudioThumbnailManager::getInstance().getThumbnail(clip->audio().source.filePath);
        if (thumbnail && thumbnail->getTotalLength() > 0.0) {
            // Build display info using project BPM, passing the file
            // duration so sourceFileStart / sourceFileEnd describe the
            // real file (ClipDisplayInfo's no-thumbnail fallback would
            // otherwise derive a clip-length-based extent here).
            double bpm = 120.0;
            if (auto* controller = TimelineController::getCurrent()) {
                bpm = controller->getState().tempo.bpm;
            }
            const double thumbnailLength = thumbnail->getTotalLength();
            auto di = ClipDisplayInfo::from(*clip, bpm, thumbnailLength);

            // Visible source range = the full file extent.
            double startTime = di.sourceFileStart;
            double endTime = di.sourceFileEnd;

            // Draw waveform
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
            thumbnail->drawChannels(g, waveformBounds, startTime, endTime, 1.0f);

            // Draw loop region overlay. Loop X positions come from the
            // loop-region fields, NOT from the file extent — a loop with
            // a non-zero start used to be drawn at the left edge of the
            // waveform because the previous code hard-coded loopStartX
            // to waveformBounds.getX(). Now we project the loop range
            // through the same transform the waveform uses.
            if (di.isLooped()) {
                const double visibleDuration = endTime - startTime;
                if (visibleDuration > 0.0) {
                    const double loopStartInFile = di.loopRegionStartSource - startTime;
                    const double loopEndInFile =
                        di.loopRegionStartSource + di.loopRegionLengthSource - startTime;
                    const double scale = waveformBounds.getWidth() / visibleDuration;

                    int loopStartX =
                        waveformBounds.getX() + static_cast<int>(loopStartInFile * scale);
                    int loopEndX = waveformBounds.getX() + static_cast<int>(loopEndInFile * scale);

                    juce::Rectangle<int> loopRegion(loopStartX, waveformBounds.getY(),
                                                    loopEndX - loopStartX,
                                                    waveformBounds.getHeight());

                    g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE).withAlpha(0.2f));
                    g.fillRect(loopRegion);

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

    // Loop toggle (single-icon mode with programmatic bg/border)
    loopToggle_ = std::make_unique<SvgButton>("Loop", BinaryData::loop_icon_svg,
                                              BinaryData::loop_icon_svgSize);
    loopToggle_->setOriginalColor(juce::Colour(0xffBCBCBC));
    loopToggle_->setNormalColor(juce::Colour(0xff999999));
    loopToggle_->setActiveColor(juce::Colour(0xffCCCCCC));
    loopToggle_->setActiveBackgroundColor(juce::Colour(0xff5588AA));
    loopToggle_->setNormalBackgroundColor(juce::Colour(0xff2A2A2A));
    loopToggle_->setBorderColor(juce::Colour(0xff555555));
    loopToggle_->setBorderThickness(1.0f);
    loopToggle_->setCornerRadius(4.0f);
    loopToggle_->onClick = [this]() {
        bool newState = !loopToggle_->isActive();
        loopToggle_->setActive(newState);
        auto& clipManager = ClipManager::getInstance();
        double bpm = 120.0;
        if (auto* controller = TimelineController::getCurrent()) {
            bpm = controller->getState().tempo.bpm;
        }
        clipManager.setClipLoopEnabled(clipId_, newState, bpm);
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
                             DarkTheme::getColour(DarkTheme::CONTROL_SLIDER_THUMB));
    offsetSlider_->setColour(juce::Slider::trackColourId,
                             DarkTheme::getColour(DarkTheme::CONTROL_VALUE_FILL));
    offsetSlider_->setColour(juce::Slider::textBoxTextColourId, DarkTheme::getTextColour());
    offsetSlider_->setColour(juce::Slider::textBoxBackgroundColourId,
                             DarkTheme::getColour(DarkTheme::SURFACE));
    offsetSlider_->onValueChange = [this]() {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetClipOffsetCommand>(clipId_, offsetSlider_->getValue()));
        waveformDisplay_->repaint();
    };
    addAndMakeVisible(*offsetSlider_);

    fadesSection_ = std::make_unique<magda::daw::ui::ClipFadesSection>();
    addAndMakeVisible(*fadesSection_);
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

    loopToggle_->setBounds(headerBounds.removeFromRight(30));
    headerBounds.removeFromRight(MARGIN * 2);

    lengthLabel_->setBounds(headerBounds.removeFromRight(120));
    headerBounds.removeFromRight(MARGIN);

    clipNameLabel_->setBounds(headerBounds);

    // Footer — offset row on top, fades section below
    auto footerBounds = bounds.removeFromBottom(FOOTER_HEIGHT);
    footerBounds.reduce(MARGIN, MARGIN);

    auto offsetRow = footerBounds.removeFromTop(30);
    offsetLabel_->setBounds(offsetRow.removeFromLeft(80));
    offsetRow.removeFromLeft(MARGIN);
    offsetSlider_->setBounds(offsetRow);

    footerBounds.removeFromTop(4);
    if (fadesSection_)
        fadesSection_->setBounds(footerBounds);

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
    loopToggle_->setActive(clip->loopEnabled);

    // Update length label
    const double bpm = TimelineController::getCurrent()
                           ? TimelineController::getCurrent()->getState().tempo.bpm
                           : 120.0;
    lengthLabel_->setText(juce::String(clip->getLengthInBeats(bpm), 2) + " beats",
                          juce::dontSendNotification);

    // Update offset slider
    if (clip->audio().source.filePath.isNotEmpty()) {
        offsetSlider_->setValue(clip->offset, juce::dontSendNotification);
    }

    fadesSection_->setClip(clipId_);
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
