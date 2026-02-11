#include "DrumGridUI.hpp"

#include "audio/MagdaSamplerPlugin.hpp"
#include "ui/themes/DarkTheme.hpp"
#include "ui/themes/FontManager.hpp"

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
    if (selected_) {
        bg = DarkTheme::getColour(DarkTheme::ACCENT_BLUE).withAlpha(0.4f);
    } else if (hasSample_) {
        bg = DarkTheme::getColour(DarkTheme::SURFACE).brighter(0.15f);
    } else {
        bg = DarkTheme::getColour(DarkTheme::SURFACE);
    }

    // Dim if muted
    if (muted_)
        bg = bg.withMultipliedAlpha(0.5f);

    g.setColour(bg);
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

    // Border
    if (selected_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_BLUE));
        g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 1.5f);
    } else {
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 0.5f);
    }

    // Solo indicator
    if (soloed_) {
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_ORANGE));
        g.fillRoundedRectangle(bounds.removeFromTop(3).toFloat(), 1.0f);
    }

    // Note name (top)
    auto textArea = getLocalBounds().reduced(4);
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.setColour(DarkTheme::getTextColour());
    g.drawText(noteName_, textArea.removeFromTop(textArea.getHeight() / 2),
               juce::Justification::centredBottom, false);

    // Sample name (bottom, truncated)
    if (sampleName_.isNotEmpty()) {
        g.setFont(FontManager::getInstance().getUIFont(8.0f));
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.drawText(sampleName_, textArea, juce::Justification::centredTop, true);
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

    // Divider between grid and detail panel
    auto area = getLocalBounds().reduced(4);
    int gridWidth = static_cast<int>(area.getWidth() * 0.45f);
    int dividerX = area.getX() + gridWidth;
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawVerticalLine(dividerX, static_cast<float>(area.getY()),
                       static_cast<float>(area.getBottom()));

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

    // Split: 45% grid, 55% detail
    int gridWidth = static_cast<int>(area.getWidth() * 0.45f);
    auto gridArea = area.removeFromLeft(gridWidth);
    area.removeFromLeft(6);  // gap after divider
    auto detailArea = area;

    // --- Pad Grid ---
    auto paginationRow = gridArea.removeFromBottom(22);
    gridArea.removeFromBottom(2);

    // Position pad buttons in 4x4 grid
    int padW = gridArea.getWidth() / kGridCols;
    int padH = gridArea.getHeight() / kGridRows;

    for (int i = 0; i < kPadsPerPage; ++i) {
        int row = i / kGridCols;
        int col = i % kGridCols;
        int x = gridArea.getX() + col * padW;
        int y = gridArea.getY() + row * padH;
        padButtons_[static_cast<size_t>(i)].setBounds(x, y, padW, padH);
    }

    // Pagination
    int btnW = 22;
    prevPageButton_.setBounds(paginationRow.removeFromLeft(btnW));
    nextPageButton_.setBounds(paginationRow.removeFromRight(btnW));
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

    // Remaining space: embedded SamplerUI
    padSamplerUI_.setBounds(detailArea);
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

    // Wire parameter changes to the sampler
    padSamplerUI_.onParameterChanged = [this, padIndex](int paramIndex, float value) {
        if (!getPadSampler)
            return;
        auto* s = getPadSampler(padIndex);
        if (!s)
            return;
        // Write directly to sampler CachedValues based on param index
        switch (paramIndex) {
            case 0:
                s->attackValue = value;
                break;
            case 1:
                s->decayValue = value;
                break;
            case 2:
                s->sustainValue = value;
                break;
            case 3:
                s->releaseValue = value;
                break;
            case 4:
                s->pitchValue = value;
                break;
            case 5:
                s->fineValue = value;
                break;
            case 6:
                s->levelValue = value;
                break;
            case 7:
                s->sampleStartValue = value;
                break;
            case 8:
                s->loopStartValue = value;
                break;
            case 9:
                s->loopEndValue = value;
                break;
            case 10:
                s->velAmountValue = value;
                break;
            default:
                break;
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
}

// =============================================================================
// DragAndDropTarget (plugin drops)
// =============================================================================

bool DrumGridUI::isInterestedInDragSource(const SourceDetails& details) {
    if (auto* obj = details.description.getDynamicObject()) {
        return obj->getProperty("type").toString() == "plugin";
    }
    return false;
}

void DrumGridUI::itemDragEnter(const SourceDetails& details) {
    int btnIdx = padButtonIndexAtPoint(details.localPosition.toInt());
    if (btnIdx >= 0)
        dropHighlightPad_ = currentPage_ * kPadsPerPage + btnIdx;
    else
        dropHighlightPad_ = -1;
    repaint();
}

void DrumGridUI::itemDragExit(const SourceDetails&) {
    dropHighlightPad_ = -1;
    repaint();
}

void DrumGridUI::itemDropped(const SourceDetails& details) {
    dropHighlightPad_ = -1;

    int btnIdx = padButtonIndexAtPoint(details.localPosition.toInt());
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
