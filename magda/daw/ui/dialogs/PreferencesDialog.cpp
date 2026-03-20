#include "PreferencesDialog.hpp"

#include "../../project/ProjectManager.hpp"
#include "../state/TimelineController.hpp"
#include "../state/TimelineEvents.hpp"
#include "../themes/DarkTheme.hpp"
#include "../themes/DialogLookAndFeel.hpp"
#include "../themes/FontManager.hpp"
#include "../windows/MainWindow.hpp"
#include "core/Config.hpp"

// ---------------------------------------------------------------------------
// Setup helpers — internal linkage, shared by all page classes
// ---------------------------------------------------------------------------
namespace {

void setupSlider(juce::Component& owner, juce::Slider& slider, juce::Label& label,
                 const juce::String& labelText, double min, double max, double interval,
                 const juce::String& suffix = "") {
    label.setText(labelText, juce::dontSendNotification);
    label.setFont(magda::FontManager::getInstance().getUIFont(12.0f));
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
    label.setFont(magda::FontManager::getInstance().getUIFont(12.0f));
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

// ---- General tab: Zoom, Timeline ------------------------------------------

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
        setupSlider(*this, timelineLengthSlider, timelineLengthLabel, "Default Length", 16.0,
                    4096.0, 1.0, " bars");
        timelineLengthSlider.setSkewFactorFromMidPoint(256.0);
        setupSlider(*this, viewDurationSlider, viewDurationLabel, "Default View", 4.0, 128.0, 1.0,
                    " bars");

        setupSectionHeader(*this, autoSaveHeader, "Auto-Save");
        setupToggle(*this, autoSaveToggle, "Enable auto-save");
        setupSlider(*this, autoSaveIntervalSlider, autoSaveIntervalLabel, "Interval", 10.0, 300.0,
                    10.0, " sec");
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int rowH = 32;
        const int labelW = 180;
        const int sliderH = 24;
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

        // Auto-Save
        autoSaveHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        autoSaveToggle.setBounds(bounds.removeFromTop(rowH).reduced(0, 4));
        bounds.removeFromTop(4);
        layoutSliderRow(bounds, autoSaveIntervalLabel, autoSaveIntervalSlider, rowH, labelW,
                        sliderH);
    }

    void loadSettings(Config& config) {
        zoomInSensitivitySlider.setValue(config.getZoomInSensitivity(), juce::dontSendNotification);
        zoomOutSensitivitySlider.setValue(config.getZoomOutSensitivity(),
                                          juce::dontSendNotification);
        zoomShiftSensitivitySlider.setValue(config.getZoomInSensitivityShift(),
                                            juce::dontSendNotification);
        timelineLengthSlider.setValue(config.getDefaultTimelineLengthBars(),
                                      juce::dontSendNotification);
        viewDurationSlider.setValue(config.getDefaultZoomViewBars(), juce::dontSendNotification);
        autoSaveToggle.setToggleState(config.getAutoSaveEnabled(), juce::dontSendNotification);
        autoSaveIntervalSlider.setValue(config.getAutoSaveIntervalSeconds(),
                                        juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        config.setZoomInSensitivity(zoomInSensitivitySlider.getValue());
        config.setZoomOutSensitivity(zoomOutSensitivitySlider.getValue());
        config.setZoomInSensitivityShift(zoomShiftSensitivitySlider.getValue());
        config.setZoomOutSensitivityShift(zoomShiftSensitivitySlider.getValue());
        config.setDefaultTimelineLengthBars(static_cast<int>(timelineLengthSlider.getValue()));
        config.setDefaultZoomViewBars(static_cast<int>(viewDurationSlider.getValue()));
        config.setAutoSaveEnabled(autoSaveToggle.getToggleState());
        config.setAutoSaveIntervalSeconds(static_cast<int>(autoSaveIntervalSlider.getValue()));
    }

  private:
    static void layoutSliderRow(juce::Rectangle<int>& bounds, juce::Label& label,
                                juce::Slider& slider, int rowH, int labelW, int sliderH) {
        auto row = bounds.removeFromTop(rowH);
        label.setBounds(row.removeFromLeft(labelW));
        slider.setBounds(row.reduced(0, (rowH - sliderH) / 2));
    }

    juce::Label zoomHeader, timelineHeader, autoSaveHeader;
    juce::Slider zoomInSensitivitySlider, zoomOutSensitivitySlider, zoomShiftSensitivitySlider;
    juce::Label zoomInLabel, zoomOutLabel, zoomShiftLabel;
    juce::Slider timelineLengthSlider, viewDurationSlider;
    juce::Label timelineLengthLabel, viewDurationLabel;
    juce::ToggleButton autoSaveToggle;
    juce::Slider autoSaveIntervalSlider;
    juce::Label autoSaveIntervalLabel;
};

// ---- UI tab: Panels, Behavior (incl. showTooltips), Layout ----------------

class UIPage : public juce::Component {
  public:
    UIPage() {
        setupSectionHeader(*this, panelsHeader, "Panels");
        setupToggle(*this, showLeftPanelToggle, "Expand Left Panel (Browser)");
        setupToggle(*this, showRightPanelToggle, "Expand Right Panel (Inspector)");
        setupToggle(*this, showBottomPanelToggle, "Expand Bottom Panel (Mixer)");

        setupSectionHeader(*this, layoutHeader, "Layout");
        setupToggle(*this, headersOnRightToggle, "Headers on the Right");

        setupSectionHeader(*this, behaviorHeader, "Behavior");
        setupToggle(*this, confirmTrackDeleteToggle, "Confirm before deleting tracks");
        setupToggle(*this, autoMonitorToggle, "Auto-monitor selected track");
        setupToggle(*this, showTooltipsToggle, "Show tooltips");
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

        // Layout
        layoutHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        headersOnRightToggle.setBounds(bounds.removeFromTop(toggleH + 8).reduced(0, 4));
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
    }

    void loadSettings(Config& config) {
        showLeftPanelToggle.setToggleState(!config.getLeftPanelCollapsed(),
                                           juce::dontSendNotification);
        showRightPanelToggle.setToggleState(!config.getRightPanelCollapsed(),
                                            juce::dontSendNotification);
        showBottomPanelToggle.setToggleState(!config.getBottomPanelCollapsed(),
                                             juce::dontSendNotification);
        headersOnRightToggle.setToggleState(config.getScrollbarOnLeft(),
                                            juce::dontSendNotification);
        confirmTrackDeleteToggle.setToggleState(config.getConfirmTrackDelete(),
                                                juce::dontSendNotification);
        autoMonitorToggle.setToggleState(config.getAutoMonitorSelectedTrack(),
                                         juce::dontSendNotification);
        showTooltipsToggle.setToggleState(config.getShowTooltips(), juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        config.setLeftPanelCollapsed(!showLeftPanelToggle.getToggleState());
        config.setRightPanelCollapsed(!showRightPanelToggle.getToggleState());
        config.setBottomPanelCollapsed(!showBottomPanelToggle.getToggleState());
        config.setScrollbarOnLeft(headersOnRightToggle.getToggleState());
        config.setConfirmTrackDelete(confirmTrackDeleteToggle.getToggleState());
        config.setAutoMonitorSelectedTrack(autoMonitorToggle.getToggleState());
        config.setShowTooltips(showTooltipsToggle.getToggleState());
    }

  private:
    juce::Label panelsHeader, layoutHeader, behaviorHeader;
    juce::ToggleButton showLeftPanelToggle, showRightPanelToggle, showBottomPanelToggle;
    juce::ToggleButton headersOnRightToggle;
    juce::ToggleButton confirmTrackDeleteToggle, autoMonitorToggle, showTooltipsToggle;
};

// ---- Colours tab: Track colour palette ------------------------------------

class ColoursPage : public juce::Component {
  public:
    static constexpr int MAX_PALETTE_SIZE = 16;

    ColoursPage() {
        setupSectionHeader(*this, coloursHeader, "Track Colour Palette");

        colourHeaderLabel.setText("Colour", juce::dontSendNotification);
        colourHeaderLabel.setFont(FontManager::getInstance().getUIFont(11.0f));
        colourHeaderLabel.setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        addAndMakeVisible(colourHeaderLabel);

        hexHeaderLabel.setText("Hex (RGB)", juce::dontSendNotification);
        hexHeaderLabel.setFont(FontManager::getInstance().getUIFont(11.0f));
        hexHeaderLabel.setColour(juce::Label::textColourId,
                                 DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        addAndMakeVisible(hexHeaderLabel);

        nameHeaderLabel.setText("Name", juce::dontSendNotification);
        nameHeaderLabel.setFont(FontManager::getInstance().getUIFont(11.0f));
        nameHeaderLabel.setColour(juce::Label::textColourId,
                                  DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        addAndMakeVisible(nameHeaderLabel);

        addColourButton.setButtonText("+ Add Colour");
        addColourButton.onClick = [this]() { addColourRow(0xFF808080, "New"); };
        addAndMakeVisible(addColourButton);

        // Clip colour mode
        setupSectionHeader(*this, clipColourHeader, "Clip Colours");

        clipColourModeLabel.setText("New clip colour", juce::dontSendNotification);
        clipColourModeLabel.setFont(FontManager::getInstance().getUIFont(12.0f));
        clipColourModeLabel.setColour(juce::Label::textColourId,
                                      DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        clipColourModeLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(clipColourModeLabel);

        clipColourModeCombo.addItem("Inherit from track", 1);
        clipColourModeCombo.addItem("Cycle through palette", 2);
        clipColourModeCombo.setColour(juce::ComboBox::backgroundColourId,
                                      DarkTheme::getColour(DarkTheme::SURFACE));
        clipColourModeCombo.setColour(juce::ComboBox::textColourId,
                                      DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        clipColourModeCombo.setColour(juce::ComboBox::outlineColourId,
                                      DarkTheme::getColour(DarkTheme::BORDER));
        addAndMakeVisible(clipColourModeCombo);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int headerH = 28;

        coloursHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);

        // Column headers
        {
            auto headerRow = bounds.removeFromTop(18);
            colourHeaderLabel.setBounds(headerRow.removeFromLeft(28));
            headerRow.removeFromLeft(8);
            hexHeaderLabel.setBounds(headerRow.removeFromLeft(80));
            headerRow.removeFromLeft(8);
            nameHeaderLabel.setBounds(headerRow.removeFromLeft(140));
        }
        bounds.removeFromTop(4);

        // Palette rows
        const int colourRowH = 26;
        for (size_t i = 0; i < colourSwatches_.size(); ++i) {
            auto row = bounds.removeFromTop(colourRowH);
            colourSwatches_[i]->setBounds(row.removeFromLeft(24).reduced(0, 2));
            row.removeFromLeft(12);
            hexEditors_[i]->setBounds(row.removeFromLeft(80).reduced(0, 2));
            row.removeFromLeft(8);
            nameEditors_[i]->setBounds(row.removeFromLeft(140).reduced(0, 2));
            row.removeFromLeft(8);
            deleteButtons_[i]->setBounds(row.removeFromLeft(20).reduced(0, 2));
            bounds.removeFromTop(2);
        }

        // Add button
        if (static_cast<int>(colourSwatches_.size()) < MAX_PALETTE_SIZE) {
            addColourButton.setVisible(true);
            bounds.removeFromTop(4);
            addColourButton.setBounds(bounds.removeFromTop(24).removeFromLeft(100));
        } else {
            addColourButton.setVisible(false);
        }

        // Clip colour mode
        bounds.removeFromTop(16);
        clipColourHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        {
            auto row = bounds.removeFromTop(32);
            clipColourModeLabel.setBounds(row.removeFromLeft(140));
            clipColourModeCombo.setBounds(row.reduced(0, 4));
        }
    }

    void loadSettings(Config& config) {
        clearColourRows();
        const auto& palette = config.getTrackColourPalette();
        for (const auto& entry : palette)
            addColourRow(entry.colour, entry.name);

        clipColourModeCombo.setSelectedId(config.getClipColourMode() + 1,
                                          juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        std::vector<Config::TrackColourEntry> palette;
        for (size_t i = 0; i < hexEditors_.size(); ++i) {
            Config::TrackColourEntry entry;
            entry.colour =
                static_cast<uint32_t>(hexEditors_[i]->getText().getHexValue64() | 0xFF000000ULL);
            entry.name = nameEditors_[i]->getText().toStdString();
            if (entry.name.empty())
                entry.name = "Colour " + std::to_string(i + 1);
            palette.push_back(entry);
        }
        config.setTrackColourPalette(palette);
        config.setClipColourMode(clipColourModeCombo.getSelectedId() - 1);
    }

  private:
    void addColourRow(uint32_t colour, const std::string& name) {
        if (static_cast<int>(colourSwatches_.size()) >= MAX_PALETTE_SIZE)
            return;

        auto idx = colourSwatches_.size();

        // Swatch (colour preview — painted in our paint() override)
        auto swatch = std::make_unique<juce::Component>();
        swatch->setPaintingIsUnclipped(true);
        addAndMakeVisible(*swatch);
        colourSwatches_.push_back(std::move(swatch));
        swatchColours_.push_back(juce::Colour(colour));

        // Hex editor (RGB only, no alpha — we force 0xFF)
        auto hex = std::make_unique<juce::TextEditor>();
        hex->setFont(FontManager::getInstance().getUIFont(12.0f));
        hex->setColour(juce::TextEditor::backgroundColourId,
                       DarkTheme::getColour(DarkTheme::SURFACE));
        hex->setColour(juce::TextEditor::textColourId,
                       DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        hex->setColour(juce::TextEditor::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
        hex->setInputRestrictions(6, "0123456789ABCDEFabcdef");
        hex->setText(juce::String::toHexString(static_cast<int>(colour & 0x00FFFFFF))
                         .paddedLeft('0', 6)
                         .toUpperCase(),
                     juce::dontSendNotification);
        hex->onTextChange = [this, idx]() { updateSwatchColour(idx); };
        addAndMakeVisible(*hex);
        hexEditors_.push_back(std::move(hex));

        // Name editor
        auto nameEd = std::make_unique<juce::TextEditor>();
        nameEd->setFont(FontManager::getInstance().getUIFont(12.0f));
        nameEd->setColour(juce::TextEditor::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
        nameEd->setColour(juce::TextEditor::textColourId,
                          DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        nameEd->setColour(juce::TextEditor::outlineColourId,
                          DarkTheme::getColour(DarkTheme::BORDER));
        nameEd->setText(juce::String(name), juce::dontSendNotification);
        addAndMakeVisible(*nameEd);
        nameEditors_.push_back(std::move(nameEd));

        // Delete button
        auto del = std::make_unique<juce::TextButton>("x");
        del->onClick = [this, idx]() { removeColourRow(idx); };
        addAndMakeVisible(*del);
        deleteButtons_.push_back(std::move(del));

        resized();
        repaint();
    }

    void removeColourRow(size_t idx) {
        if (idx >= colourSwatches_.size())
            return;
        removeChildComponent(colourSwatches_[idx].get());
        removeChildComponent(hexEditors_[idx].get());
        removeChildComponent(nameEditors_[idx].get());
        removeChildComponent(deleteButtons_[idx].get());

        colourSwatches_.erase(colourSwatches_.begin() + static_cast<ptrdiff_t>(idx));
        swatchColours_.erase(swatchColours_.begin() + static_cast<ptrdiff_t>(idx));
        hexEditors_.erase(hexEditors_.begin() + static_cast<ptrdiff_t>(idx));
        nameEditors_.erase(nameEditors_.begin() + static_cast<ptrdiff_t>(idx));
        deleteButtons_.erase(deleteButtons_.begin() + static_cast<ptrdiff_t>(idx));

        // Rebind callbacks with correct indices
        for (size_t i = 0; i < deleteButtons_.size(); ++i)
            deleteButtons_[i]->onClick = [this, i]() { removeColourRow(i); };
        for (size_t i = 0; i < hexEditors_.size(); ++i)
            hexEditors_[i]->onTextChange = [this, i]() { updateSwatchColour(i); };

        resized();
        repaint();
    }

    void clearColourRows() {
        for (auto& s : colourSwatches_)
            removeChildComponent(s.get());
        for (auto& h : hexEditors_)
            removeChildComponent(h.get());
        for (auto& n : nameEditors_)
            removeChildComponent(n.get());
        for (auto& d : deleteButtons_)
            removeChildComponent(d.get());
        colourSwatches_.clear();
        swatchColours_.clear();
        hexEditors_.clear();
        nameEditors_.clear();
        deleteButtons_.clear();
    }

    void updateSwatchColour(size_t idx) {
        if (idx >= hexEditors_.size())
            return;
        auto hexVal = hexEditors_[idx]->getText().getHexValue64();
        swatchColours_[idx] = juce::Colour(static_cast<uint32_t>(hexVal | 0xFF000000ULL));
        repaint();
    }

    void paint(juce::Graphics& g) override {
        for (size_t i = 0; i < colourSwatches_.size(); ++i) {
            auto area = colourSwatches_[i]->getBounds().toFloat().reduced(1.0f);
            g.setColour(swatchColours_[i]);
            g.fillRoundedRectangle(area, 3.0f);
            g.setColour(swatchColours_[i].brighter(0.3f));
            g.drawRoundedRectangle(area, 3.0f, 1.0f);
        }
    }

    juce::Label coloursHeader;
    juce::Label colourHeaderLabel, hexHeaderLabel, nameHeaderLabel;
    std::vector<std::unique_ptr<juce::Component>> colourSwatches_;
    std::vector<juce::Colour> swatchColours_;
    std::vector<std::unique_ptr<juce::TextEditor>> hexEditors_;
    std::vector<std::unique_ptr<juce::TextEditor>> nameEditors_;
    std::vector<std::unique_ptr<juce::TextButton>> deleteButtons_;
    juce::TextButton addColourButton;

    // Clip colour mode
    juce::Label clipColourHeader;
    juce::Label clipColourModeLabel;
    juce::ComboBox clipColourModeCombo;
};

// ---- Rendering tab --------------------------------------------------------

class RenderingPage : public juce::Component {
  public:
    RenderingPage() {
        // --- Output Folder ---
        setupSectionHeader(*this, renderHeader, "Output");

        renderFolderLabel.setText("Render Output Folder", juce::dontSendNotification);
        renderFolderLabel.setFont(FontManager::getInstance().getUIFont(12.0f));
        renderFolderLabel.setColour(juce::Label::textColourId,
                                    DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        renderFolderLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(renderFolderLabel);

        renderFolderValue.setText("Default (beside source file)", juce::dontSendNotification);
        renderFolderValue.setFont(FontManager::getInstance().getUIFont(12.0f));
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
                                              renderFolderPath_ =
                                                  result.getFullPathName().toStdString();
                                              renderFolderValue.setText(result.getFullPathName(),
                                                                        juce::dontSendNotification);
                                          }
                                      });
        };
        addAndMakeVisible(renderFolderBrowseButton);

        renderFolderClearButton.setButtonText("Clear");
        renderFolderClearButton.onClick = [this]() {
            renderFolderPath_.clear();
            renderFolderValue.setText("Default (beside source file)", juce::dontSendNotification);
        };
        addAndMakeVisible(renderFolderClearButton);

        // --- Format ---
        setupSectionHeader(*this, formatHeader, "Format");

        setupComboLabel(sampleRateLabel, "Sample Rate");
        sampleRateCombo.addItem("44100 Hz", 1);
        sampleRateCombo.addItem("48000 Hz", 2);
        sampleRateCombo.addItem("96000 Hz", 3);
        sampleRateCombo.addItem("192000 Hz", 4);
        styleCombo(sampleRateCombo);
        addAndMakeVisible(sampleRateCombo);

        setupComboLabel(bitDepthLabel, "Export Bit Depth");
        bitDepthCombo.addItem("16-bit", 1);
        bitDepthCombo.addItem("24-bit", 2);
        bitDepthCombo.addItem("32-bit float", 3);
        styleCombo(bitDepthCombo);
        addAndMakeVisible(bitDepthCombo);

        setupComboLabel(bounceBitDepthLabel, "Bounce Bit Depth");
        bounceBitDepthCombo.addItem("16-bit", 1);
        bounceBitDepthCombo.addItem("24-bit", 2);
        bounceBitDepthCombo.addItem("32-bit float", 3);
        styleCombo(bounceBitDepthCombo);
        addAndMakeVisible(bounceBitDepthCombo);

        // --- File Naming ---
        setupSectionHeader(*this, namingHeader, "File Naming");

        setupComboLabel(patternLabel, "Export Pattern");
        patternEditor.setFont(FontManager::getInstance().getUIFont(12.0f));
        patternEditor.setColour(juce::TextEditor::backgroundColourId,
                                DarkTheme::getColour(DarkTheme::SURFACE));
        patternEditor.setColour(juce::TextEditor::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        patternEditor.setColour(juce::TextEditor::outlineColourId,
                                DarkTheme::getColour(DarkTheme::BORDER));
        addAndMakeVisible(patternEditor);

        setupComboLabel(bouncePatternLabel, "Bounce Pattern");
        bouncePatternEditor.setFont(FontManager::getInstance().getUIFont(12.0f));
        bouncePatternEditor.setColour(juce::TextEditor::backgroundColourId,
                                      DarkTheme::getColour(DarkTheme::SURFACE));
        bouncePatternEditor.setColour(juce::TextEditor::textColourId,
                                      DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        bouncePatternEditor.setColour(juce::TextEditor::outlineColourId,
                                      DarkTheme::getColour(DarkTheme::BORDER));
        addAndMakeVisible(bouncePatternEditor);

        patternHint.setText("Tokens: <clip-name> <track-name> <project-name> <date-time>",
                            juce::dontSendNotification);
        patternHint.setFont(FontManager::getInstance().getUIFont(10.0f));
        patternHint.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_DIM));
        patternHint.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(patternHint);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(16);
        const int rowH = 32;
        const int headerH = 28;
        const int labelW = 140;
        const int secGap = 12;

        // Output folder
        renderHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        renderFolderLabel.setBounds(bounds.removeFromTop(rowH));
        bounds.removeFromTop(4);
        {
            auto row = bounds.removeFromTop(rowH);
            auto buttonsArea = row.removeFromRight(140);
            renderFolderValue.setBounds(row);
            renderFolderClearButton.setBounds(buttonsArea.removeFromRight(60).reduced(0, 2));
            buttonsArea.removeFromRight(4);
            renderFolderBrowseButton.setBounds(buttonsArea.reduced(0, 2));
        }
        bounds.removeFromTop(secGap);

        // Format
        formatHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        layoutComboRow(bounds, sampleRateLabel, sampleRateCombo, rowH, labelW);
        bounds.removeFromTop(4);
        layoutComboRow(bounds, bitDepthLabel, bitDepthCombo, rowH, labelW);
        bounds.removeFromTop(4);
        layoutComboRow(bounds, bounceBitDepthLabel, bounceBitDepthCombo, rowH, labelW);
        bounds.removeFromTop(secGap);

        // File naming
        namingHeader.setBounds(bounds.removeFromTop(headerH));
        bounds.removeFromTop(4);
        {
            auto row = bounds.removeFromTop(rowH);
            patternLabel.setBounds(row.removeFromLeft(labelW));
            patternEditor.setBounds(row.reduced(0, 4));
        }
        bounds.removeFromTop(4);
        {
            auto row = bounds.removeFromTop(rowH);
            bouncePatternLabel.setBounds(row.removeFromLeft(labelW));
            bouncePatternEditor.setBounds(row.reduced(0, 4));
        }
        bounds.removeFromTop(2);
        patternHint.setBounds(bounds.removeFromTop(18).withTrimmedLeft(labelW));
    }

    void loadSettings(Config& config) {
        // Folder
        renderFolderPath_ = config.getRenderFolder();
        if (renderFolderPath_.empty()) {
            renderFolderValue.setText("Default (beside source file)", juce::dontSendNotification);
        } else {
            renderFolderValue.setText(juce::String(renderFolderPath_), juce::dontSendNotification);
        }

        // Sample rate
        double sr = config.getRenderSampleRate();
        if (sr >= 192000.0)
            sampleRateCombo.setSelectedId(4, juce::dontSendNotification);
        else if (sr >= 96000.0)
            sampleRateCombo.setSelectedId(3, juce::dontSendNotification);
        else if (sr >= 48000.0)
            sampleRateCombo.setSelectedId(2, juce::dontSendNotification);
        else
            sampleRateCombo.setSelectedId(1, juce::dontSendNotification);

        // Bit depth
        int bd = config.getRenderBitDepth();
        if (bd >= 32)
            bitDepthCombo.setSelectedId(3, juce::dontSendNotification);
        else if (bd >= 24)
            bitDepthCombo.setSelectedId(2, juce::dontSendNotification);
        else
            bitDepthCombo.setSelectedId(1, juce::dontSendNotification);

        // Bounce bit depth
        int bbd = config.getBounceBitDepth();
        if (bbd >= 32)
            bounceBitDepthCombo.setSelectedId(3, juce::dontSendNotification);
        else if (bbd >= 24)
            bounceBitDepthCombo.setSelectedId(2, juce::dontSendNotification);
        else
            bounceBitDepthCombo.setSelectedId(1, juce::dontSendNotification);

        // File patterns
        patternEditor.setText(juce::String(config.getRenderFilePattern()),
                              juce::dontSendNotification);
        bouncePatternEditor.setText(juce::String(config.getBounceFilePattern()),
                                    juce::dontSendNotification);
    }

    void applySettings(Config& config) {
        config.setRenderFolder(renderFolderPath_);

        static constexpr double sampleRates[] = {44100.0, 48000.0, 96000.0, 192000.0};
        int srIdx = sampleRateCombo.getSelectedId() - 1;
        if (srIdx >= 0 && srIdx < 4)
            config.setRenderSampleRate(sampleRates[srIdx]);

        static constexpr int bitDepths[] = {16, 24, 32};
        int bdIdx = bitDepthCombo.getSelectedId() - 1;
        if (bdIdx >= 0 && bdIdx < 3)
            config.setRenderBitDepth(bitDepths[bdIdx]);

        auto pattern = patternEditor.getText().toStdString();
        if (pattern.empty())
            pattern = "<project-name>_<date-time>";
        config.setRenderFilePattern(pattern);

        auto bouncePattern = bouncePatternEditor.getText().toStdString();
        if (bouncePattern.empty())
            bouncePattern = "<clip-name>_<date-time>";
        config.setBounceFilePattern(bouncePattern);

        int bbdIdx = bounceBitDepthCombo.getSelectedId() - 1;
        if (bbdIdx >= 0 && bbdIdx < 3)
            config.setBounceBitDepth(bitDepths[bbdIdx]);
    }

  private:
    void setupComboLabel(juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(FontManager::getInstance().getUIFont(12.0f));
        label.setColour(juce::Label::textColourId, DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);
    }

    void styleCombo(juce::ComboBox& combo) {
        combo.setColour(juce::ComboBox::backgroundColourId,
                        DarkTheme::getColour(DarkTheme::SURFACE));
        combo.setColour(juce::ComboBox::textColourId,
                        DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        combo.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    }

    static void layoutComboRow(juce::Rectangle<int>& bounds, juce::Label& label,
                               juce::ComboBox& combo, int rowH, int labelW) {
        auto row = bounds.removeFromTop(rowH);
        label.setBounds(row.removeFromLeft(labelW));
        combo.setBounds(row.reduced(0, 4));
    }

    juce::Label renderHeader, formatHeader, namingHeader;
    juce::Label renderFolderLabel;
    juce::Label renderFolderValue;
    juce::TextButton renderFolderBrowseButton;
    juce::TextButton renderFolderClearButton;
    std::string renderFolderPath_;

    juce::Label sampleRateLabel, bitDepthLabel, bounceBitDepthLabel;
    juce::ComboBox sampleRateCombo, bitDepthCombo, bounceBitDepthCombo;

    juce::Label patternLabel, bouncePatternLabel, patternHint;
    juce::TextEditor patternEditor, bouncePatternEditor;

    std::unique_ptr<juce::FileChooser> fileChooser_;
};

// ---- AI tab ---------------------------------------------------------------

class AIPage : public juce::Component {
  public:
    AIPage() {
        setupSectionHeader(*this, aiHeader, "AI Assistant");

        aiApiKeyLabel.setText("OpenAI API Key", juce::dontSendNotification);
        aiApiKeyLabel.setFont(FontManager::getInstance().getUIFont(12.0f));
        aiApiKeyLabel.setColour(juce::Label::textColourId,
                                DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
        aiApiKeyLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(aiApiKeyLabel);

        aiApiKeyEditor.setFont(FontManager::getInstance().getUIFont(12.0f));
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

        aiStatusLabel.setFont(FontManager::getInstance().getUIFont(12.0f));
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
    setLookAndFeel(&daw::ui::DialogLookAndFeel::getInstance());

    generalPage = std::make_unique<GeneralPage>();
    uiPage = std::make_unique<UIPage>();
    coloursPage = std::make_unique<ColoursPage>();
    renderingPage = std::make_unique<RenderingPage>();
    aiPage = std::make_unique<AIPage>();
    shortcutsPage = std::make_unique<ShortcutsPage>();

    auto tabBg = DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND);
    tabbedComponent.addTab("General", tabBg, generalPage.get(), false);
    tabbedComponent.addTab("UI", tabBg, uiPage.get(), false);
    tabbedComponent.addTab("Colours", tabBg, coloursPage.get(), false);
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
    setSize(500, 650);
}

PreferencesDialog::~PreferencesDialog() {
    setLookAndFeel(nullptr);
}

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
    coloursPage->loadSettings(config);
    renderingPage->loadSettings(config);
    aiPage->loadSettings(config);
    shortcutsPage->loadSettings(config);
}

void PreferencesDialog::applySettings() {
    auto& config = Config::getInstance();
    generalPage->applySettings(config);
    uiPage->applySettings(config);
    coloursPage->applySettings(config);
    renderingPage->applySettings(config);
    aiPage->applySettings(config);
    shortcutsPage->applySettings(config);
    config.save();

    // Apply auto-save settings
    ProjectManager::getInstance().setAutoSaveEnabled(config.getAutoSaveEnabled(),
                                                     config.getAutoSaveIntervalSeconds());

    // Apply timeline length to live session
    if (auto* tc = TimelineController::getCurrent()) {
        double newLength = tc->getState().tempo.barsToTime(config.getDefaultTimelineLengthBars());
        tc->dispatch(SetTimelineLengthEvent{newLength});
    }

    // Apply panel visibility and layout to live session
    for (int i = juce::TopLevelWindow::getNumTopLevelWindows(); --i >= 0;) {
        if (auto* mw = dynamic_cast<MainWindow*>(juce::TopLevelWindow::getTopLevelWindow(i))) {
            mw->applyPanelVisibilityFromConfig();
            mw->applyLayoutFromConfig();
            break;
        }
    }
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
