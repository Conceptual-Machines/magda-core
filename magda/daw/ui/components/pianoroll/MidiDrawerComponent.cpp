#include "MidiDrawerComponent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "CCLaneComponent.hpp"
#include "VelocityLaneComponent.hpp"

namespace magda {

MidiDrawerComponent::MidiDrawerComponent() {
    setName("MidiDrawer");

    // Create the permanent velocity lane
    velocityLane_ = std::make_unique<VelocityLaneComponent>();
    velocityLane_->setLeftPadding(leftPadding_);
    addAndMakeVisible(velocityLane_.get());

    // Pitch bend range editor (hidden by default)
    pbRangeLabel_ = std::make_unique<juce::Label>("pbRange", "2");
    pbRangeLabel_->setEditable(true);
    pbRangeLabel_->setJustificationType(juce::Justification::centred);
    pbRangeLabel_->setFont(FontManager::getInstance().getUIFont(10.0f));
    pbRangeLabel_->setColour(juce::Label::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    pbRangeLabel_->setColour(juce::Label::backgroundColourId, juce::Colour(0x00000000));
    pbRangeLabel_->setColour(juce::Label::outlineColourId, juce::Colour(0x00000000));
    pbRangeLabel_->setColour(juce::TextEditor::backgroundColourId,
                             DarkTheme::getColour(DarkTheme::BACKGROUND));
    pbRangeLabel_->setColour(juce::TextEditor::textColourId,
                             DarkTheme::getColour(DarkTheme::TEXT_PRIMARY));
    pbRangeLabel_->setTooltip("Pitch bend range (semitones)");
    pbRangeLabel_->onTextChange = [this]() {
        int range = pbRangeLabel_->getText().getIntValue();
        range = juce::jlimit(1, 96, range);
        pbRangeLabel_->setText(juce::String(range), juce::dontSendNotification);

        // Apply to active pitch bend lane
        int ccIdx = activeTabIndex_ - 1;
        if (ccIdx >= 0 && ccIdx < static_cast<int>(ccTabs_.size())) {
            if (ccTabs_[ccIdx].isPitchBend && ccTabs_[ccIdx].ccLane)
                ccTabs_[ccIdx].ccLane->setPitchBendRange(range);
        }
    };
    addChildComponent(pbRangeLabel_.get());
}

MidiDrawerComponent::~MidiDrawerComponent() = default;

// ============================================================================
// Settings forwarding
// ============================================================================

void MidiDrawerComponent::setClip(ClipId clipId) {
    clipId_ = clipId;
    velocityLane_->setClip(clipId);
    for (auto& tab : ccTabs_) {
        if (tab.ccLane)
            tab.ccLane->setClip(clipId);
    }
}

void MidiDrawerComponent::setClipIds(const std::vector<ClipId>& clipIds) {
    clipIds_ = clipIds;
    velocityLane_->setClipIds(clipIds);
}

void MidiDrawerComponent::setPixelsPerBeat(double ppb) {
    pixelsPerBeat_ = ppb;
    velocityLane_->setPixelsPerBeat(ppb);
    for (auto& tab : ccTabs_) {
        if (tab.ccLane)
            tab.ccLane->setPixelsPerBeat(ppb);
    }
}

void MidiDrawerComponent::setScrollOffset(int offsetX) {
    scrollOffsetX_ = offsetX;
    velocityLane_->setScrollOffset(offsetX);
    for (auto& tab : ccTabs_) {
        if (tab.ccLane)
            tab.ccLane->setScrollOffset(offsetX);
    }
}

void MidiDrawerComponent::setLeftPadding(int padding) {
    leftPadding_ = padding;
    velocityLane_->setLeftPadding(padding);
    for (auto& tab : ccTabs_) {
        if (tab.ccLane)
            tab.ccLane->setLeftPadding(padding);
    }
}

void MidiDrawerComponent::setRelativeMode(bool relative) {
    relativeMode_ = relative;
    velocityLane_->setRelativeMode(relative);
    for (auto& tab : ccTabs_) {
        if (tab.ccLane)
            tab.ccLane->setRelativeMode(relative);
    }
}

void MidiDrawerComponent::setClipStartBeats(double startBeats) {
    clipStartBeats_ = startBeats;
    velocityLane_->setClipStartBeats(startBeats);
    for (auto& tab : ccTabs_) {
        if (tab.ccLane)
            tab.ccLane->setClipStartBeats(startBeats);
    }
}

void MidiDrawerComponent::setClipLengthBeats(double lengthBeats) {
    clipLengthBeats_ = lengthBeats;
    velocityLane_->setClipLengthBeats(lengthBeats);
    for (auto& tab : ccTabs_) {
        if (tab.ccLane)
            tab.ccLane->setClipLengthBeats(lengthBeats);
    }
}

void MidiDrawerComponent::setLoopRegion(double offsetBeats, double lengthBeats, bool enabled) {
    loopOffsetBeats_ = offsetBeats;
    loopLengthBeats_ = lengthBeats;
    loopEnabled_ = enabled;
    velocityLane_->setLoopRegion(offsetBeats, lengthBeats, enabled);
    for (auto& tab : ccTabs_) {
        if (tab.ccLane)
            tab.ccLane->setLoopRegion(offsetBeats, lengthBeats, enabled);
    }
}

void MidiDrawerComponent::refreshAll() {
    velocityLane_->refreshNotes();
    for (auto& tab : ccTabs_) {
        if (tab.ccLane)
            tab.ccLane->refreshEvents();
    }
}

juce::String MidiDrawerComponent::getActiveTabName() const {
    if (activeTabIndex_ == 0)
        return "Velocity";

    int ccIdx = activeTabIndex_ - 1;
    if (ccIdx >= 0 && ccIdx < static_cast<int>(ccTabs_.size())) {
        if (ccTabs_[ccIdx].ccLane)
            return ccTabs_[ccIdx].ccLane->getLaneName();
    }
    return "Velocity";
}

// ============================================================================
// Layout
// ============================================================================

void MidiDrawerComponent::resized() {
    auto bounds = getLocalBounds();

    // Left margin (keyboard column area)
    bounds.removeFromLeft(leftMargin_);

    // Resize handle + Tab bar at top (painted in paint())
    bounds.removeFromTop(RESIZE_HANDLE_HEIGHT + TAB_BAR_HEIGHT);

    // Active lane fills the rest
    velocityLane_->setBounds(bounds);
    velocityLane_->setVisible(activeTabIndex_ == 0);

    for (size_t i = 0; i < ccTabs_.size(); ++i) {
        if (ccTabs_[i].ccLane) {
            ccTabs_[i].ccLane->setBounds(bounds);
            ccTabs_[i].ccLane->setVisible(static_cast<int>(i) == activeTabIndex_ - 1);
        }
    }

    updatePbRangeVisibility();
}

void MidiDrawerComponent::paint(juce::Graphics& g) {
    auto fullBounds = getLocalBounds();

    // Left margin background
    if (leftMargin_ > 0) {
        auto leftArea = fullBounds.removeFromLeft(leftMargin_);
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
        g.fillRect(leftArea);

        // Top border across full width
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
        g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));

        // Right edge of left column
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
        g.drawVerticalLine(leftMargin_ - 1, 0.0f, static_cast<float>(getHeight()));
    }

    // Resize handle at top edge (in the main area)
    auto handleArea = fullBounds.removeFromTop(RESIZE_HANDLE_HEIGHT);
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.fillRect(handleArea.removeFromTop(1));

    auto tabBarArea = fullBounds.removeFromTop(TAB_BAR_HEIGHT);
    paintTabBar(g, tabBarArea);

    // "Range" title above the PB range input
    if (pbRangeLabel_->isVisible() && leftMargin_ > 4) {
        auto labelBounds = pbRangeLabel_->getBounds();
        g.setFont(FontManager::getInstance().getUIFont(9.0f));
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        g.drawText("Range", 2, juce::jmax(0, labelBounds.getY() - 14), leftMargin_ - 4, 12,
                   juce::Justification::centred, false);
    }
}

void MidiDrawerComponent::paintTabBar(juce::Graphics& g, juce::Rectangle<int> area) {
    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(area);

    // Top border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(area.getY(), static_cast<float>(area.getX()),
                         static_cast<float>(area.getRight()));

    auto font = FontManager::getInstance().getUIFont(11.0f);
    g.setFont(font);

    int x = area.getX() + 6;
    int tabHeight = area.getHeight() - 4;
    int tabY = area.getY() + 2;

    // "Vel" tab (always present)
    {
        int tabWidth = static_cast<int>(font.getStringWidthFloat("Vel")) + 18;
        bool isActive = (activeTabIndex_ == 0);

        if (isActive) {
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).withAlpha(0.8f));
            g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(tabY),
                                   static_cast<float>(tabWidth), static_cast<float>(tabHeight),
                                   3.0f);
        }

        g.setColour(isActive ? DarkTheme::getColour(DarkTheme::TEXT_PRIMARY)
                             : DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        g.drawText("Vel", x, tabY, tabWidth, tabHeight, juce::Justification::centred, true);
        x += tabWidth + 4;
    }

    // CC/PB tabs
    for (size_t i = 0; i < ccTabs_.size(); ++i) {
        juce::String tabName = ccTabs_[i].name;
        int tabWidth =
            static_cast<int>(font.getStringWidthFloat(tabName)) + 28;  // extra for close btn
        bool isActive = (static_cast<int>(i) + 1 == activeTabIndex_);

        if (isActive) {
            g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND).withAlpha(0.8f));
            g.fillRoundedRectangle(static_cast<float>(x), static_cast<float>(tabY),
                                   static_cast<float>(tabWidth), static_cast<float>(tabHeight),
                                   3.0f);
        }

        g.setColour(isActive ? DarkTheme::getColour(DarkTheme::TEXT_PRIMARY)
                             : DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
        g.drawText(tabName, x, tabY, tabWidth - 14, tabHeight, juce::Justification::centred, true);

        // Close button "x"
        g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.6f));
        int closeX = x + tabWidth - 14;
        int closeY = tabY + tabHeight / 2;
        g.drawLine(static_cast<float>(closeX - 3), static_cast<float>(closeY - 3),
                   static_cast<float>(closeX + 3), static_cast<float>(closeY + 3), 1.0f);
        g.drawLine(static_cast<float>(closeX + 3), static_cast<float>(closeY - 3),
                   static_cast<float>(closeX - 3), static_cast<float>(closeY + 3), 1.0f);

        x += tabWidth + 4;
    }

    // "+" button
    int plusSize = tabHeight;
    g.setColour(DarkTheme::getColour(DarkTheme::TEXT_SECONDARY));
    g.drawText("+", x, tabY, plusSize, tabHeight, juce::Justification::centred, true);
}

// ============================================================================
// Mouse handling for tab bar
// ============================================================================

void MidiDrawerComponent::mouseMove(const juce::MouseEvent& e) {
    if (e.y < RESIZE_HANDLE_HEIGHT)
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MidiDrawerComponent::mouseDrag(const juce::MouseEvent& e) {
    if (isResizing_ && onResizeDrag) {
        int newHeight = resizeStartHeight_ - e.getDistanceFromDragStartY();
        onResizeDrag(newHeight);
    }
}

void MidiDrawerComponent::mouseUp(const juce::MouseEvent&) {
    isResizing_ = false;
}

void MidiDrawerComponent::mouseDown(const juce::MouseEvent& e) {
    // Resize handle at the top edge (across full width)
    if (e.y < RESIZE_HANDLE_HEIGHT) {
        isResizing_ = true;
        resizeStartHeight_ = getHeight();
        return;
    }

    // Only handle clicks in the tab bar area (right of left margin)
    if (e.x < leftMargin_ || e.y < RESIZE_HANDLE_HEIGHT ||
        e.y >= RESIZE_HANDLE_HEIGHT + TAB_BAR_HEIGHT)
        return;

    auto font = FontManager::getInstance().getUIFont(11.0f);
    int x = leftMargin_ + 6;
    int tabHeight = TAB_BAR_HEIGHT - 4;

    // Check "Vel" tab
    {
        int tabWidth = static_cast<int>(font.getStringWidthFloat("Vel")) + 18;
        if (e.x >= x && e.x < x + tabWidth) {
            setActiveTab(0);
            return;
        }
        x += tabWidth + 4;
    }

    // Check CC/PB tabs
    for (size_t i = 0; i < ccTabs_.size(); ++i) {
        juce::String tabName = ccTabs_[i].name;
        int tabWidth = static_cast<int>(font.getStringWidthFloat(tabName)) + 28;

        if (e.x >= x && e.x < x + tabWidth) {
            // Check if close button was clicked
            int closeX = x + tabWidth - 14;
            if (e.x >= closeX - 6 && e.x <= closeX + 6) {
                removeTab(static_cast<int>(i) + 1);
                return;
            }

            setActiveTab(static_cast<int>(i) + 1);
            return;
        }
        x += tabWidth + 4;
    }

    // Check "+" button
    int plusSize = tabHeight;
    if (e.x >= x && e.x < x + plusSize) {
        showAddTabMenu();
        return;
    }
}

// ============================================================================
// Tab management
// ============================================================================

void MidiDrawerComponent::syncSettingsToCCLane(CCLaneComponent* lane) {
    lane->setClip(clipId_);
    lane->setPixelsPerBeat(pixelsPerBeat_);
    lane->setScrollOffset(scrollOffsetX_);
    lane->setLeftPadding(leftPadding_);
    lane->setRelativeMode(relativeMode_);
    lane->setClipStartBeats(clipStartBeats_);
    lane->setClipLengthBeats(clipLengthBeats_);
    lane->setLoopRegion(loopOffsetBeats_, loopLengthBeats_, loopEnabled_);
    // Undo commands are handled internally by CCLaneComponent (CurveEditorBase subclass)
}

void MidiDrawerComponent::addCCTab(int ccNumber) {
    // Check if tab already exists
    for (const auto& tab : ccTabs_) {
        if (!tab.isPitchBend && tab.ccNumber == ccNumber)
            return;
    }

    TabInfo tab;
    tab.isPitchBend = false;
    tab.ccNumber = ccNumber;
    tab.ccLane = std::make_unique<CCLaneComponent>();
    tab.ccLane->setCCNumber(ccNumber);
    tab.ccLane->setIsPitchBend(false);
    tab.name = tab.ccLane->getLaneName();

    syncSettingsToCCLane(tab.ccLane.get());
    addChildComponent(tab.ccLane.get());

    ccTabs_.push_back(std::move(tab));
    setActiveTab(static_cast<int>(ccTabs_.size()));  // Switch to new tab
    resized();
    repaint();
}

void MidiDrawerComponent::addPitchBendTab() {
    // Check if tab already exists
    for (const auto& tab : ccTabs_) {
        if (tab.isPitchBend)
            return;
    }

    TabInfo tab;
    tab.isPitchBend = true;
    tab.ccNumber = -1;
    tab.ccLane = std::make_unique<CCLaneComponent>();
    tab.ccLane->setIsPitchBend(true);
    tab.name = "Pitch";

    syncSettingsToCCLane(tab.ccLane.get());
    addChildComponent(tab.ccLane.get());

    ccTabs_.push_back(std::move(tab));
    setActiveTab(static_cast<int>(ccTabs_.size()));
    resized();
    repaint();
}

void MidiDrawerComponent::removeTab(int tabIndex) {
    if (tabIndex <= 0 || tabIndex > static_cast<int>(ccTabs_.size()))
        return;  // Can't remove velocity tab

    int ccIdx = tabIndex - 1;
    removeChildComponent(ccTabs_[ccIdx].ccLane.get());
    ccTabs_.erase(ccTabs_.begin() + ccIdx);

    // Adjust active tab
    if (activeTabIndex_ >= tabIndex) {
        activeTabIndex_ = juce::jmax(0, activeTabIndex_ - 1);
    }
    updateLaneVisibility();
    resized();
    repaint();
}

void MidiDrawerComponent::setActiveTab(int tabIndex) {
    if (tabIndex == activeTabIndex_)
        return;

    activeTabIndex_ = tabIndex;
    updateLaneVisibility();
    updatePbRangeVisibility();
    repaint();
}

void MidiDrawerComponent::updateLaneVisibility() {
    velocityLane_->setVisible(activeTabIndex_ == 0);
    for (size_t i = 0; i < ccTabs_.size(); ++i) {
        if (ccTabs_[i].ccLane) {
            ccTabs_[i].ccLane->setVisible(static_cast<int>(i) == activeTabIndex_ - 1);
        }
    }
}

void MidiDrawerComponent::updatePbRangeVisibility() {
    bool showPbRange = false;
    int ccIdx = activeTabIndex_ - 1;
    if (ccIdx >= 0 && ccIdx < static_cast<int>(ccTabs_.size())) {
        showPbRange = ccTabs_[ccIdx].isPitchBend;
        // Sync label text from the lane's current range
        if (showPbRange && ccTabs_[ccIdx].ccLane) {
            pbRangeLabel_->setText(juce::String(ccTabs_[ccIdx].ccLane->getPitchBendRange()),
                                   juce::dontSendNotification);
        }
    }

    pbRangeLabel_->setVisible(showPbRange);
    if (showPbRange && leftMargin_ > 0) {
        // Position in the left column, vertically centered (with space for "Range" label above)
        int labelW = leftMargin_ - 4;
        int labelH = 18;
        int titleH = 12;
        int totalH = titleH + 2 + labelH;
        int topOffset = RESIZE_HANDLE_HEIGHT + TAB_BAR_HEIGHT;
        int laneHeight = getHeight() - topOffset;
        int groupY = topOffset + (laneHeight - totalH) / 2;
        pbRangeLabel_->setBounds(2, groupY + titleH + 2, labelW, labelH);
        pbRangeLabel_->toFront(false);
    }
}

void MidiDrawerComponent::showAddTabMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "Pitchbend");
    menu.addSeparator();
    menu.addItem(2, "CC 1 (Mod Wheel)");
    menu.addItem(3, "CC 7 (Volume)");
    menu.addItem(4, "CC 11 (Expression)");
    menu.addItem(5, "CC 64 (Sustain)");
    menu.addSeparator();
    menu.addItem(100, "Custom CC...");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int result) {
        switch (result) {
            case 1:
                addPitchBendTab();
                break;
            case 2:
                addCCTab(1);
                break;
            case 3:
                addCCTab(7);
                break;
            case 4:
                addCCTab(11);
                break;
            case 5:
                addCCTab(64);
                break;
            case 100: {
                // Show dialog for custom CC number
                auto* alert = new juce::AlertWindow("Custom CC", "Enter CC number (0-127):",
                                                    juce::MessageBoxIconType::QuestionIcon);
                alert->addTextEditor("cc", "1", "CC Number:");
                alert->addButton("OK", 1);
                alert->addButton("Cancel", 0);
                // enterModalState with deleteWhenDismissed=true handles cleanup
                alert->enterModalState(
                    true, juce::ModalCallbackFunction::create([this, alert](int result) {
                        if (result == 1) {
                            int cc = alert->getTextEditorContents("cc").getIntValue();
                            cc = juce::jlimit(0, 127, cc);
                            addCCTab(cc);
                        }
                    }),
                    true);
                break;
            }
            default:
                break;
        }
    });
}

}  // namespace magda
