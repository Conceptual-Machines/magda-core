#include "DrumGridUI.hpp"

#include <BinaryData.h>
#include <tracktion_engine/tracktion_engine.h>

#include "audio/MagdaSamplerPlugin.hpp"
#include "ui/debug/DebugSettings.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

namespace te = tracktion::engine;

namespace magda::daw::ui {

// =============================================================================
// PadButton
// =============================================================================

DrumGridUI::PadButton::PadButton() {}

void DrumGridUI::PadButton::setPadIndex(int index) {
    padIndex_ = index;
}

void DrumGridUI::PadButton::setNoteName(const juce::String& name) {
    if (noteName_ != name) {
        noteName_ = name;
        repaint();
    }
}

void DrumGridUI::PadButton::setSampleName(const juce::String& name) {
    if (sampleName_ != name) {
        sampleName_ = name;
        repaint();
    }
}

void DrumGridUI::PadButton::setSelected(bool selected) {
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void DrumGridUI::PadButton::setHasSample(bool has) {
    if (hasSample_ != has) {
        hasSample_ = has;
        repaint();
    }
}

void DrumGridUI::PadButton::setMuted(bool muted) {
    if (muted_ != muted) {
        muted_ = muted;
        repaint();
    }
}

void DrumGridUI::PadButton::setSoloed(bool soloed) {
    if (soloed_ != soloed) {
        soloed_ = soloed;
        repaint();
    }
}

void DrumGridUI::PadButton::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().reduced(2);

    // Background colour
    juce::Colour bg;
    float borderThickness;
    if (selected_) {
        bg = DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.4f);
        borderThickness = 1.5f;
    } else if (hasSample_) {
        bg = DarkTheme::getColour(DarkTheme::SURFACE).brighter(0.1f);
        borderThickness = 0.75f;
    } else {
        bg = DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.03f);
        borderThickness = 0.5f;
    }

    // Dim if muted
    if (muted_)
        bg = bg.withMultipliedAlpha(0.5f);

    g.setColour(bg);
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

    // Border
    if (selected_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    }
    g.drawRoundedRectangle(bounds.toFloat(), 3.0f, borderThickness);

    // Solo indicator — orange top bar
    if (soloed_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.fillRoundedRectangle(bounds.removeFromTop(3).toFloat(), 1.0f);
    }

    auto textArea = getLocalBounds().reduced(4);

    if (hasSample_) {
        // --- Filled pad: note name top, plugin/sample name bottom ---
        auto topRow = textArea.removeFromTop(textArea.getHeight() / 3);
        auto bottomRow = textArea;

        // Note name (small, secondary)
        g.setFont(FontManager::getInstance().getUIFont(8.0f));
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.drawText(noteName_, topRow, juce::Justification::centredBottom, false);

        // Plugin/sample name (primary, truncated)
        g.setFont(FontManager::getInstance().getUIFont(9.0f));
        g.setColour(DarkTheme::getTextColour());
        g.drawText(sampleName_, bottomRow, juce::Justification::centred, true);
    } else {
        // --- Empty pad: note name centred ---
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.drawText(noteName_, textArea, juce::Justification::centred, false);
    }
}

void DrumGridUI::PadButton::mouseDown(const juce::MouseEvent& /*e*/) {
    if (onClicked)
        onClicked(padIndex_);
}

// =============================================================================
// DrumGridUI
// =============================================================================

DrumGridUI::DrumGridUI() {
    // Setup pad buttons
    for (int i = 0; i < kPadsPerPage; ++i) {
        padButtons_[static_cast<size_t>(i)].onClicked = [this](int padIndex) {
            setSelectedPad(padIndex);
        };
        addAndMakeVisible(padButtons_[static_cast<size_t>(i)]);
    }

    // Pagination
    setupButton(prevPageButton_);
    prevPageButton_.onClick = [this]() { goToPrevPage(); };
    addAndMakeVisible(prevPageButton_);

    setupButton(nextPageButton_);
    nextPageButton_.onClick = [this]() { goToNextPage(); };
    addAndMakeVisible(nextPageButton_);

    pageLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    pageLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    pageLabel_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pageLabel_);

    // Detail panel labels
    setupLabel(detailPadNameLabel_, "Pad 0 - C2", 11.0f);
    detailPadNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    setupLabel(detailSampleNameLabel_, "(empty)", 10.0f);
    setupLabel(levelLabel_, "LEVEL", 9.0f);
    setupLabel(panLabel_, "PAN", 9.0f);

    // Level slider (-60 to +12 dB)
    levelSlider_.setRange(-60.0, 12.0, 0.1);
    levelSlider_.setValue(0.0, juce::dontSendNotification);
    levelSlider_.onValueChanged = [this](double value) {
        if (onPadLevelChanged)
            onPadLevelChanged(selectedPad_, static_cast<float>(value));
    };
    addAndMakeVisible(levelSlider_);

    // Pan slider (-1 to +1)
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(0.0, juce::dontSendNotification);
    panSlider_.setValueFormatter([](double v) {
        if (std::abs(v) < 0.01)
            return juce::String("C");
        if (v < 0)
            return juce::String(static_cast<int>(-v * 100)) + "L";
        return juce::String(static_cast<int>(v * 100)) + "R";
    });
    panSlider_.setValueParser([](const juce::String& text) {
        juce::String t = text.trim().toUpperCase();
        if (t == "C" || t == "0")
            return 0.0;
        if (t.endsWithIgnoreCase("L"))
            return -t.dropLastCharacters(1).trim().getDoubleValue() / 100.0;
        if (t.endsWithIgnoreCase("R"))
            return t.dropLastCharacters(1).trim().getDoubleValue() / 100.0;
        return t.getDoubleValue();
    });
    panSlider_.onValueChanged = [this](double value) {
        if (onPadPanChanged)
            onPadPanChanged(selectedPad_, static_cast<float>(value));
    };
    addAndMakeVisible(panSlider_);

    // Mute/Solo buttons
    setupButton(muteButton_);
    muteButton_.setClickingTogglesState(true);
    muteButton_.onClick = [this]() {
        bool muted = muteButton_.getToggleState();
        padInfos_[static_cast<size_t>(selectedPad_)].mute = muted;
        if (onPadMuteChanged)
            onPadMuteChanged(selectedPad_, muted);
        refreshPadButtons();
    };
    addAndMakeVisible(muteButton_);

    setupButton(soloButton_);
    soloButton_.setClickingTogglesState(true);
    soloButton_.onClick = [this]() {
        bool soloed = soloButton_.getToggleState();
        padInfos_[static_cast<size_t>(selectedPad_)].solo = soloed;
        if (onPadSoloChanged)
            onPadSoloChanged(selectedPad_, soloed);
        refreshPadButtons();
    };
    addAndMakeVisible(soloButton_);

    // Load/Clear buttons
    setupButton(loadButton_);
    loadButton_.onClick = [this]() {
        if (onLoadRequested)
            onLoadRequested(selectedPad_);
    };
    addAndMakeVisible(loadButton_);

    setupButton(clearButton_);
    clearButton_.onClick = [this]() {
        if (onClearRequested)
            onClearRequested(selectedPad_);
    };
    addAndMakeVisible(clearButton_);

    // Embedded SamplerUI (initially hidden, shown when pad has MagdaSamplerPlugin)
    addChildComponent(padSamplerUI_);

    // Plugin parameter grid (for non-sampler child plugins)
    for (int i = 0; i < kPluginParamSlots; ++i) {
        pluginParamSlots_[static_cast<size_t>(i)] = std::make_unique<ParamSlotComponent>(i);
        addChildComponent(*pluginParamSlots_[static_cast<size_t>(i)]);
    }

    // UI button to open plugin native window
    pluginUIButton_ = std::make_unique<magda::SvgButton>("UI", BinaryData::open_in_new_svg,
                                                         BinaryData::open_in_new_svgSize);
    pluginUIButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    pluginUIButton_->setHoverColor(DarkTheme::getTextColour());
    addChildComponent(*pluginUIButton_);

    // Plugin name label for param area header
    pluginNameLabel_.setFont(FontManager::getInstance().getUIFont(10.0f));
    pluginNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    pluginNameLabel_.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(pluginNameLabel_);

    // Chains panel
    chainsLabel_.setText("Chains:", juce::dontSendNotification);
    chainsLabel_.setFont(FontManager::getInstance().getUIFont(9.0f));
    chainsLabel_.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    chainsLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(chainsLabel_);

    chainsViewport_.setScrollBarsShown(true, false);
    chainsViewport_.setInterceptsMouseClicks(false, true);
    chainsContainer_.setInterceptsMouseClicks(false, true);
    chainsViewport_.setViewedComponent(&chainsContainer_, false);
    addAndMakeVisible(chainsViewport_);

    chainsToggleButton_ = std::make_unique<magda::SvgButton>("Chains", BinaryData::menu_svg,
                                                             BinaryData::menu_svgSize);
    chainsToggleButton_->setClickingTogglesState(true);
    chainsToggleButton_->setToggleState(chainsPanelVisible_, juce::dontSendNotification);
    chainsToggleButton_->setNormalColor(DarkTheme::getSecondaryTextColour());
    chainsToggleButton_->setActiveColor(juce::Colours::white);
    chainsToggleButton_->setActiveBackgroundColor(
        DarkTheme::getColour(DarkTheme::ACCENT_BLUE).darker(0.3f));
    chainsToggleButton_->setActive(chainsPanelVisible_);
    chainsToggleButton_->onClick = [this]() {
        setChainsPanelVisible(chainsToggleButton_->getToggleState());
        chainsToggleButton_->setActive(chainsToggleButton_->getToggleState());
    };
    addAndMakeVisible(*chainsToggleButton_);

    // Initialize
    refreshPadButtons();
    refreshDetailPanel();
}

void DrumGridUI::updatePadInfo(int padIndex, const juce::String& sampleName, bool mute, bool solo,
                               float levelDb, float pan) {
    if (padIndex < 0 || padIndex >= kTotalPads)
        return;

    auto& info = padInfos_[static_cast<size_t>(padIndex)];
    info.sampleName = sampleName;
    info.mute = mute;
    info.solo = solo;
    info.level = levelDb;
    info.pan = pan;

    // Update visible pad buttons if this pad is on the current page
    int pageStart = currentPage_ * kPadsPerPage;
    if (padIndex >= pageStart && padIndex < pageStart + kPadsPerPage) {
        int btnIdx = padIndex - pageStart;
        auto& btn = padButtons_[static_cast<size_t>(btnIdx)];
        btn.setSampleName(sampleName);
        btn.setHasSample(sampleName.isNotEmpty());
        btn.setMuted(mute);
        btn.setSoloed(solo);
    }

    // Update detail panel if this is the selected pad
    if (padIndex == selectedPad_)
        refreshDetailPanel();

    // Rebuild chain rows to reflect updated pad state
    rebuildChainRows();
}

void DrumGridUI::setSelectedPad(int padIndex) {
    if (padIndex < 0 || padIndex >= kTotalPads)
        return;

    selectedPad_ = padIndex;

    // Switch page if needed
    int targetPage = padIndex / kPadsPerPage;
    if (targetPage != currentPage_) {
        currentPage_ = targetPage;
        refreshPadButtons();
    } else {
        // Just update selection highlight
        int pageStart = currentPage_ * kPadsPerPage;
        for (int i = 0; i < kPadsPerPage; ++i) {
            padButtons_[static_cast<size_t>(i)].setSelected(pageStart + i == selectedPad_);
        }
    }

    refreshDetailPanel();
    updatePadSamplerUI(padIndex);

    // If SamplerUI is not showing, try to show plugin params instead
    if (!padSamplerUI_.isVisible())
        refreshPluginParams(padIndex);

    // Update chain row selection highlights
    for (auto& row : chainRows_) {
        row->setSelected(row->getPadIndex() == selectedPad_);
    }
}

// =============================================================================
// FileDragAndDropTarget
// =============================================================================

bool DrumGridUI::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files) {
        if (f.endsWithIgnoreCase(".wav") || f.endsWithIgnoreCase(".aif") ||
            f.endsWithIgnoreCase(".aiff") || f.endsWithIgnoreCase(".flac") ||
            f.endsWithIgnoreCase(".ogg") || f.endsWithIgnoreCase(".mp3"))
            return true;
    }
    return false;
}

void DrumGridUI::filesDropped(const juce::StringArray& files, int x, int y) {
    // Find which pad the file was dropped on
    int btnIdx = padButtonIndexAtPoint({x, y});
    if (btnIdx < 0)
        return;

    int padIndex = currentPage_ * kPadsPerPage + btnIdx;

    for (const auto& f : files) {
        juce::File file(f);
        if (file.existsAsFile() && onSampleDropped) {
            setSelectedPad(padIndex);
            onSampleDropped(padIndex, file);
            break;
        }
    }
}

// =============================================================================
// Paint
// =============================================================================

void DrumGridUI::paint(juce::Graphics& g) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawRect(getLocalBounds(), 1);
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).brighter(0.05f));
    g.fillRect(getLocalBounds().reduced(1));

    // Dividers
    auto area = getLocalBounds().reduced(4);
    float gridFrac = chainsPanelVisible_ ? 0.25f : 0.3f;
    int gridWidth = static_cast<int>(area.getWidth() * gridFrac);
    int dividerX = area.getX() + gridWidth;
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawVerticalLine(dividerX, static_cast<float>(area.getY()),
                       static_cast<float>(area.getBottom()));

    if (chainsPanelVisible_) {
        // Second divider between chains and detail
        int afterGrid = dividerX + 6;  // gap
        int remainingWidth = area.getRight() - afterGrid;
        int chainsWidth = static_cast<int>(remainingWidth * 0.4f);
        int divider2X = afterGrid + chainsWidth;
        g.drawVerticalLine(divider2X, static_cast<float>(area.getY()),
                           static_cast<float>(area.getBottom()));
    }

    // Plugin drop highlight on pad
    if (dropHighlightPad_ >= 0) {
        int pageStart = currentPage_ * kPadsPerPage;
        int btnIdx = dropHighlightPad_ - pageStart;
        if (btnIdx >= 0 && btnIdx < kPadsPerPage) {
            auto padBounds = padButtons_[static_cast<size_t>(btnIdx)].getBounds();
            g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.3f));
            g.fillRoundedRectangle(padBounds.toFloat(), 3.0f);
        }
    }
}

// =============================================================================
// Layout
// =============================================================================

void DrumGridUI::resized() {
    auto area = getLocalBounds().reduced(6);

    // Grid fraction depends on whether chains panel is visible
    float gridFrac = chainsPanelVisible_ ? 0.25f : 0.3f;
    int gridWidth = static_cast<int>(area.getWidth() * gridFrac);
    auto gridArea = area.removeFromLeft(gridWidth);
    area.removeFromLeft(6);  // gap after divider

    // Chains panel (if visible)
    if (chainsPanelVisible_) {
        int chainsWidth = static_cast<int>(area.getWidth() * 0.4f);
        auto chainsArea = area.removeFromLeft(chainsWidth);
        area.removeFromLeft(6);  // gap after divider

        // Toggle button in the chains header
        auto chainsHeader = chainsArea.removeFromTop(18);
        chainsToggleButton_->setBounds(chainsHeader.removeFromRight(16));
        chainsLabel_.setBounds(chainsHeader);
        chainsLabel_.setVisible(true);
        chainsArea.removeFromTop(2);

        // Viewport with chain rows
        chainsViewport_.setBounds(chainsArea);
        chainsViewport_.setVisible(true);

        // Size container and layout rows
        int scrollbarWidth = chainsViewport_.getScrollBarThickness();
        int containerWidth = chainsViewport_.getWidth() - scrollbarWidth;
        int y = 0;
        for (auto& row : chainRows_) {
            row->setBounds(0, y, containerWidth, PadChainRowComponent::ROW_HEIGHT);
            y += PadChainRowComponent::ROW_HEIGHT + 2;
        }
        chainsContainer_.setSize(containerWidth, juce::jmax(y, chainsArea.getHeight()));
    } else {
        chainsLabel_.setVisible(false);
        chainsViewport_.setVisible(false);
        // Place toggle button in the pagination area (handled below)
    }

    auto detailArea = area;

    // --- Pad Grid ---
    auto paginationRow = gridArea.removeFromBottom(22);
    gridArea.removeFromBottom(2);

    // Position pad buttons in 4x4 grid (square pads, fill grid area)
    constexpr int padGap = 3;
    int padSize = juce::jmin((gridArea.getWidth() - padGap * (kGridCols - 1)) / kGridCols,
                             (gridArea.getHeight() - padGap * (kGridRows - 1)) / kGridRows);

    for (int i = 0; i < kPadsPerPage; ++i) {
        int row = i / kGridCols;
        int col = i % kGridCols;
        int x = gridArea.getX() + col * (padSize + padGap);
        int y = gridArea.getY() + row * (padSize + padGap);
        padButtons_[static_cast<size_t>(i)].setBounds(x, y, padSize, padSize);
    }

    // Pagination
    int btnW = 22;
    prevPageButton_.setBounds(paginationRow.removeFromLeft(btnW));
    nextPageButton_.setBounds(paginationRow.removeFromRight(btnW));

    // When chains panel is hidden, put the toggle button in pagination row
    if (!chainsPanelVisible_) {
        chainsToggleButton_->setBounds(paginationRow.removeFromRight(16));
        paginationRow.removeFromRight(2);
    }

    pageLabel_.setBounds(paginationRow);

    // --- Detail Panel ---
    // Row 1: pad name + sample name
    detailPadNameLabel_.setBounds(detailArea.removeFromTop(14));
    detailArea.removeFromTop(1);
    detailSampleNameLabel_.setBounds(detailArea.removeFromTop(12));
    detailArea.removeFromTop(4);

    // Row 2: compact controls — Level, Pan, M, S, Load, Clear all in one row
    auto controlsRow = detailArea.removeFromTop(20);
    int controlW = controlsRow.getWidth() / 6;
    levelSlider_.setBounds(controlsRow.removeFromLeft(controlW).reduced(1, 0));
    panSlider_.setBounds(controlsRow.removeFromLeft(controlW).reduced(1, 0));
    muteButton_.setBounds(controlsRow.removeFromLeft(controlW).reduced(1, 0));
    soloButton_.setBounds(controlsRow.removeFromLeft(controlW).reduced(1, 0));
    loadButton_.setBounds(controlsRow.removeFromLeft(controlW).reduced(1, 0));
    clearButton_.setBounds(controlsRow.reduced(1, 0));

    // Hide level/pan labels (compact mode)
    levelLabel_.setBounds(0, 0, 0, 0);
    panLabel_.setBounds(0, 0, 0, 0);

    detailArea.removeFromTop(4);

    // Remaining space: embedded SamplerUI or plugin param grid
    padSamplerUI_.setBounds(detailArea);

    if (!padSamplerUI_.isVisible() && pluginParamSlots_[0] && pluginParamSlots_[0]->isVisible()) {
        // Header row: plugin name + UI button
        auto headerRow = detailArea.removeFromTop(18);
        int uiBtnSize = 18;
        pluginUIButton_->setBounds(headerRow.removeFromRight(uiBtnSize));
        headerRow.removeFromRight(4);
        pluginNameLabel_.setBounds(headerRow);

        detailArea.removeFromTop(4);

        // 4x4 grid of ParamSlotComponents
        auto contentArea = detailArea.reduced(2, 0);
        constexpr int paramCols = 4;
        constexpr int paramRows = 4;
        int cellWidth = contentArea.getWidth() / paramCols;
        int cellHeight = contentArea.getHeight() / paramRows;

        auto labelFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamLabelFontSize());
        auto valueFont = FontManager::getInstance().getUIFont(
            DebugSettings::getInstance().getParamValueFontSize());

        for (int i = 0; i < kPluginParamSlots; ++i) {
            int row = i / paramCols;
            int col = i % paramCols;
            int x = contentArea.getX() + col * cellWidth;
            int y = contentArea.getY() + row * cellHeight;
            pluginParamSlots_[static_cast<size_t>(i)]->setFonts(labelFont, valueFont);
            pluginParamSlots_[static_cast<size_t>(i)]->setBounds(x, y, cellWidth - 2, cellHeight);
        }
    }
}

// =============================================================================
// SamplerUI embedding
// =============================================================================

void DrumGridUI::updatePadSamplerUI(int padIndex) {
    if (!getPadSampler) {
        padSamplerUI_.setVisible(false);
        return;
    }

    auto* sampler = getPadSampler(padIndex);
    if (!sampler) {
        padSamplerUI_.setVisible(false);
        return;
    }

    // Wire parameter changes to the sampler's AutomatableParameters
    padSamplerUI_.onParameterChanged = [this, padIndex](int paramIndex, float value) {
        if (!getPadSampler)
            return;
        auto* s = getPadSampler(padIndex);
        if (!s)
            return;
        auto params = s->getAutomatableParameters();
        if (paramIndex >= 0 && paramIndex < params.size()) {
            params[paramIndex]->setParameter(value, juce::sendNotification);
        }
    };

    padSamplerUI_.onLoopEnabledChanged = [this, padIndex](bool enabled) {
        if (!getPadSampler)
            return;
        auto* s = getPadSampler(padIndex);
        if (!s)
            return;
        s->loopEnabledAtomic.store(enabled, std::memory_order_relaxed);
        s->loopEnabledValue = enabled;
    };

    padSamplerUI_.getPlaybackPosition = [this, padIndex]() -> double {
        if (!getPadSampler)
            return 0.0;
        auto* s = getPadSampler(padIndex);
        if (!s)
            return 0.0;
        return s->getPlaybackPosition();
    };

    padSamplerUI_.onFileDropped = [this, padIndex](const juce::File& file) {
        if (onSampleDropped)
            onSampleDropped(padIndex, file);
    };

    padSamplerUI_.onLoadSampleRequested = [this, padIndex]() {
        if (onLoadRequested)
            onLoadRequested(padIndex);
    };

    // Update parameters from sampler state
    juce::String sampleName;
    auto file = sampler->getSampleFile();
    if (file.existsAsFile())
        sampleName = file.getFileNameWithoutExtension();

    padSamplerUI_.updateParameters(
        sampler->attackValue.get(), sampler->decayValue.get(), sampler->sustainValue.get(),
        sampler->releaseValue.get(), sampler->pitchValue.get(), sampler->fineValue.get(),
        sampler->levelValue.get(), sampler->sampleStartValue.get(), sampler->loopEnabledValue.get(),
        sampler->loopStartValue.get(), sampler->loopEndValue.get(), sampler->velAmountValue.get(),
        sampleName);

    padSamplerUI_.setWaveformData(sampler->getWaveform(), sampler->getSampleRate(),
                                  sampler->getSampleLengthSeconds());

    padSamplerUI_.setVisible(true);

    // Hide plugin param slots when SamplerUI is shown
    for (auto& slot : pluginParamSlots_)
        if (slot)
            slot->setVisible(false);
    pluginUIButton_->setVisible(false);
    pluginNameLabel_.setVisible(false);
}

// =============================================================================
// Plugin parameter display
// =============================================================================

void DrumGridUI::refreshPluginParams(int padIndex) {
    if (!getPadPlugin) {
        for (auto& slot : pluginParamSlots_)
            if (slot)
                slot->setVisible(false);
        pluginUIButton_->setVisible(false);
        pluginNameLabel_.setVisible(false);
        return;
    }

    auto* plugin = getPadPlugin(padIndex);

    // Hide if no plugin, or if it's a MagdaSamplerPlugin (SamplerUI handles that)
    if (!plugin || dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin) != nullptr) {
        for (auto& slot : pluginParamSlots_)
            if (slot)
                slot->setVisible(false);
        pluginUIButton_->setVisible(false);
        pluginNameLabel_.setVisible(false);
        return;
    }

    auto params = plugin->getAutomatableParameters();

    for (int i = 0; i < kPluginParamSlots; ++i) {
        auto& slot = pluginParamSlots_[static_cast<size_t>(i)];
        if (i < params.size()) {
            auto* param = params[i];
            slot->setParamName(param->getParameterName());
            slot->setParamValue(param->getCurrentNormalisedValue());
            slot->onValueChanged = [param](double value) {
                param->setParameter(static_cast<float>(value), juce::sendNotificationSync);
            };
            slot->setVisible(true);
        } else {
            slot->setVisible(false);
        }
    }

    // Show UI button wired to open plugin native editor
    pluginUIButton_->setVisible(true);
    pluginUIButton_->onClick = [plugin]() {
        if (auto* ext = dynamic_cast<te::ExternalPlugin*>(plugin)) {
            if (ext->windowState) {
                ext->windowState->showWindowExplicitly();
            }
        } else {
            plugin->showWindowExplicitly();
        }
    };

    pluginNameLabel_.setText(plugin->getName(), juce::dontSendNotification);
    pluginNameLabel_.setVisible(true);

    resized();
}

// =============================================================================
// DragAndDropTarget (plugin drops)
// =============================================================================

bool DrumGridUI::isInterestedInDragSource(const SourceDetails& details) {
    if (auto* obj = details.description.getDynamicObject()) {
        bool interested = obj->getProperty("type").toString() == "plugin";
        DBG("DrumGridUI::isInterestedInDragSource: " << (interested ? "YES" : "NO"));
        return interested;
    }
    return false;
}

void DrumGridUI::itemDragEnter(const SourceDetails& details) {
    int btnIdx = padButtonIndexAtPoint(details.localPosition);
    if (btnIdx >= 0)
        dropHighlightPad_ = currentPage_ * kPadsPerPage + btnIdx;
    else
        dropHighlightPad_ = -1;
    repaint();
}

void DrumGridUI::itemDragMove(const SourceDetails& details) {
    int btnIdx = padButtonIndexAtPoint(details.localPosition);
    int newHighlight = btnIdx >= 0 ? currentPage_ * kPadsPerPage + btnIdx : -1;
    if (newHighlight != dropHighlightPad_) {
        dropHighlightPad_ = newHighlight;
        repaint();
    }
}

void DrumGridUI::itemDragExit(const SourceDetails&) {
    dropHighlightPad_ = -1;
    repaint();
}

void DrumGridUI::itemDropped(const SourceDetails& details) {
    dropHighlightPad_ = -1;

    int btnIdx = padButtonIndexAtPoint(details.localPosition);
    DBG("DrumGridUI::itemDropped at " << details.localPosition.toString() << " btnIdx=" << btnIdx);
    if (btnIdx < 0) {
        repaint();
        return;
    }

    int padIndex = currentPage_ * kPadsPerPage + btnIdx;
    setSelectedPad(padIndex);

    if (auto* obj = details.description.getDynamicObject()) {
        if (onPluginDropped)
            onPluginDropped(padIndex, *obj);
    }

    repaint();
}

// =============================================================================
// Chains panel
// =============================================================================

void DrumGridUI::rebuildChainRows() {
    chainRows_.clear();
    chainsContainer_.removeAllChildren();

    for (int i = 0; i < kTotalPads; ++i) {
        auto& info = padInfos_[static_cast<size_t>(i)];
        if (info.sampleName.isEmpty())
            continue;

        auto row = std::make_unique<PadChainRowComponent>(i);
        juce::String displayName = getNoteName(i) + " " + info.sampleName;
        row->updateFromPad(displayName, info.level, info.pan, info.mute, info.solo);

        row->onClicked = [this](int padIndex) { setSelectedPad(padIndex); };
        row->onLevelChanged = [this](int padIndex, float val) {
            if (onPadLevelChanged)
                onPadLevelChanged(padIndex, val);
        };
        row->onPanChanged = [this](int padIndex, float val) {
            if (onPadPanChanged)
                onPadPanChanged(padIndex, val);
        };
        row->onMuteChanged = [this](int padIndex, bool val) {
            padInfos_[static_cast<size_t>(padIndex)].mute = val;
            if (onPadMuteChanged)
                onPadMuteChanged(padIndex, val);
            refreshPadButtons();
        };
        row->onSoloChanged = [this](int padIndex, bool val) {
            padInfos_[static_cast<size_t>(padIndex)].solo = val;
            if (onPadSoloChanged)
                onPadSoloChanged(padIndex, val);
            refreshPadButtons();
        };
        row->onDeleteClicked = [this](int padIndex) {
            if (onPadDeleteRequested)
                onPadDeleteRequested(padIndex);
            else if (onClearRequested)
                onClearRequested(padIndex);
        };

        row->setSelected(i == selectedPad_);
        chainsContainer_.addAndMakeVisible(*row);
        chainRows_.push_back(std::move(row));
    }

    resized();
    repaint();
}

void DrumGridUI::setChainsPanelVisible(bool visible) {
    if (chainsPanelVisible_ == visible)
        return;
    chainsPanelVisible_ = visible;
    resized();
    repaint();
    if (onLayoutChanged)
        onLayoutChanged();
}

// =============================================================================
// Internal helpers
// =============================================================================

void DrumGridUI::refreshPadButtons() {
    int pageStart = currentPage_ * kPadsPerPage;

    for (int i = 0; i < kPadsPerPage; ++i) {
        int padIdx = pageStart + i;
        auto& btn = padButtons_[static_cast<size_t>(i)];
        auto& info = padInfos_[static_cast<size_t>(padIdx)];

        btn.setPadIndex(padIdx);
        btn.setNoteName(getNoteName(padIdx));
        btn.setSampleName(info.sampleName);
        btn.setHasSample(info.sampleName.isNotEmpty());
        btn.setSelected(padIdx == selectedPad_);
        btn.setMuted(info.mute);
        btn.setSoloed(info.solo);
    }

    // Update page label
    pageLabel_.setText("Page " + juce::String(currentPage_ + 1) + "/" + juce::String(kNumPages),
                       juce::dontSendNotification);
    prevPageButton_.setEnabled(currentPage_ > 0);
    nextPageButton_.setEnabled(currentPage_ < kNumPages - 1);
}

void DrumGridUI::refreshDetailPanel() {
    auto& info = padInfos_[static_cast<size_t>(selectedPad_)];

    detailPadNameLabel_.setText("Pad " + juce::String(selectedPad_) + " - " +
                                    getNoteName(selectedPad_),
                                juce::dontSendNotification);

    if (info.sampleName.isNotEmpty()) {
        detailSampleNameLabel_.setText(info.sampleName, juce::dontSendNotification);
        detailSampleNameLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    } else {
        detailSampleNameLabel_.setText("(empty)", juce::dontSendNotification);
        detailSampleNameLabel_.setColour(juce::Label::textColourId,
                                         DarkTheme::getSecondaryTextColour());
    }

    levelSlider_.setValue(info.level, juce::dontSendNotification);
    panSlider_.setValue(info.pan, juce::dontSendNotification);

    muteButton_.setToggleState(info.mute, juce::dontSendNotification);
    muteButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_RED));
    soloButton_.setToggleState(info.solo, juce::dontSendNotification);
    soloButton_.setColour(juce::TextButton::buttonOnColourId,
                          DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
}

void DrumGridUI::goToPrevPage() {
    if (currentPage_ > 0) {
        --currentPage_;
        refreshPadButtons();
    }
}

void DrumGridUI::goToNextPage() {
    if (currentPage_ < kNumPages - 1) {
        ++currentPage_;
        refreshPadButtons();
    }
}

juce::String DrumGridUI::getNoteName(int padIndex) {
    int midiNote = 36 + padIndex;  // Pad 0 = MIDI 36 = C2
    return juce::MidiMessage::getMidiNoteName(midiNote, true, true, 3);
}

int DrumGridUI::padButtonIndexAtPoint(juce::Point<int> point) const {
    for (int i = 0; i < kPadsPerPage; ++i) {
        if (padButtons_[static_cast<size_t>(i)].getBounds().contains(point))
            return i;
    }
    return -1;
}

void DrumGridUI::setupLabel(juce::Label& label, const juce::String& text, float fontSize) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(FontManager::getInstance().getUIFont(fontSize));
    label.setColour(juce::Label::textColourId, DarkTheme::getSecondaryTextColour());
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);
}

void DrumGridUI::setupButton(juce::TextButton& button) {
    button.setColour(juce::TextButton::buttonColourId, DarkTheme::getColour(DarkTheme::SURFACE));
    button.setColour(juce::TextButton::textColourOffId, DarkTheme::getTextColour());
}

}  // namespace magda::daw::ui
