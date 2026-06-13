#include "modulation/LFOCurveEditorWindow.hpp"

#include "core/PresetManager.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SmallButtonLookAndFeel.hpp"
#include "ui/themes/SmallComboBoxLookAndFeel.hpp"

namespace magda::daw::ui {

// ============================================================================
// LFOCurveEditorContent
// ============================================================================

LFOCurveEditorContent::LFOCurveEditorContent(magda::ModInfo* modInfo,
                                             std::function<void()> onWaveformChanged,
                                             std::function<void()> onDragPreview)
    : modInfo_(modInfo) {
    // Configure the curve editor
    curveEditor_.setName("popupLFO");
    curveEditor_.setModInfo(modInfo);
    curveEditor_.setCurveColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    curveEditor_.onWaveformChanged = std::move(onWaveformChanged);
    curveEditor_.onDragPreview = std::move(onDragPreview);
    addAndMakeVisible(curveEditor_);

    setupControls();
    updateControlsFromModInfo();
}

void LFOCurveEditorContent::setupControls() {
    // Sync toggle button
    syncToggle_.setButtonText("Free");
    syncToggle_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    syncToggle_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    syncToggle_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    syncToggle_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    syncToggle_.setClickingTogglesState(true);
    syncToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    syncToggle_.onClick = [this]() {
        bool synced = syncToggle_.getToggleState();
        syncToggle_.setButtonText(synced ? "Sync" : "Free");
        rateSlider_.setVisible(!synced);
        syncDivisionCombo_.setVisible(synced);
        if (modInfo_) {
            modInfo_->tempoSync = synced;
        }
        if (onTempoSyncChanged) {
            onTempoSyncChanged(synced);
        }
    };
    addAndMakeVisible(syncToggle_);

    // Rate slider (Hz)
    rateSlider_.setRange(0.01, 20.0, 0.01);
    rateSlider_.setValue(1.0, juce::dontSendNotification);
    rateSlider_.setFont(FontManager::getInstance().getUIFont(9.0f));
    rateSlider_.setShowFillIndicator(false);
    rateSlider_.onValueChanged = [this](double value) {
        if (modInfo_) {
            modInfo_->rate = static_cast<float>(value);
        }
        if (onRateChanged) {
            onRateChanged(static_cast<float>(value));
        }
    };
    addAndMakeVisible(rateSlider_);

    // Sync division combo
    syncDivisionCombo_.addItem("1 Bar", static_cast<int>(magda::SyncDivision::Whole) + 100);
    syncDivisionCombo_.addItem("1/2", static_cast<int>(magda::SyncDivision::Half) + 100);
    syncDivisionCombo_.addItem("1/4", static_cast<int>(magda::SyncDivision::Quarter) + 100);
    syncDivisionCombo_.addItem("1/8", static_cast<int>(magda::SyncDivision::Eighth) + 100);
    syncDivisionCombo_.addItem("1/16", static_cast<int>(magda::SyncDivision::Sixteenth) + 100);
    syncDivisionCombo_.addItem("1/32", static_cast<int>(magda::SyncDivision::ThirtySecond) + 100);
    syncDivisionCombo_.setSelectedId(static_cast<int>(magda::SyncDivision::Quarter) + 100,
                                     juce::dontSendNotification);
    syncDivisionCombo_.setColour(juce::ComboBox::backgroundColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    syncDivisionCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    syncDivisionCombo_.setColour(juce::ComboBox::outlineColourId,
                                 DarkTheme::getColour(DarkTheme::BORDER));
    syncDivisionCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    syncDivisionCombo_.onChange = [this]() {
        int id = syncDivisionCombo_.getSelectedId();
        if (id >= 100) {
            auto division = static_cast<magda::SyncDivision>(id - 100);
            if (modInfo_) {
                modInfo_->syncDivision = division;
            }
            if (onSyncDivisionChanged) {
                onSyncDivisionChanged(division);
            }
        }
    };
    addChildComponent(syncDivisionCombo_);

    // Loop/One-shot toggle
    loopOneShotToggle_.setButtonText("Loop");
    loopOneShotToggle_.setColour(juce::TextButton::buttonColourId,
                                 DarkTheme::getColour(DarkTheme::SURFACE));
    loopOneShotToggle_.setColour(juce::TextButton::buttonOnColourId,
                                 DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    loopOneShotToggle_.setColour(juce::TextButton::textColourOffId,
                                 DarkTheme::getSecondaryTextColour());
    loopOneShotToggle_.setColour(juce::TextButton::textColourOnId,
                                 DarkTheme::getColour(DarkTheme::BACKGROUND));
    loopOneShotToggle_.setClickingTogglesState(true);
    loopOneShotToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    loopOneShotToggle_.onClick = [this]() {
        bool oneShot = loopOneShotToggle_.getToggleState();
        loopOneShotToggle_.setButtonText(oneShot ? "1-Shot" : "Loop");
        if (modInfo_) {
            modInfo_->oneShot = oneShot;
        }
        if (onOneShotChanged) {
            onOneShotChanged(oneShot);
        }
    };
    addAndMakeVisible(loopOneShotToggle_);

    // MSEG toggle (loop region)
    msegToggle_.setButtonText("MSEG");
    msegToggle_.setColour(juce::TextButton::buttonColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    msegToggle_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    msegToggle_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    msegToggle_.setColour(juce::TextButton::textColourOnId,
                          DarkTheme::getColour(DarkTheme::BACKGROUND));
    msegToggle_.setClickingTogglesState(true);
    msegToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    msegToggle_.onClick = [this]() {
        bool useLoop = msegToggle_.getToggleState();
        curveEditor_.setShowLoopRegion(useLoop);
        if (modInfo_) {
            modInfo_->useLoopRegion = useLoop;
        }
        if (onLoopRegionChanged) {
            onLoopRegionChanged(useLoop);
        }
    };
    addAndMakeVisible(msegToggle_);

    // Preset selector
    presetCombo_.setTextWhenNothingSelected("Preset");
    presetCombo_.setColour(juce::ComboBox::backgroundColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
    presetCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    presetCombo_.setColour(juce::ComboBox::outlineColourId,
                           DarkTheme::getColour(DarkTheme::BORDER));
    presetCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    presetCombo_.onChange = [this]() {
        int id = presetCombo_.getSelectedId();
        if (id >= USER_PRESET_ID_BASE) {
            const int index = id - USER_PRESET_ID_BASE;
            if (index >= 0 && index < userCurvePresets_.size()) {
                std::vector<magda::CurvePointData> points;
                auto& presetManager = magda::PresetManager::getInstance();
                if (!presetManager.loadCurvePreset(userCurvePresets_[index], points)) {
                    showPresetError("Load Curve Preset Failed", presetManager.getLastError());
                    return;
                }

                currentUserCurvePreset_ = userCurvePresets_[index];
                curveEditor_.loadCurvePoints(points);
            }
        } else if (id > 0) {
            currentUserCurvePreset_.clear();
            auto preset = static_cast<magda::CurvePreset>(id - 1);
            curveEditor_.loadPreset(preset);
        }
    };
    rebuildPresetCombo();
    addAndMakeVisible(presetCombo_);

    // Save preset button
    savePresetButton_ = std::make_unique<magda::SvgButton>("Save Preset", BinaryData::save_svg,
                                                           BinaryData::save_svgSize);
    savePresetButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    savePresetButton_->setHoverColor(DarkTheme::getTextColour());
    savePresetButton_->setTooltip("Save curve preset");
    savePresetButton_->onClick = [this]() { showSaveCurvePresetDialog(); };
    addAndMakeVisible(savePresetButton_.get());

    // Reset button
    resetButton_ = std::make_unique<magda::SvgButton>("Reset Curve", BinaryData::refresh_svg,
                                                      BinaryData::refresh_svgSize);
    resetButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    resetButton_->setHoverColor(DarkTheme::getTextColour());
    resetButton_->setTooltip("Reset curve");
    resetButton_->onClick = [this]() {
        currentUserCurvePreset_.clear();
        curveEditor_.loadPreset(magda::CurvePreset::Triangle);
        presetCombo_.setSelectedId(static_cast<int>(magda::CurvePreset::Triangle) + 1,
                                   juce::dontSendNotification);
    };
    addAndMakeVisible(resetButton_.get());

    // Grid label
    gridLabel_.setText("Grid:", juce::dontSendNotification);
    gridLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    gridLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(gridLabel_);

    // Grid X divisions
    gridXCombo_.addItem("2", 2);
    gridXCombo_.addItem("4", 4);
    gridXCombo_.addItem("8", 8);
    gridXCombo_.addItem("16", 16);
    gridXCombo_.addItem("32", 32);
    gridXCombo_.setSelectedId(4, juce::dontSendNotification);
    gridXCombo_.setColour(juce::ComboBox::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    gridXCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    gridXCombo_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    gridXCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    gridXCombo_.onChange = [this]() {
        curveEditor_.setGridDivisionsX(gridXCombo_.getSelectedId());
    };
    addAndMakeVisible(gridXCombo_);

    // Grid Y divisions
    gridYCombo_.addItem("2", 2);
    gridYCombo_.addItem("4", 4);
    gridYCombo_.addItem("8", 8);
    gridYCombo_.addItem("16", 16);
    gridYCombo_.setSelectedId(4, juce::dontSendNotification);
    gridYCombo_.setColour(juce::ComboBox::backgroundColourId,
                          DarkTheme::getColour(DarkTheme::SURFACE));
    gridYCombo_.setColour(juce::ComboBox::textColourId, DarkTheme::getTextColour());
    gridYCombo_.setColour(juce::ComboBox::outlineColourId, DarkTheme::getColour(DarkTheme::BORDER));
    gridYCombo_.setLookAndFeel(&SmallComboBoxLookAndFeel::getInstance());
    gridYCombo_.onChange = [this]() {
        curveEditor_.setGridDivisionsY(gridYCombo_.getSelectedId());
    };
    addAndMakeVisible(gridYCombo_);

    // Snap X toggle
    snapXToggle_.setButtonText("X");
    snapXToggle_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
    snapXToggle_.setColour(juce::TextButton::buttonOnColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    snapXToggle_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    snapXToggle_.setColour(juce::TextButton::textColourOnId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND));
    snapXToggle_.setClickingTogglesState(true);
    snapXToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    snapXToggle_.onClick = [this]() { curveEditor_.setSnapX(snapXToggle_.getToggleState()); };
    addAndMakeVisible(snapXToggle_);

    // Snap Y toggle
    snapYToggle_.setButtonText("Y");
    snapYToggle_.setColour(juce::TextButton::buttonColourId,
                           DarkTheme::getColour(DarkTheme::SURFACE));
    snapYToggle_.setColour(juce::TextButton::buttonOnColourId,
                           DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    snapYToggle_.setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    snapYToggle_.setColour(juce::TextButton::textColourOnId,
                           DarkTheme::getColour(DarkTheme::BACKGROUND));
    snapYToggle_.setClickingTogglesState(true);
    snapYToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    snapYToggle_.onClick = [this]() { curveEditor_.setSnapY(snapYToggle_.getToggleState()); };
    addAndMakeVisible(snapYToggle_);

    // Loop-marker snap toggle (snaps loop points to the X grid)
    loopSnapToggle_.setButtonText("Lp");
    loopSnapToggle_.setColour(juce::TextButton::buttonColourId,
                              DarkTheme::getColour(DarkTheme::SURFACE));
    loopSnapToggle_.setColour(juce::TextButton::buttonOnColourId,
                              DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
    loopSnapToggle_.setColour(juce::TextButton::textColourOffId,
                              DarkTheme::getSecondaryTextColour());
    loopSnapToggle_.setColour(juce::TextButton::textColourOnId,
                              DarkTheme::getColour(DarkTheme::BACKGROUND));
    loopSnapToggle_.setClickingTogglesState(true);
    loopSnapToggle_.setToggleState(curveEditor_.getSnapLoop(), juce::dontSendNotification);
    loopSnapToggle_.setLookAndFeel(&SmallButtonLookAndFeel::getInstance());
    loopSnapToggle_.onClick = [this]() {
        curveEditor_.setSnapLoop(loopSnapToggle_.getToggleState());
    };
    addAndMakeVisible(loopSnapToggle_);
}

void LFOCurveEditorContent::updateControlsFromModInfo() {
    if (!modInfo_)
        return;

    // Sync settings
    syncToggle_.setToggleState(modInfo_->tempoSync, juce::dontSendNotification);
    syncToggle_.setButtonText(modInfo_->tempoSync ? "Sync" : "Free");
    rateSlider_.setValue(modInfo_->rate, juce::dontSendNotification);
    rateSlider_.setVisible(!modInfo_->tempoSync);
    syncDivisionCombo_.setSelectedId(static_cast<int>(modInfo_->syncDivision) + 100,
                                     juce::dontSendNotification);
    syncDivisionCombo_.setVisible(modInfo_->tempoSync);

    // Loop/one-shot
    loopOneShotToggle_.setToggleState(modInfo_->oneShot, juce::dontSendNotification);
    loopOneShotToggle_.setButtonText(modInfo_->oneShot ? "1-Shot" : "Loop");

    // MSEG
    msegToggle_.setToggleState(modInfo_->useLoopRegion, juce::dontSendNotification);
    curveEditor_.setShowLoopRegion(modInfo_->useLoopRegion);
}

void LFOCurveEditorContent::rebuildPresetCombo(const juce::String& selectedUserPreset) {
    presetCombo_.clear(juce::dontSendNotification);

    presetCombo_.addSectionHeading("Factory");
    presetCombo_.addItem("Triangle", static_cast<int>(magda::CurvePreset::Triangle) + 1);
    presetCombo_.addItem("Sine", static_cast<int>(magda::CurvePreset::Sine) + 1);
    presetCombo_.addItem("Ramp Up", static_cast<int>(magda::CurvePreset::RampUp) + 1);
    presetCombo_.addItem("Ramp Down", static_cast<int>(magda::CurvePreset::RampDown) + 1);
    presetCombo_.addItem("S-Curve", static_cast<int>(magda::CurvePreset::SCurve) + 1);
    presetCombo_.addItem("Exp", static_cast<int>(magda::CurvePreset::Exponential) + 1);
    presetCombo_.addItem("Log", static_cast<int>(magda::CurvePreset::Logarithmic) + 1);

    userCurvePresets_ = magda::PresetManager::getInstance().getCurvePresets();
    if (!userCurvePresets_.isEmpty()) {
        presetCombo_.addSectionHeading("User");
        for (int i = 0; i < userCurvePresets_.size(); ++i)
            presetCombo_.addItem(userCurvePresets_[i], USER_PRESET_ID_BASE + i);
    }

    if (selectedUserPreset.isNotEmpty()) {
        const int index = userCurvePresets_.indexOf(selectedUserPreset);
        if (index >= 0) {
            presetCombo_.setSelectedId(USER_PRESET_ID_BASE + index, juce::dontSendNotification);
            return;
        }
    }

    if (modInfo_ && modInfo_->curvePreset != magda::CurvePreset::Custom) {
        presetCombo_.setSelectedId(static_cast<int>(modInfo_->curvePreset) + 1,
                                   juce::dontSendNotification);
    }
}

void LFOCurveEditorContent::showPresetError(const juce::String& title,
                                            const juce::String& message) {
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(title)
                                     .withMessage(message)
                                     .withButton("OK"),
                                 nullptr);
}

void LFOCurveEditorContent::showSaveCurvePresetDialog() {
    auto* alert = new juce::AlertWindow("Save Curve Preset", "Enter a name for this curve preset:",
                                        juce::MessageBoxIconType::NoIcon);

    juce::String defaultName = currentUserCurvePreset_;
    juce::String defaultCategory;
    const auto slash = defaultName.lastIndexOfChar('/');
    if (slash > 0) {
        defaultCategory = defaultName.substring(0, slash);
        defaultName = defaultName.substring(slash + 1);
    }
    if (defaultName.isEmpty())
        defaultName = "Custom Curve";

    alert->addTextEditor("category", defaultCategory, "Category (optional):");
    alert->addTextEditor("name", defaultName, "Name:");
    alert->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<LFOCurveEditorContent> safeThis(this);
    alert->enterModalState(
        true, juce::ModalCallbackFunction::create([safeThis, alert](int result) {
            if (result != 1) {
                delete alert;
                return;
            }

            auto category = alert->getTextEditorContents("category").trim();
            auto name = alert->getTextEditorContents("name").trim();
            delete alert;

            if (safeThis == nullptr || name.isEmpty())
                return;

            juce::StringArray categoryParts;
            categoryParts.addTokens(category, "/", "");
            juce::StringArray cleanedCategoryParts;
            for (const auto& part : categoryParts) {
                auto trimmed = part.trim();
                if (trimmed.isNotEmpty())
                    cleanedCategoryParts.add(trimmed);
            }

            const auto cleanCategory = cleanedCategoryParts.joinIntoString("/");
            const auto fullName = cleanCategory.isEmpty() ? name : (cleanCategory + "/" + name);

            auto doSave = [safeThis, fullName]() {
                if (safeThis != nullptr)
                    safeThis->saveCurvePreset(fullName);
            };

            if (magda::PresetManager::getInstance().getCurvePresets().contains(fullName)) {
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::QuestionIcon)
                        .withTitle("Overwrite Preset?")
                        .withMessage("\"" + fullName + "\" already exists. Overwrite?")
                        .withButton("Overwrite")
                        .withButton("Cancel"),
                    [doSave](int choice) {
                        if (choice == 1)
                            doSave();
                    });
            } else {
                doSave();
            }
        }));
}

void LFOCurveEditorContent::saveCurvePreset(const juce::String& presetName) {
    std::vector<magda::CurvePointData> points;
    const auto& editorPoints = curveEditor_.getPoints();
    points.reserve(editorPoints.size());

    for (const auto& point : editorPoints) {
        magda::CurvePointData data;
        data.phase = static_cast<float>(point.x);
        data.value = static_cast<float>(point.y);
        data.tension = static_cast<float>(point.tension);
        data.curveType = magda::curveTypeToInt(point.curveType);
        data.inHandleX = static_cast<float>(point.inHandle.x);
        data.inHandleY = static_cast<float>(point.inHandle.y);
        data.outHandleX = static_cast<float>(point.outHandle.x);
        data.outHandleY = static_cast<float>(point.outHandle.y);
        points.push_back(data);
    }

    if (points.size() < 2) {
        showPresetError("Save Curve Preset Failed", "Curve needs at least two points.");
        return;
    }

    auto& presetManager = magda::PresetManager::getInstance();
    if (!presetManager.saveCurvePreset(points, presetName)) {
        showPresetError("Save Curve Preset Failed", presetManager.getLastError());
        return;
    }

    currentUserCurvePreset_ = presetName;
    rebuildPresetCombo(currentUserCurvePreset_);
}

void LFOCurveEditorContent::paint(juce::Graphics& g) {
    // Header background
    auto headerBounds = getLocalBounds().removeFromTop(HEADER_HEIGHT);
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRect(headerBounds);

    // Header bottom border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(HEADER_HEIGHT - 1, 0.0f, static_cast<float>(getWidth()));

    // Footer background
    auto footerBounds = getLocalBounds().removeFromBottom(FOOTER_HEIGHT);
    g.setColour(DarkTheme::getColour(DarkTheme::SURFACE));
    g.fillRect(footerBounds);

    // Footer top border
    g.drawHorizontalLine(getHeight() - FOOTER_HEIGHT, 0.0f, static_cast<float>(getWidth()));
}

void LFOCurveEditorContent::resized() {
    auto bounds = getLocalBounds();

    // Header at top with preset selector, save button, and reset button
    auto header = bounds.removeFromTop(HEADER_HEIGHT);
    header.reduce(6, 3);
    presetCombo_.setBounds(header.removeFromLeft(90));
    header.removeFromLeft(4);  // Gap
    savePresetButton_->setBounds(header.removeFromLeft(18));
    header.removeFromLeft(4);
    resetButton_->setBounds(header.removeFromLeft(18));

    // Footer at bottom
    auto footer = bounds.removeFromBottom(FOOTER_HEIGHT);
    footer.reduce(6, 4);

    constexpr int gap = 6;

    // Rate section: [Sync][Rate/Division]
    constexpr int syncWidth = 38;
    constexpr int rateWidth = 60;

    syncToggle_.setBounds(footer.removeFromLeft(syncWidth));
    footer.removeFromLeft(gap);
    auto rateBounds = footer.removeFromLeft(rateWidth);
    rateSlider_.setBounds(rateBounds);
    syncDivisionCombo_.setBounds(rateBounds);
    footer.removeFromLeft(gap * 2);

    // Mode section: [Loop/1-Shot][MSEG]
    constexpr int modeWidth = 46;
    loopOneShotToggle_.setBounds(footer.removeFromLeft(modeWidth));
    footer.removeFromLeft(gap);
    msegToggle_.setBounds(footer.removeFromLeft(modeWidth));
    footer.removeFromLeft(gap * 2);

    // Grid section: [Grid:][X combo][Y combo][Snap X][Snap Y]
    constexpr int labelWidth = 30;
    constexpr int comboWidth = 38;
    constexpr int snapWidth = 22;

    gridLabel_.setBounds(footer.removeFromLeft(labelWidth));
    gridXCombo_.setBounds(footer.removeFromLeft(comboWidth));
    footer.removeFromLeft(4);
    gridYCombo_.setBounds(footer.removeFromLeft(comboWidth));
    footer.removeFromLeft(gap);
    snapXToggle_.setBounds(footer.removeFromLeft(snapWidth));
    footer.removeFromLeft(4);
    snapYToggle_.setBounds(footer.removeFromLeft(snapWidth));
    footer.removeFromLeft(4);
    loopSnapToggle_.setBounds(footer.removeFromLeft(snapWidth));

    // Curve editor takes remaining space (between header and footer)
    // Only expand horizontally, not vertically (to avoid overlapping header/footer)
    int padding = curveEditor_.getPadding();
    auto editorBounds =
        bounds.withX(bounds.getX() - padding).withWidth(bounds.getWidth() + padding * 2);
    curveEditor_.setBounds(editorBounds);

    // Bring preset combo to front to ensure it's not hidden
    presetCombo_.toFront(false);
}

// ============================================================================
// LFOCurveEditorWindow
// ============================================================================

LFOCurveEditorWindow::LFOCurveEditorWindow(magda::ModInfo* modInfo,
                                           std::function<void()> onWaveformChanged,
                                           std::function<void()> onDragPreview)
    : DocumentWindow("LFO Curve Editor", DarkTheme::getColour(DarkTheme::BACKGROUND),
                     DocumentWindow::closeButton),
      content_(modInfo, std::move(onWaveformChanged), std::move(onDragPreview)) {
    // Wire up callbacks
    content_.onRateChanged = [this](float rate) {
        if (onRateChanged)
            onRateChanged(rate);
    };
    content_.onTempoSyncChanged = [this](bool synced) {
        if (onTempoSyncChanged)
            onTempoSyncChanged(synced);
    };
    content_.onSyncDivisionChanged = [this](magda::SyncDivision div) {
        if (onSyncDivisionChanged)
            onSyncDivisionChanged(div);
    };
    content_.onOneShotChanged = [this](bool oneShot) {
        if (onOneShotChanged)
            onOneShotChanged(oneShot);
    };
    content_.onLoopRegionChanged = [this](bool useLoop) {
        if (onLoopRegionChanged)
            onLoopRegionChanged(useLoop);
    };

    setContentNonOwned(&content_, true);

    // Window settings
    setSize(500, 300);
    setResizable(true, true);
    setResizeLimits(400, 200, 1000, 600);
    setUsingNativeTitleBar(false);
    setVisible(true);
    setAlwaysOnTop(true);

    centreWithSize(getWidth(), getHeight());
}

void LFOCurveEditorWindow::closeButtonPressed() {
    setVisible(false);
    if (onWindowClosed) {
        onWindowClosed();
    }
}

}  // namespace magda::daw::ui
