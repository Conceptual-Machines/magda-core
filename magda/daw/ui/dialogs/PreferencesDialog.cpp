#include "PreferencesDialog.hpp"

#include "../themes/DarkTheme.hpp"
#include "../themes/FontManager.hpp"
#include "core/Config.hpp"

// ---------------------------------------------------------------------------
// Setup helpers — internal linkage, shared by all page classes
// ---------------------------------------------------------------------------
namespace {

void setupSlider(juce::Component& owner, juce::Slider& slider, juce::Label& label,
                 const juce::String& labelText, double min, double max, double interval,
                 const juce::String& suffix = "") {
    label.setText(labelText, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId,
                    magda::DarkTheme::getColour(magda::DarkTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
    owner.addAndMakeVisible(label);

    slider.setRange(min, max, interval);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::backgroundColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::SURFACE));
    slider.setColour(juce::Slider::thumbColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::ACCENT_BLUE));
    slider.setColour(juce::Slider::trackColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::ACCENT_BLUE).darker(0.3f));
    slider.setColour(juce::Slider::textBoxTextColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::TEXT_PRIMARY));
    slider.setColour(juce::Slider::textBoxBackgroundColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::SURFACE));
    slider.setColour(juce::Slider::textBoxOutlineColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::BORDER));
    owner.addAndMakeVisible(slider);
}

void setupToggle(juce::Component& owner, juce::ToggleButton& toggle, const juce::String& text) {
    toggle.setButtonText(text);
    toggle.setColour(juce::ToggleButton::textColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::TEXT_PRIMARY));
    toggle.setColour(juce::ToggleButton::tickColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::ACCENT_BLUE));
    toggle.setColour(juce::ToggleButton::tickDisabledColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::TEXT_DIM));
    owner.addAndMakeVisible(toggle);
}

void setupSectionHeader(juce::Component& owner, juce::Label& header, const juce::String& text) {
    header.setText(text, juce::dontSendNotification);
    header.setColour(juce::Label::textColourId,
                     magda::DarkTheme::getColour(magda::DarkTheme::TEXT_SECONDARY));
    header.setFont(magda::FontManager::getInstance().getUIFontBold(14.0f));
    header.setJustificationType(juce::Justification::centredLeft);
    owner.addAndMakeVisible(header);
}

void setupShortcutLabel(juce::Component& owner, juce::Label& label, const juce::String& action,
                        const juce::String& shortcut) {
    label.setText(action + ":  " + shortcut, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId,
                    magda::DarkTheme::getColour(magda::DarkTheme::TEXT_PRIMARY));
    label.setJustificationType(juce::Justification::centredLeft);
    owner.addAndMakeVisible(label);
}

}  // namespace

// ---------------------------------------------------------------------------
// Tab page components
// ---------------------------------------------------------------------------
namespace magda {

// ---- General tab: Zoom, Timeline, Transport Display -----------------------

class GeneralPage : public juce::Component {
  public:
    GeneralPage() {
        setupSectionHeader(*this, zoomHeader, "Zoom");
        setupSlider(*this, zoomInSensitivitySlider, zoomInLabel, "Zoom In Sensitivity", 5.0, 100.0,
                    1.0);
        setupSlider(*this, zoomOutSensitivitySlider, zoomOutLabel, "Zoom Out Sensitivity", 5.0,
                    100.0, 1.0);
        setupSlider(*this, zoomShiftSensitivitySlider, zoomShiftLabel, "Shift+Zoom Sensitivity",
                    1.0, 50.0, 0.5);

        setupSectionHeader(*this, timelineHeader, "Timeline");
        setupSlider(*this, timelineLengthSlider, timelineLengthLabel, "Default Length (sec)", 60.0,
                    1800.0, 10.0, " sec");
        setupSlider(*this, viewDurationSlider, viewDurationLabel, "Default View Duration", 10.0,
                    300.0, 5.0, " sec");

        setupSectionHeader(*this, transportHeader, "Transport Display");
        setupToggle(*this, showBothFormatsToggle, "Show both time formats");
        setupToggle(*this, defaultBarsBeatsToggle, "Default to Bars/Beats (vs Seconds)");
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int rowH = 32;
        const int labelW = 180;
        const int sliderH = 24;
        const int toggleH = 24;
        const int headerH = 28;
        const int secGap = 12;

        // Zoom
        zoomHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        layoutSliderRow(bounds, zoomInLabel, zoomInSensitivitySlider, rowH, labelW, sliderH);
        bounds.removeFromTop(4);
        layoutSliderRow(bounds, zoomOutLabel, zoomOutSensitivitySlider, rowH, labelW, sliderH);
        bounds.removeFromTop(4);
        layoutSliderRow(bounds, zoomShiftLabel, zoomShiftSensitivitySlider, rowH, labelW, sliderH);
        bounds.removeFromTop(secGap);

        // Timeline
        timelineHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        layoutSliderRow(bounds, timelineLengthLabel, timelineLengthSlider, rowH, labelW, sliderH);
        bounds.removeFromTop(4);
        layoutSliderRow(bounds, viewDurationLabel, viewDurationSlider, rowH, labelW, sliderH);
        bounds.removeFromTop(secGap);

        // Transport
        transportHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        showBothFormatsToggle.setBounds(bounds.removeFromTop(toggleH + 8).reduced(0, 4));
        bounds.removeFromTop(4);
        defaultBarsBeatsToggle.setBounds(bounds.removeFromTop(toggleH + 8).reduced(0, 4));
    }

    void loadSettings(Config& config) {
        zoomInSensitivitySlider.setValue(config.getZoomInSensitivity(), juce::dontSendNotification);
        zoomOutSensitivitySlider.setValue(config.getZoomOutSensitivity(),
                                          juce::dontSendNotification);
        zoomShiftSensitivitySlider.setValue(config.getZoomInSensitivityShift(),
                                            juce::dontSendNotification);
        timelineLengthSlider.setValue(config.getDefaultTimelineLength(),
                                      juce::dontSendNotification);
        viewDurationSlider.setValue(config.getDefaultZoomViewDuration(),
                                    juce::dontSendNotification);
        showBothFormatsToggle.setToggleState(config.getTransportShowBothFormats(),
                                             juce::dontSendNotification);
        defaultBarsBeatsToggle.setToggleState(config.getTransportDefaultBarsBeats(),
                                              juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        config.setZoomInSensitivity(zoomInSensitivitySlider.getValue());
        config.setZoomOutSensitivity(zoomOutSensitivitySlider.getValue());
        config.setZoomInSensitivityShift(zoomShiftSensitivitySlider.getValue());
        config.setZoomOutSensitivityShift(zoomShiftSensitivitySlider.getValue());
        config.setDefaultTimelineLength(timelineLengthSlider.getValue());
        config.setDefaultZoomViewDuration(viewDurationSlider.getValue());
        config.setTransportShowBothFormats(showBothFormatsToggle.getToggleState());
        config.setTransportDefaultBarsBeats(defaultBarsBeatsToggle.getToggleState());
    }

  private:
    static void layoutSliderRow(juce::Rectangle<int>& bounds, juce::Label& label,
                                juce::Slider& slider, int rowH, int labelW, int sliderH) {
        auto row = bounds.removeFromTop(rowH);
        label.setBounds(row.removeFromLeft(labelW));
        slider.setBounds(row.reduced(0, (rowH - sliderH) / 2));
    }

    juce::Label zoomHeader, timelineHeader, transportHeader;
    juce::Slider zoomInSensitivitySlider, zoomOutSensitivitySlider, zoomShiftSensitivitySlider;
    juce::Label zoomInLabel, zoomOutLabel, zoomShiftLabel;
    juce::Slider timelineLengthSlider, viewDurationSlider;
    juce::Label timelineLengthLabel, viewDurationLabel;
    juce::ToggleButton showBothFormatsToggle, defaultBarsBeatsToggle;
};

// ---- UI tab: Panels, Behavior (incl. showTooltips), Layout ----------------

class UIPage : public juce::Component {
  public:
    UIPage() {
        setupSectionHeader(*this, panelsHeader, "Panels (Default Visibility)");
        setupToggle(*this, showLeftPanelToggle, "Show Left Panel (Browser)");
        setupToggle(*this, showRightPanelToggle, "Show Right Panel (Inspector)");
        setupToggle(*this, showBottomPanelToggle, "Show Bottom Panel (Mixer)");

        setupSectionHeader(*this, behaviorHeader, "Behavior");
        setupToggle(*this, confirmTrackDeleteToggle, "Confirm before deleting tracks");
        setupToggle(*this, autoMonitorToggle, "Auto-monitor selected track");
        setupToggle(*this, showTooltipsToggle, "Show tooltips");

        setupSectionHeader(*this, layoutHeader, "Layout");
        setupToggle(*this, leftHandedLayoutToggle, "Headers on Right");
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int toggleH = 24;
        const int headerH = 28;
        const int secGap = 12;

        // Panels
        panelsHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        showLeftPanelToggle.setBounds(bounds.removeFromTop(toggleH + 8).reduced(0, 4));
        bounds.removeFromTop(4);
        showRightPanelToggle.setBounds(bounds.removeFromTop(toggleH + 8).reduced(0, 4));
        bounds.removeFromTop(4);
        showBottomPanelToggle.setBounds(bounds.removeFromTop(toggleH + 8).reduced(0, 4));
        bounds.removeFromTop(secGap);

        // Behavior
        behaviorHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        confirmTrackDeleteToggle.setBounds(bounds.removeFromTop(toggleH + 8).reduced(0, 4));
        bounds.removeFromTop(4);
        autoMonitorToggle.setBounds(bounds.removeFromTop(toggleH + 8).reduced(0, 4));
        bounds.removeFromTop(4);
        showTooltipsToggle.setBounds(bounds.removeFromTop(toggleH + 8).reduced(0, 4));
        bounds.removeFromTop(secGap);

        // Layout
        layoutHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        leftHandedLayoutToggle.setBounds(bounds.removeFromTop(toggleH + 8).reduced(0, 4));
    }

    void loadSettings(Config& config) {
        showLeftPanelToggle.setToggleState(config.getShowLeftPanel(), juce::dontSendNotification);
        showRightPanelToggle.setToggleState(config.getShowRightPanel(), juce::dontSendNotification);
        showBottomPanelToggle.setToggleState(config.getShowBottomPanel(),
                                             juce::dontSendNotification);
        confirmTrackDeleteToggle.setToggleState(config.getConfirmTrackDelete(),
                                                juce::dontSendNotification);
        autoMonitorToggle.setToggleState(config.getAutoMonitorSelectedTrack(),
                                         juce::dontSendNotification);
        showTooltipsToggle.setToggleState(config.getShowTooltips(), juce::dontSendNotification);
        leftHandedLayoutToggle.setToggleState(config.getScrollbarOnLeft(),
                                              juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        config.setShowLeftPanel(showLeftPanelToggle.getToggleState());
        config.setShowRightPanel(showRightPanelToggle.getToggleState());
        config.setShowBottomPanel(showBottomPanelToggle.getToggleState());
        config.setConfirmTrackDelete(confirmTrackDeleteToggle.getToggleState());
        config.setAutoMonitorSelectedTrack(autoMonitorToggle.getToggleState());
        config.setShowTooltips(showTooltipsToggle.getToggleState());
        config.setScrollbarOnLeft(leftHandedLayoutToggle.getToggleState());
    }

  private:
    juce::Label panelsHeader, behaviorHeader, layoutHeader;
    juce::ToggleButton showLeftPanelToggle, showRightPanelToggle, showBottomPanelToggle;
    juce::ToggleButton confirmTrackDeleteToggle, autoMonitorToggle, showTooltipsToggle;
    juce::ToggleButton leftHandedLayoutToggle;
};

// ---- Rendering tab --------------------------------------------------------

class RenderingPage : public juce::Component {
  public:
    RenderingPage() {
        setupSectionHeader(*this, renderHeader, "Rendering");

        renderFolderLabel.setText("Render Output Folder", juce::dontSendNotification);
        renderFolderLabel.setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        renderFolderLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(renderFolderLabel);

        renderFolderValue.setText("Default (beside source file)", juce::dontSendNotification);
        renderFolderValue.setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        renderFolderValue.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(renderFolderValue);

        renderFolderBrowseButton.setButtonText("Browse...");
        renderFolderBrowseButton.onClick = [this]() {
            fileChooser_ = std::make_unique<juce::FileChooser>("Select Render Output Folder");
            fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                          juce::FileBrowserComponent::canSelectDirectories,
                                      [this](const juce::FileChooser& fc) {
                                          auto result = fc.getResult();
                                          if (result.exists()) {
                                              renderFolderValue.setText(result.getFullPathName(),
                                                                        juce::dontSendNotification);
                                              renderFolderValue.setTooltip(
                                                  result.getFullPathName());
                                          }
                                      });
        };
        addAndMakeVisible(renderFolderBrowseButton);

        renderFolderClearButton.setButtonText("Clear");
        renderFolderClearButton.onClick = [this]() {
            renderFolderValue.setText("Default (beside source file)", juce::dontSendNotification);
            renderFolderValue.setTooltip("");
        };
        addAndMakeVisible(renderFolderClearButton);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int rowH = 32;
        const int headerH = 28;

        renderHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);

        renderFolderLabel.setBounds(bounds.removeFromTop(rowH));
        bounds.removeFromTop(4);

        auto row = bounds.removeFromTop(rowH);
        auto buttonsArea = row.removeFromRight(140);
        renderFolderValue.setBounds(row);
        renderFolderClearButton.setBounds(buttonsArea.removeFromRight(60).reduced(0, 2));
        buttonsArea.removeFromRight(4);
        renderFolderBrowseButton.setBounds(buttonsArea.reduced(0, 2));
    }

    void loadSettings(Config& config) {
        auto folder = config.getRenderFolder();
        if (folder.empty()) {
            renderFolderValue.setText("Default (beside source file)", juce::dontSendNotification);
            renderFolderValue.setTooltip("");
        } else {
            renderFolderValue.setText(juce::String(folder), juce::dontSendNotification);
            renderFolderValue.setTooltip(juce::String(folder));
        }
    }

    void applySettings(Config& config) {
        config.setRenderFolder(renderFolderValue.getTooltip().toStdString());
    }

  private:
    juce::Label renderHeader;
    juce::Label renderFolderLabel;
    juce::Label renderFolderValue;
    juce::TextButton renderFolderBrowseButton;
    juce::TextButton renderFolderClearButton;
    std::unique_ptr<juce::FileChooser> fileChooser_;
};

// ---- AI tab ---------------------------------------------------------------

class AIPage : public juce::Component {
  public:
    AIPage() {
        setupSectionHeader(*this, aiHeader, "AI Assistant");

        aiApiKeyLabel.setText("OpenAI API Key", juce::dontSendNotification);
        aiApiKeyLabel.setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        aiApiKeyLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(aiApiKeyLabel);

        aiApiKeyEditor.setPasswordCharacter(static_cast<juce::juce_wchar>('*'));
        aiApiKeyEditor.setTextToShowWhenEmpty("sk-...", DarkTheme::getColour(DarkTheme::TEXT_DIM));
        aiApiKeyEditor.setColour(juce::TextEditor::backgroundColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
        aiApiKeyEditor.setColour(juce::TextEditor::textColourId,
                                 DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        aiApiKeyEditor.setColour(juce::TextEditor::outlineColourId,
                                 DarkTheme::getColour(DarkTheme::BORDER));
        addAndMakeVisible(aiApiKeyEditor);

        aiValidateButton.setButtonText("Validate");
        aiValidateButton.onClick = [this]() {
            auto key = aiApiKeyEditor.getText().trim();
            if (key.isEmpty()) {
                aiStatusLabel.setText("Enter an API key first", juce::dontSendNotification);
                aiStatusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
                return;
            }

            aiValidateButton.setEnabled(false);
            aiStatusLabel.setText("Validating...", juce::dontSendNotification);
            aiStatusLabel.setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));

            auto safeThis = juce::Component::SafePointer<AIPage>(this);

            juce::Thread::launch([safeThis, key]() {
                juce::URL url("https://api.openai.com/v1/models");
                juce::String headers = "Authorization: Bearer " + key;

                int statusCode = 0;
                auto options =
                    juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withExtraHeaders(headers)
                        .withConnectionTimeoutMs(10000)
                        .withStatusCode(&statusCode);

                auto stream = url.createInputStream(options);

                // Consume the response body so macOS NSURLSession doesn't
                // cancel the task when the stream is destroyed (avoids -999)
                if (stream)
                    stream->readEntireStreamAsString();

                bool valid = false;
                juce::String statusMsg;

                if (statusCode == 200) {
                    valid = true;
                    statusMsg = "Valid";
                } else if (statusCode == 401) {
                    statusMsg = "Invalid API key";
                } else if (statusCode > 0) {
                    statusMsg = "HTTP " + juce::String(statusCode);
                } else {
                    statusMsg = "Connection failed - check your network";
                }

                juce::MessageManager::callAsync([safeThis, valid, statusMsg]() {
                    if (!safeThis)
                        return;
                    safeThis->aiValidateButton.setEnabled(true);
                    safeThis->aiStatusLabel.setText(statusMsg, juce::dontSendNotification);
                    safeThis->aiStatusLabel.setColour(juce::Label::textColourId,
                                                      valid ? juce::Colours::limegreen
                                                            : juce::Colours::red);
                });
            });
        };
        addAndMakeVisible(aiValidateButton);

        aiStatusLabel.setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        aiStatusLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(aiStatusLabel);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int rowH = 32;
        const int labelW = 180;
        const int headerH = 28;

        aiHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);

        auto row = bounds.removeFromTop(rowH);
        aiApiKeyLabel.setBounds(row.removeFromLeft(labelW));
        aiApiKeyEditor.setBounds(row.reduced(0, 4));
        bounds.removeFromTop(4);

        row = bounds.removeFromTop(rowH);
        aiValidateButton.setBounds(row.removeFromLeft(80).reduced(0, 4));
        row.removeFromLeft(8);
        aiStatusLabel.setBounds(row);
    }

    void loadSettings(Config& config) {
        aiApiKeyEditor.setText(juce::String(config.getOpenAIApiKey()), juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        config.setOpenAIApiKey(aiApiKeyEditor.getText().toStdString());
    }

  private:
    juce::Label aiHeader;
    juce::Label aiApiKeyLabel;
    juce::TextEditor aiApiKeyEditor;
    juce::TextButton aiValidateButton;
    juce::Label aiStatusLabel;
};

// ---- Shortcuts tab (read-only) --------------------------------------------

class ShortcutsPage : public juce::Component {
  public:
    ShortcutsPage() {
        setupSectionHeader(*this, shortcutsHeader, "Keyboard Shortcuts");
#if JUCE_MAC
        setupShortcutLabel(*this, addTrackShortcut, "Add Track", juce::String::fromUTF8("\u2318T"));
        setupShortcutLabel(*this, deleteTrackShortcut, "Delete Track",
                           juce::String::fromUTF8("\u232B"));
        setupShortcutLabel(*this, duplicateTrackShortcut, "Duplicate Track",
                           juce::String::fromUTF8("\u2318D"));
#else
        setupShortcutLabel(*this, addTrackShortcut, "Add Track", "Ctrl+T");
        setupShortcutLabel(*this, deleteTrackShortcut, "Delete Track", "Delete");
        setupShortcutLabel(*this, duplicateTrackShortcut, "Duplicate Track", "Ctrl+D");
#endif
        setupShortcutLabel(*this, muteTrackShortcut, "Mute Track", "M");
        setupShortcutLabel(*this, soloTrackShortcut, "Solo Track", "S");
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int rowH = 32;
        const int headerH = 28;

        shortcutsHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);

        addTrackShortcut.setBounds(bounds.removeFromTop(rowH));
        bounds.removeFromTop(4);
        deleteTrackShortcut.setBounds(bounds.removeFromTop(rowH));
        bounds.removeFromTop(4);
        duplicateTrackShortcut.setBounds(bounds.removeFromTop(rowH));
        bounds.removeFromTop(4);
        muteTrackShortcut.setBounds(bounds.removeFromTop(rowH));
        bounds.removeFromTop(4);
        soloTrackShortcut.setBounds(bounds.removeFromTop(rowH));
    }

    void loadSettings(Config& /*config*/) {}
    void applySettings(Config& /*config*/) {}

  private:
    juce::Label shortcutsHeader;
    juce::Label addTrackShortcut, deleteTrackShortcut, duplicateTrackShortcut;
    juce::Label muteTrackShortcut, soloTrackShortcut;
};

// ---------------------------------------------------------------------------
// PreferencesDialog
// ---------------------------------------------------------------------------

PreferencesDialog::PreferencesDialog() {
    generalPage = std::make_unique<GeneralPage>();
    uiPage = std::make_unique<UIPage>();
    renderingPage = std::make_unique<RenderingPage>();
    aiPage = std::make_unique<AIPage>();
    shortcutsPage = std::make_unique<ShortcutsPage>();

    auto tabBg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    tabbedComponent.addTab("General", tabBg, generalPage.get(), false);
    tabbedComponent.addTab("UI", tabBg, uiPage.get(), false);
    tabbedComponent.addTab("Rendering", tabBg, renderingPage.get(), false);
    tabbedComponent.addTab("AI", tabBg, aiPage.get(), false);
    tabbedComponent.addTab("Shortcuts", tabBg, shortcutsPage.get(), false);
    addAndMakeVisible(tabbedComponent);

    okButton.setButtonText("OK");
    okButton.onClick = [this]() {
        applySettings();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    };
    addAndMakeVisible(okButton);

    cancelButton.setButtonText("Cancel");
    cancelButton.onClick = [this]() {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(cancelButton);

    applyButton.setButtonText("Apply");
    applyButton.onClick = [this]() { applySettings(); };
    addAndMakeVisible(applyButton);

    loadCurrentSettings();
    setSize(500, 580);
}

PreferencesDialog::~PreferencesDialog() = default;

void PreferencesDialog::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
}

void PreferencesDialog::resized() {
    const int buttonH = 28;
    const int buttonW = 80;
    const int buttonSpacing = 10;
    const int margin = 16;

    auto bounds = getLocalBounds();

    // Reserve bottom strip for the button row
    auto bottomStrip = bounds.removeFromBottom(buttonH + (margin * 2));
    tabbedComponent.setBounds(bounds);

    // Right-align buttons within the bottom strip
    bottomStrip.reduce(margin, margin);
    const int totalBtnW = (buttonW * 3) + (buttonSpacing * 2);
    bottomStrip.removeFromLeft(bottomStrip.getWidth() - totalBtnW);

    cancelButton.setBounds(bottomStrip.removeFromLeft(buttonW));
    bottomStrip.removeFromLeft(buttonSpacing);
    applyButton.setBounds(bottomStrip.removeFromLeft(buttonW));
    bottomStrip.removeFromLeft(buttonSpacing);
    okButton.setBounds(bottomStrip.removeFromLeft(buttonW));
}

void PreferencesDialog::loadCurrentSettings() {
    auto& config = Config::getInstance();
    generalPage->loadSettings(config);
    uiPage->loadSettings(config);
    renderingPage->loadSettings(config);
    aiPage->loadSettings(config);
    shortcutsPage->loadSettings(config);
}

void PreferencesDialog::applySettings() {
    auto& config = Config::getInstance();
    generalPage->applySettings(config);
    uiPage->applySettings(config);
    renderingPage->applySettings(config);
    aiPage->applySettings(config);
    shortcutsPage->applySettings(config);
    config.save();
}

void PreferencesDialog::showDialog(juce::Component* parent) {
    (void)parent;
    auto* dialog = new PreferencesDialog();

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Preferences";
    options.dialogBackgroundColour = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    options.content.setOwned(dialog);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    options.launchAsync();
}

}  // namespace magda
