#include "ChordPanelContent.hpp"

#include "ui/components/chord/ChordBlockComponent.hpp"
#include "ui/components/common/DraggableValueLabel.hpp"

namespace magda::daw::ui {

// ============================================================================
// ScaleBlockComponent
// ============================================================================

ScaleBlockComponent::ScaleBlockComponent(const magda::music::ScaleWithChords& scale)
    : scale_(scale) {
    setName("ScaleBlock");
    setRepaintsOnMouseActivity(true);
}

void ScaleBlockComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    // Green-tinted background — brighter when selected
    float baseAlpha = selected_ ? 0.35f : 0.15f;
    auto colour = DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(baseAlpha);
    if (isMouseOver())
        colour = DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(baseAlpha + 0.15f);

    g.setColour(colour);
    g.fillRoundedRectangle(bounds, 3.0f);

    // Border when selected or hovered
    if (selected_ || isMouseOver()) {
        float borderAlpha = selected_ ? 0.7f : 0.5f;
        g.setColour(DarkTheme::getColour(DarkTheme::ACCENT_GREEN).withAlpha(borderAlpha));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
    }

    // Scale name
    g.setColour(selected_ ? DarkTheme::getTextColour()
                          : DarkTheme::getTextColour().withAlpha(0.8f));
    g.setFont(FontManager::getInstance().getUIFont(10.0f));
    g.drawText(juce::String(scale_.name), getLocalBounds().reduced(4, 0),
               juce::Justification::centred);
}

void ScaleBlockComponent::mouseDown(const juce::MouseEvent& /*e*/) {
    // Single click opens diatonic chord popup
    if (auto* parent = dynamic_cast<ChordPanelContent*>(getParentComponent()))
        parent->showScalePopup(scale_, this);
}

void ScaleBlockComponent::mouseDoubleClick(const juce::MouseEvent& /*e*/) {
    // Double-click toggles selection for filtering
    if (auto* parent = dynamic_cast<ChordPanelContent*>(getParentComponent()))
        parent->toggleScaleSelection(this);
}

void ScaleBlockComponent::mouseEnter(const juce::MouseEvent& /*e*/) {
    repaint();
}

void ScaleBlockComponent::mouseExit(const juce::MouseEvent& /*e*/) {
    repaint();
}

// ============================================================================
// ScaleChordsPopup
// ============================================================================

ScaleChordsPopup::ScaleChordsPopup(const magda::music::ScaleWithChords& scale) : scale_(scale) {
    setName("ScaleChordsPopup");

    // Build chord blocks for each diatonic triad
    for (size_t i = 0; i < scale_.chords.size(); ++i) {
        auto block = std::make_unique<ChordBlockComponent>(scale_.chords[i]);

        // Roman numeral degree label
        static const char* romanNumerals[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
        if (i < 7) {
            juce::String degree = romanNumerals[i];
            // Lowercase for minor/diminished
            if (scale_.chords[i].quality == magda::music::ChordQuality::Minor ||
                scale_.chords[i].quality == magda::music::ChordQuality::Diminished) {
                degree = degree.toLowerCase();
            }
            if (scale_.chords[i].quality == magda::music::ChordQuality::Diminished)
                degree += juce::String(juce::CharPointer_UTF8("\xc2\xb0"));  // degree symbol °
            block->setDegreeLabel(degree);
        }

        addAndMakeVisible(block.get());
        chordBlocks_.push_back(std::move(block));
    }
}

void ScaleChordsPopup::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    // Dark background with border
    g.setColour(DarkTheme::getColour(DarkTheme::PANEL_BACKGROUND));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(DarkTheme::getBorderColour().brighter(0.2f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    // Title
    auto titleArea = getLocalBounds().reduced(10, 6).removeFromTop(20);
    g.setColour(DarkTheme::getTextColour());
    g.setFont(FontManager::getInstance().getUIFont(12.0f).boldened());
    g.drawText(juce::String(scale_.name), titleArea, juce::Justification::centredLeft);
}

void ScaleChordsPopup::resized() {
    auto area = getLocalBounds().reduced(10, 6);
    area.removeFromTop(24);  // title

    // Grid of chord blocks — single row if they fit, wrap otherwise
    int blockWidth = 70;
    int blockHeight = 36;
    int gap = 4;

    int x = area.getX();
    int y = area.getY();

    for (auto& block : chordBlocks_) {
        if (x + blockWidth > area.getRight()) {
            x = area.getX();
            y += blockHeight + gap;
        }
        block->setBounds(x, y, blockWidth, blockHeight);
        x += blockWidth + gap;
    }
}

void ScaleChordsPopup::mouseDown(const juce::MouseEvent& e) {
    // Close if clicking outside chord blocks
    bool clickedBlock = false;
    for (auto& block : chordBlocks_) {
        if (block->getBounds().contains(e.getPosition())) {
            clickedBlock = true;
            break;
        }
    }
    if (!clickedBlock)
        dismiss();
}

void ScaleChordsPopup::inputAttemptWhenModal() {
    dismiss();
}

void ScaleChordsPopup::dismiss() {
    exitModalState(0);
    setVisible(false);
    if (auto* parent = getParentComponent())
        parent->removeChildComponent(this);
}

void ScaleChordsPopup::showAt(juce::Component* parent, juce::Rectangle<int> targetArea) {
    // Calculate size based on chord count
    int numChords = static_cast<int>(chordBlocks_.size());
    int blockWidth = 70;
    int blockHeight = 36;
    int gap = 4;
    int cols = std::min(numChords, 7);
    int rows = (numChords + cols - 1) / cols;

    int popupWidth = cols * (blockWidth + gap) - gap + 20;
    int popupHeight = rows * (blockHeight + gap) - gap + 34;

    // Position above the target area
    auto screenTarget = parent->localAreaToGlobal(targetArea);
    int popupX = screenTarget.getX();
    int popupY = screenTarget.getY() - popupHeight - 4;

    // Keep on screen
    auto screenBounds = parent->getTopLevelComponent()->getScreenBounds();
    if (popupY < screenBounds.getY())
        popupY = screenTarget.getBottom() + 4;
    if (popupX + popupWidth > screenBounds.getRight())
        popupX = screenBounds.getRight() - popupWidth;

    auto screenRect = juce::Rectangle<int>(popupX, popupY, popupWidth, popupHeight);
    setBounds(parent->getTopLevelComponent()->getLocalArea(nullptr, screenRect));
    parent->getTopLevelComponent()->addAndMakeVisible(this);
    enterModalState(false, nullptr, false);  // lifetime managed by activePopup_
}

// ============================================================================
// ChordPanelContent
// ============================================================================

ChordPanelContent::ChordPanelContent() {
    setName("ChordPanel");
    setupFooterControls();
}

ChordPanelContent::~ChordPanelContent() {
    stopTimer();
}

void ChordPanelContent::setChordEngine(magda::daw::audio::MidiChordEnginePlugin* plugin) {
    if (chordPlugin_ == plugin)
        return;

    chordPlugin_ = plugin;
    currentChord_.clear();
    detectedKey_.clear();
    recentChords_.clear();
    suggestions_.clear();
    detectedScales_.clear();
    selectedScaleNames_.clear();
    suggestionBlocks_.clear();
    historyBlocks_.clear();
    scaleBlocks_.clear();

    if (plugin) {
        startTimerHz(15);
        syncFooterFromParams();
    } else {
        stopTimer();
    }

    repaint();
}

void ChordPanelContent::timerCallback() {
    if (!chordPlugin_)
        return;

    bool needsRepaint = false;
    bool needsLayout = false;

    // Current chord
    auto chord = chordPlugin_->getCurrentChordName();
    if (chord != currentChord_) {
        currentChord_ = chord;
        needsRepaint = true;
    }

    // Key/mode
    auto keyMode = chordPlugin_->getDetectedKeyMode();
    juce::String keyStr;
    if (keyMode.has_value())
        keyStr = keyMode->first + " " + keyMode->second;
    if (keyStr != detectedKey_) {
        detectedKey_ = keyStr;
        needsRepaint = true;
    }

    // Recent chords (last 5)
    auto history = chordPlugin_->getRecentChords();
    std::vector<juce::String> names;
    int start = std::max(0, static_cast<int>(history.size()) - 5);
    for (int i = start; i < static_cast<int>(history.size()); ++i)
        names.push_back(history[static_cast<size_t>(i)].getDisplayName());
    if (names != recentChords_) {
        recentChords_ = names;
        rebuildHistoryBlocks();
        needsLayout = true;
    }

    // Suggestions
    auto newSuggestions = chordPlugin_->getSuggestions();
    bool suggestionsChanged = newSuggestions.size() != suggestions_.size();
    if (!suggestionsChanged) {
        for (size_t i = 0; i < newSuggestions.size(); ++i) {
            if (newSuggestions[i].chord.getDisplayName() !=
                suggestions_[i].chord.getDisplayName()) {
                suggestionsChanged = true;
                break;
            }
        }
    }
    if (suggestionsChanged) {
        suggestions_ = newSuggestions;
        rebuildSuggestionBlocks();
        needsLayout = true;
    }

    // Detected scales (top 5)
    auto newScales = chordPlugin_->getDetectedScales(5);
    bool scalesChanged = newScales.size() != detectedScales_.size();
    if (!scalesChanged) {
        for (size_t i = 0; i < newScales.size(); ++i) {
            if (newScales[i].name != detectedScales_[i].name) {
                scalesChanged = true;
                break;
            }
        }
    }
    if (scalesChanged) {
        detectedScales_ = newScales;
        rebuildScaleBlocks();
        needsLayout = true;
    }

    if (needsLayout)
        resized();
    else if (needsRepaint)
        repaint();
}

void ChordPanelContent::rebuildSuggestionBlocks() {
    for (auto& block : suggestionBlocks_)
        removeChildComponent(block.get());
    suggestionBlocks_.clear();

    for (const auto& item : suggestions_) {
        auto block = std::make_unique<ChordBlockComponent>(item.chord);
        block->setDegreeLabel(item.degree);
        addAndMakeVisible(block.get());
        suggestionBlocks_.push_back(std::move(block));
    }
}

void ChordPanelContent::rebuildHistoryBlocks() {
    for (auto& block : historyBlocks_)
        removeChildComponent(block.get());
    historyBlocks_.clear();

    if (!chordPlugin_)
        return;

    auto history = chordPlugin_->getRecentChords();
    int start = std::max(0, static_cast<int>(history.size()) - 5);
    for (int i = start; i < static_cast<int>(history.size()); ++i) {
        auto block = std::make_unique<ChordBlockComponent>(history[static_cast<size_t>(i)]);
        addAndMakeVisible(block.get());
        historyBlocks_.push_back(std::move(block));
    }
}

void ChordPanelContent::rebuildScaleBlocks() {
    for (auto& block : scaleBlocks_)
        removeChildComponent(block.get());
    scaleBlocks_.clear();

    // Prune stale selections (scales that are no longer detected)
    std::set<std::string> validNames;
    for (const auto& scale : detectedScales_)
        validNames.insert(scale.name);
    for (auto it = selectedScaleNames_.begin(); it != selectedScaleNames_.end();) {
        if (validNames.find(*it) == validNames.end())
            it = selectedScaleNames_.erase(it);
        else
            ++it;
    }

    for (const auto& scale : detectedScales_) {
        auto block = std::make_unique<ScaleBlockComponent>(scale);
        // Restore selection state
        if (selectedScaleNames_.count(scale.name))
            block->setSelected(true);
        addAndMakeVisible(block.get());
        scaleBlocks_.push_back(std::move(block));
    }

    updateScaleFilterPitchClasses();
}

void ChordPanelContent::toggleScaleSelection(ScaleBlockComponent* block) {
    const auto& name = block->getScale().name;
    bool nowSelected = !block->isSelected();
    block->setSelected(nowSelected);

    if (nowSelected)
        selectedScaleNames_.insert(name);
    else
        selectedScaleNames_.erase(name);

    updateScaleFilterPitchClasses();
}

void ChordPanelContent::updateScaleFilterPitchClasses() {
    if (!chordPlugin_)
        return;

    auto& params = chordPlugin_->getSuggestionParams();
    params.explicitScalePitchClasses.clear();

    // Merge pitch classes from all selected scales
    for (const auto& scale : detectedScales_) {
        if (selectedScaleNames_.count(scale.name)) {
            for (int pitch : scale.pitches)
                params.explicitScalePitchClasses.insert(pitch % 12);
        }
    }
}

void ChordPanelContent::showScalePopup(const magda::music::ScaleWithChords& scale,
                                       juce::Component* source) {
    // If popup is already showing for this scale, toggle it off
    if (activePopup_ && activePopup_->getScaleName() == scale.name) {
        activePopup_.reset();
        return;
    }

    // Dismiss any existing popup before creating a new one
    activePopup_.reset();

    auto popup = std::make_unique<ScaleChordsPopup>(scale);
    auto* popupPtr = popup.get();
    activePopup_ = std::move(popup);
    popupPtr->showAt(this, source->getBounds());
}

void ChordPanelContent::setupFooterControls() {
    // Novelty slider (0-100%)
    noveltyLabel_ = std::make_unique<magda::DraggableValueLabel>(
        magda::DraggableValueLabel::Format::Percentage);
    noveltyLabel_->setRange(0.0, 1.0, 0.3);
    noveltyLabel_->setValue(0.3, juce::dontSendNotification);
    noveltyLabel_->setFontSize(9.0f);
    noveltyLabel_->setDrawBackground(false);
    noveltyLabel_->setDrawBorder(false);
    noveltyLabel_->setShowFillIndicator(false);
    noveltyLabel_->setTextOverride("Nov 30%");
    noveltyLabel_->onValueChange = [this]() {
        if (chordPlugin_) {
            chordPlugin_->getSuggestionParams().novelty =
                static_cast<float>(noveltyLabel_->getValue());
            int pct = static_cast<int>(noveltyLabel_->getValue() * 100.0);
            noveltyLabel_->setTextOverride("Nov " + juce::String(pct) + "%");
        }
    };
    addAndMakeVisible(noveltyLabel_.get());

    // Toggle button helper
    auto makeToggle = [this](const juce::String& text) {
        auto btn = std::make_unique<juce::TextButton>(text);
        btn->setClickingTogglesState(true);
        btn->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        btn->setColour(juce::TextButton::buttonOnColourId,
                       DarkTheme::getAccentColour().withAlpha(0.25f));
        btn->setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
        btn->setColour(juce::TextButton::textColourOnId, DarkTheme::getAccentColour());
        addAndMakeVisible(btn.get());
        return btn;
    };

    add7thsBtn_ = makeToggle("7th");
    add7thsBtn_->setToggleState(true, juce::dontSendNotification);
    add7thsBtn_->onClick = [this]() {
        if (chordPlugin_)
            chordPlugin_->getSuggestionParams().add7ths = add7thsBtn_->getToggleState();
    };

    add9thsBtn_ = makeToggle("9th");
    add9thsBtn_->onClick = [this]() {
        if (chordPlugin_)
            chordPlugin_->getSuggestionParams().add9ths = add9thsBtn_->getToggleState();
    };

    addAltBtn_ = makeToggle("Alt");
    addAltBtn_->onClick = [this]() {
        if (chordPlugin_)
            chordPlugin_->getSuggestionParams().addAlterations = addAltBtn_->getToggleState();
    };

    scaleFilterBtn_ = makeToggle("Scale");
    scaleFilterBtn_->setToggleState(true, juce::dontSendNotification);
    scaleFilterBtn_->onClick = [this]() {
        if (chordPlugin_)
            chordPlugin_->getSuggestionParams().useScaleFiltering =
                scaleFilterBtn_->getToggleState();
    };

    explorerBtn_ = std::make_unique<juce::TextButton>("Browse");
    explorerBtn_->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    explorerBtn_->setColour(juce::TextButton::textColourOffId, DarkTheme::getSecondaryTextColour());
    addAndMakeVisible(explorerBtn_.get());
    // Explorer click handler — TODO: open chord/scale browser
}

void ChordPanelContent::syncFooterFromParams() {
    if (!chordPlugin_)
        return;

    const auto& params = chordPlugin_->getSuggestionParams();
    noveltyLabel_->setValue(params.novelty, juce::dontSendNotification);
    int pct = static_cast<int>(params.novelty * 100.0f);
    noveltyLabel_->setTextOverride("Nov " + juce::String(pct) + "%");
    add7thsBtn_->setToggleState(params.add7ths, juce::dontSendNotification);
    add9thsBtn_->setToggleState(params.add9ths, juce::dontSendNotification);
    addAltBtn_->setToggleState(params.addAlterations, juce::dontSendNotification);
    scaleFilterBtn_->setToggleState(params.useScaleFiltering, juce::dontSendNotification);
}

void ChordPanelContent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background
    g.setColour(DarkTheme::getBackgroundColour());
    g.fillRect(bounds);

    // Left border
    g.setColour(DarkTheme::getBorderColour());
    g.fillRect(bounds.getX(), bounds.getY(), 1, bounds.getHeight());

    // Placeholder when no plugin connected
    if (!chordPlugin_) {
        g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
        g.setFont(FontManager::getInstance().getUIFont(12.0f));
        g.drawText("No MIDI device", bounds, juce::Justification::centred);
        return;
    }

    auto headerFont = FontManager::getInstance().getUIFont(10.0f);

    // --- Column 1: Detection ---
    if (detectionCol_.getWidth() > 0) {
        auto col = detectionCol_;
        auto area = col.reduced(PADDING, 0);

        // "CHORD" header
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(headerFont);
        g.drawText("CHORD", area.removeFromTop(SECTION_HEADER_HEIGHT),
                   juce::Justification::centredLeft);

        // Current chord display box
        auto chordBox = area.removeFromTop(44);
        g.setColour(DarkTheme::getBackgroundColour().brighter(0.06f));
        g.fillRoundedRectangle(chordBox.toFloat(), 4.0f);
        g.setColour(DarkTheme::getBorderColour());
        g.drawRoundedRectangle(chordBox.toFloat(), 4.0f, 1.0f);

        if (currentChord_.isEmpty()) {
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.4f));
            g.setFont(FontManager::getInstance().getUIFont(13.0f));
            g.drawText("Play...", chordBox, juce::Justification::centred);
        } else {
            g.setColour(DarkTheme::getAccentColour());
            g.setFont(FontManager::getInstance().getUIFont(20.0f).boldened());
            g.drawText(currentChord_, chordBox, juce::Justification::centred);
        }

        area.removeFromTop(8);

        // "HISTORY" header
        if (!historyBlocks_.empty()) {
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(headerFont);
            g.drawText("HISTORY", area.removeFromTop(SECTION_HEADER_HEIGHT),
                       juce::Justification::centredLeft);
        }
    }

    // --- Column 2: Suggestions header ---
    if (suggestionsCol_.getWidth() > 0) {
        auto col = suggestionsCol_;

        // Column separator
        g.setColour(DarkTheme::getBorderColour());
        g.fillRect(col.getX(), col.getY() + 4, 1, col.getHeight() - 8);

        auto area = col.reduced(PADDING, 0);

        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(headerFont);
        g.drawText("SUGGESTIONS", area.removeFromTop(SECTION_HEADER_HEIGHT),
                   juce::Justification::centredLeft);

        if (suggestionBlocks_.empty() && chordPlugin_) {
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            auto emptyArea = area;
            emptyArea.removeFromTop(8);
            g.drawText("Play to get suggestions", emptyArea.removeFromTop(20),
                       juce::Justification::centredLeft);
        }
    }

    // --- Column 3: Key / Scale ---
    if (keyScaleCol_.getWidth() > 0) {
        auto col = keyScaleCol_;

        // Column separator
        g.setColour(DarkTheme::getBorderColour());
        g.fillRect(col.getX(), col.getY() + 4, 1, col.getHeight() - 8);

        auto area = col.reduced(PADDING, 0);

        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(headerFont);
        g.drawText("KEY", area.removeFromTop(SECTION_HEADER_HEIGHT),
                   juce::Justification::centredLeft);

        if (detectedKey_.isNotEmpty()) {
            area.removeFromTop(4);
            g.setColour(DarkTheme::getTextColour());
            g.setFont(FontManager::getInstance().getUIFont(16.0f).boldened());
            g.drawText(detectedKey_, area.removeFromTop(24), juce::Justification::centredLeft);
        } else {
            area.removeFromTop(4);
            g.setColour(DarkTheme::getSecondaryTextColour().withAlpha(0.3f));
            g.setFont(FontManager::getInstance().getUIFont(11.0f));
            g.drawText("Detecting...", area.removeFromTop(20), juce::Justification::centredLeft);
        }

        area.removeFromTop(8);

        // "SCALES" header
        if (!scaleBlocks_.empty()) {
            g.setColour(DarkTheme::getSecondaryTextColour());
            g.setFont(headerFont);
            g.drawText("SCALES", area.removeFromTop(SECTION_HEADER_HEIGHT),
                       juce::Justification::centredLeft);
            // Scale blocks positioned in resized()
        }
    }

    // --- Footer separator line ---
    if (chordPlugin_) {
        int footerY = getHeight() - FOOTER_HEIGHT;
        g.setColour(DarkTheme::getBorderColour());
        g.fillRect(0, footerY, getWidth(), 1);
    }
}

void ChordPanelContent::resized() {
    auto bounds = getLocalBounds();

    // Reserve footer area
    auto footerArea = bounds.removeFromBottom(FOOTER_HEIGHT);

    // Layout footer controls
    {
        auto footer = footerArea.reduced(PADDING, 0);
        footer.removeFromTop(1);  // separator line

        // Left: space for collapse button (managed by BottomPanel)
        footer.removeFromLeft(24);

        // Right: explorer button
        explorerBtn_->setBounds(footer.removeFromRight(50).reduced(0, 2));
        footer.removeFromRight(4);

        // Middle: novelty + toggle buttons
        noveltyLabel_->setBounds(footer.removeFromLeft(52).reduced(0, 2));
        footer.removeFromLeft(4);
        add7thsBtn_->setBounds(footer.removeFromLeft(32).reduced(0, 2));
        footer.removeFromLeft(2);
        add9thsBtn_->setBounds(footer.removeFromLeft(32).reduced(0, 2));
        footer.removeFromLeft(2);
        addAltBtn_->setBounds(footer.removeFromLeft(32).reduced(0, 2));
        footer.removeFromLeft(2);
        scaleFilterBtn_->setBounds(footer.removeFromLeft(42).reduced(0, 2));
    }

    // 3-column split: detection (28%) | suggestions (50%) | key/scale (22%)
    auto totalWidth = bounds.getWidth();
    int detectionWidth = static_cast<int>(totalWidth * 0.28f);
    int keyScaleWidth = static_cast<int>(totalWidth * 0.22f);

    detectionCol_ = bounds.removeFromLeft(detectionWidth);
    bounds.removeFromLeft(COLUMN_GAP);
    keyScaleCol_ = bounds.removeFromRight(keyScaleWidth);
    bounds.removeFromRight(COLUMN_GAP);
    suggestionsCol_ = bounds;

    // Position history blocks in detection column
    {
        auto area = detectionCol_.reduced(PADDING, 0);
        area.removeFromTop(SECTION_HEADER_HEIGHT);  // "CHORD" header
        area.removeFromTop(44);                     // chord display box
        area.removeFromTop(8);                      // gap

        if (!historyBlocks_.empty()) {
            area.removeFromTop(SECTION_HEADER_HEIGHT);  // "HISTORY" header
            area.removeFromTop(2);

            int x = area.getX();
            int y = area.getY();
            int blockWidth = std::max(50, (area.getWidth() - BLOCK_GAP) / 2);

            for (auto& block : historyBlocks_) {
                if (x + blockWidth > area.getRight()) {
                    x = area.getX();
                    y += BLOCK_HEIGHT + BLOCK_GAP;
                }
                if (y + BLOCK_HEIGHT > area.getBottom())
                    break;
                block->setBounds(x, y, blockWidth, BLOCK_HEIGHT);
                x += blockWidth + BLOCK_GAP;
            }
        }
    }

    // Position suggestion blocks in suggestions column (grid flow)
    {
        auto area = suggestionsCol_.reduced(PADDING, 0);
        area.removeFromTop(SECTION_HEADER_HEIGHT);  // "SUGGESTIONS" header
        area.removeFromTop(2);

        int numCols = area.getWidth() > 280 ? 3 : 2;
        int blockWidth = (area.getWidth() - BLOCK_GAP * (numCols - 1)) / numCols;
        int x = area.getX();
        int y = area.getY();

        for (auto& block : suggestionBlocks_) {
            if (x + blockWidth > area.getRight() + 1) {
                x = area.getX();
                y += BLOCK_HEIGHT + BLOCK_GAP;
            }
            if (y + BLOCK_HEIGHT > area.getBottom())
                break;
            block->setBounds(x, y, blockWidth, BLOCK_HEIGHT);
            x += blockWidth + BLOCK_GAP;
        }
    }

    // Position scale blocks in key/scale column
    {
        auto area = keyScaleCol_.reduced(PADDING, 0);
        area.removeFromTop(SECTION_HEADER_HEIGHT);  // "KEY" header

        if (detectedKey_.isNotEmpty()) {
            area.removeFromTop(4);   // gap
            area.removeFromTop(24);  // key text
        } else {
            area.removeFromTop(4);
            area.removeFromTop(20);
        }

        area.removeFromTop(8);  // gap before SCALES

        if (!scaleBlocks_.empty()) {
            area.removeFromTop(SECTION_HEADER_HEIGHT);  // "SCALES" header
            area.removeFromTop(2);

            int scaleBlockHeight = 26;
            for (auto& block : scaleBlocks_) {
                if (area.getHeight() < scaleBlockHeight)
                    break;
                block->setBounds(area.removeFromTop(scaleBlockHeight));
                area.removeFromTop(BLOCK_GAP);
            }
        }
    }
}

}  // namespace magda::daw::ui
