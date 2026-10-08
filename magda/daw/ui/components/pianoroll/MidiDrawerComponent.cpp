#include "MidiDrawerComponent.hpp"

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../common/SvgButton.hpp"
#include "BinaryData.h"
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
                             ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    pbRangeLabel_->setColour(juce::Label::backgroundColourId, juce::Colour(0x00000000));
    pbRangeLabel_->setColour(juce::Label::outlineColourId, juce::Colour(0x00000000));
    pbRangeLabel_->setColour(juce::TextEditor::backgroundColourId,
                             ActiveTheme::getColour(ActiveTheme::BACKGROUND));
    pbRangeLabel_->setColour(juce::TextEditor::textColourId,
                             ActiveTheme::getColour(ActiveTheme::TEXT_PRIMARY));
    pbRangeLabel_->setTooltip("Pitch bend range (semitones)");
    pbRangeLabel_->onTextChange = [this]() {
        int range = pbRangeLabel_->getText().getIntValue();
        range = juce::jlimit(1, 96, range);
        pbRangeLabel_->setText(juce::String(range), juce::dontSendNotification);

        // Apply to the pitch bend lane
        for (auto& tab : ccTabs_) {
            if (tab.isPitchBend && tab.ccLane)
                tab.ccLane->setPitchBendRange(range);
        }
    };
    addChildComponent(pbRangeLabel_.get());

    velocityCaption_ = juce::String(juce::CharPointer_UTF8("VEL \xc2\xb7 0\xe2\x80\x93"
                                                           "127"));

    const std::array<std::tuple<const char*, int, const char*>, 3> pickers{{
        {BinaryData::mevelocity_svg, BinaryData::mevelocity_svgSize, "Velocity"},
        {BinaryData::mecc_svg, BinaryData::mecc_svgSize, "CC and pitchbend lanes"},
        {BinaryData::mempe_svg, BinaryData::mempe_svgSize, "MPE: per-note pitch glide"},
    }};
    for (size_t i = 0; i < laneButtons_.size(); ++i) {
        const auto& [svg, size, tip] = pickers[i];
        auto button = std::make_unique<SvgButton>(tip, svg, static_cast<size_t>(size));
        button->setTooltip(tip);
        button->setOriginalColor(juce::Colour(0xFFB3B3B3));
        button->setNormalColor(ActiveTheme::TEXT_SECONDARY);
        button->setHoverColor(ActiveTheme::TEXT_PRIMARY);
        button->setActiveColor(ActiveTheme::TEXT_PRIMARY);
        button->setActiveBackgroundColor(ActiveTheme::MIDI_TOOL_ACTIVE);
        button->setActiveBorderColor(ActiveTheme::MIDI_TOOL_ACTIVE_BORDER);
        button->setBorderThickness(1.0f);
        button->setCornerRadius(6.0f);
        button->setIconPadding(6.0f);
        button->onClick = [this, i]() {
            setLaneMode(static_cast<LaneMode>(i));
            if (onLaneModeChanged)
                onLaneModeChanged(laneMode_);
        };
        addAndMakeVisible(button.get());
        laneButtons_[i] = std::move(button);
    }
    setLaneMode(LaneMode::Velocity);
}

MidiDrawerComponent::~MidiDrawerComponent() = default;

void MidiDrawerComponent::setLaneMode(LaneMode mode) {
    laneMode_ = mode;
    for (size_t i = 0; i < laneButtons_.size(); ++i)
        laneButtons_[i]->setActive(static_cast<size_t>(mode) == i);
    resized();
    repaint();
}

void MidiDrawerComponent::setVelocityCaption(juce::String caption) {
    velocityCaption_ = std::move(caption);
    repaint();
}

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

// ============================================================================
// Layout
// ============================================================================

juce::Rectangle<int> MidiDrawerComponent::getLaneRowBounds(int laneIndex) const {
    const int top = RESIZE_HANDLE_HEIGHT;
    const int totalHeight = getHeight() - top;
    const int count = getLaneCount();
    if (count <= 0 || totalHeight <= 0 || laneIndex < 0 || laneIndex >= count)
        return {};

    const int laneHeight = totalHeight / count;
    const int y = top + laneIndex * laneHeight;
    // Last lane absorbs the integer-division remainder
    const int h = (laneIndex == count - 1) ? (getHeight() - y) : laneHeight;
    return {0, y, getWidth(), h};
}

void MidiDrawerComponent::resized() {
    const bool pickerFits = leftMargin_ >= 90;
    for (size_t i = 0; i < laneButtons_.size(); ++i) {
        laneButtons_[i]->setVisible(pickerFits);
        laneButtons_[i]->setBounds(6 + static_cast<int>(i) * 28, RESIZE_HANDLE_HEIGHT + 4, 26, 26);
    }

    velocityLane_->setVisible(!showsCcLanes());
    if (!showsCcLanes())
        velocityLane_->setBounds(getLaneRowBounds(0).withTrimmedLeft(leftMargin_));

    for (size_t i = 0; i < ccTabs_.size(); ++i) {
        if (!ccTabs_[i].ccLane)
            continue;
        ccTabs_[i].ccLane->setVisible(showsCcLanes());
        if (showsCcLanes())
            ccTabs_[i].ccLane->setBounds(
                getLaneRowBounds(static_cast<int>(i)).withTrimmedLeft(leftMargin_));
    }

    updatePbRangeVisibility();
}

void MidiDrawerComponent::paint(juce::Graphics& g) {
    auto fullBounds = getLocalBounds();

    // Left margin background
    if (leftMargin_ > 0) {
        auto leftArea = fullBounds.removeFromLeft(leftMargin_);
        g.setColour(ActiveTheme::getColour(ActiveTheme::MIDI_LANE));
        g.fillRect(leftArea);

        // Top border across full width
        g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
        g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));

        // Right edge of left column
        g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER).withAlpha(0.5f));
        g.drawVerticalLine(leftMargin_ - 1, 0.0f, static_cast<float>(getHeight()));
    }

    // Resize handle at top edge (in the main area)
    auto handleArea = fullBounds.removeFromTop(RESIZE_HANDLE_HEIGHT);
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
    g.fillRect(handleArea.removeFromTop(1));

    // Lane headers in the left margin column
    if (leftMargin_ > 4)
        paintLaneHeaders(g);

    // "Range" title above the PB range input
    if (pbRangeLabel_->isVisible() && leftMargin_ > 4) {
        auto labelBounds = pbRangeLabel_->getBounds();
        g.setFont(FontManager::getInstance().getUIFont(9.0f));
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
        g.drawText("Range", 2, juce::jmax(0, labelBounds.getY() - 14), leftMargin_ - 4, 12,
                   juce::Justification::centred, false);
    }
}

void MidiDrawerComponent::paintOverChildren(juce::Graphics& g) {
    // Separator lines between stacked lanes
    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER).withAlpha(0.5f));
    for (int i = 1; i < getLaneCount(); ++i) {
        auto row = getLaneRowBounds(i);
        if (!row.isEmpty())
            g.drawHorizontalLine(row.getY(), 0.0f, static_cast<float>(getWidth()));
    }
}

void MidiDrawerComponent::paintLaneHeaders(juce::Graphics& g) {
    const auto textColour = ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY);
    const auto& fonts = FontManager::getInstance();

    if (!showsCcLanes()) {
        const auto caption =
            laneMode_ == LaneMode::MPE
                ? juce::String(juce::CharPointer_UTF8("MPE \xc2\xb7 glide on the notes"))
                : velocityCaption_;
        g.setFont(fonts.getMonoFont(9.0f));
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.drawText(caption, 4, getHeight() - 18, leftMargin_ - 8, 14, juce::Justification::centred,
                   true);
        return;
    }

    // Each CC lane's name below the picker, with a close button
    for (int lane = 0; lane < getLaneCount(); ++lane) {
        auto row = getLaneRowBounds(lane);
        if (row.isEmpty())
            continue;
        const int top = juce::jmax(row.getY() + 4, RESIZE_HANDLE_HEIGHT + PICKER_HEIGHT);

        g.setFont(fonts.getUIFont(10.0f));
        g.setColour(textColour);
        g.drawText(ccTabs_[static_cast<size_t>(lane)].name, 6, top, leftMargin_ - 26, 14,
                   juce::Justification::centredLeft, true);

        g.setColour(textColour.withAlpha(0.6f));
        auto closeX = static_cast<float>(leftMargin_ - 11);
        auto closeY = static_cast<float>(top + 7);
        g.drawLine(closeX - 2.5f, closeY - 2.5f, closeX + 2.5f, closeY + 2.5f, 1.0f);
        g.drawLine(closeX + 2.5f, closeY - 2.5f, closeX - 2.5f, closeY + 2.5f, 1.0f);
    }

    if (ccTabs_.empty()) {
        g.setFont(fonts.getUIFont(10.0f));
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.drawText("No CC lanes", leftMargin_ + 8, RESIZE_HANDLE_HEIGHT,
                   getWidth() - leftMargin_ - 16, getHeight() - RESIZE_HANDLE_HEIGHT,
                   juce::Justification::centredLeft, true);
    }

    // "+" button at the bottom of the header column
    g.setFont(fonts.getUIFont(14.0f));
    g.setColour(textColour);
    g.drawText("+", 0, getHeight() - ADD_BUTTON_HEIGHT, leftMargin_, ADD_BUTTON_HEIGHT,
               juce::Justification::centred, false);
}

// ============================================================================
// Mouse handling
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

    // Only handle clicks in the lane header column
    if (e.x >= leftMargin_ || e.y < RESIZE_HANDLE_HEIGHT)
        return;

    if (!showsCcLanes())
        return;

    // "+" button at the bottom of the header column
    if (e.y >= getHeight() - ADD_BUTTON_HEIGHT) {
        showAddLaneMenu();
        return;
    }

    // Close button beside a lane's name
    for (size_t i = 0; i < ccTabs_.size(); ++i) {
        auto row = getLaneRowBounds(static_cast<int>(i));
        const int top = juce::jmax(row.getY() + 4, RESIZE_HANDLE_HEIGHT + PICKER_HEIGHT);
        if (row.contains(e.getPosition())) {
            if (e.x >= leftMargin_ - 18 && e.y >= top && e.y <= top + 14)
                removeTab(static_cast<int>(i));
            return;
        }
    }
}

// ============================================================================
// Lane management
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

void MidiDrawerComponent::growDrawerForLanes() {
    // Ask the parent to grow the drawer so each lane keeps a usable height
    // (the parent clamps to its own max)
    int preferred = RESIZE_HANDLE_HEIGHT + getLaneCount() * PREFERRED_LANE_HEIGHT;
    if (onResizeDrag && getHeight() < preferred)
        onResizeDrag(preferred);
}

void MidiDrawerComponent::addCCTab(int ccNumber) {
    // Check if the lane already exists
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
    addAndMakeVisible(tab.ccLane.get());

    ccTabs_.push_back(std::move(tab));
    setLaneMode(LaneMode::CC);
    growDrawerForLanes();
    if (onLanesChanged)
        onLanesChanged();
}

void MidiDrawerComponent::addPitchBendTab() {
    // Check if the lane already exists
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
    addAndMakeVisible(tab.ccLane.get());

    ccTabs_.push_back(std::move(tab));
    setLaneMode(LaneMode::CC);
    growDrawerForLanes();
    if (onLanesChanged)
        onLanesChanged();
}

void MidiDrawerComponent::removeTab(int ccIdx) {
    if (ccIdx < 0 || ccIdx >= static_cast<int>(ccTabs_.size()))
        return;

    removeChildComponent(ccTabs_[static_cast<size_t>(ccIdx)].ccLane.get());
    ccTabs_.erase(ccTabs_.begin() + ccIdx);

    resized();
    repaint();
    if (onLanesChanged)
        onLanesChanged();
}

void MidiDrawerComponent::updatePbRangeVisibility() {
    // The PB range editor sits left of the icon column, in the pitchbend lane's row
    int pbLaneIndex = -1;
    for (size_t i = 0; i < ccTabs_.size(); ++i) {
        if (ccTabs_[i].isPitchBend) {
            pbLaneIndex = static_cast<int>(i);
            if (ccTabs_[i].ccLane) {
                pbRangeLabel_->setText(juce::String(ccTabs_[i].ccLane->getPitchBendRange()),
                                       juce::dontSendNotification);
            }
            break;
        }
    }

    bool showPbRange = showsCcLanes() && pbLaneIndex >= 0 && leftMargin_ > 4;
    pbRangeLabel_->setVisible(showPbRange);
    if (showPbRange) {
        auto row = getLaneRowBounds(pbLaneIndex);
        int labelW = leftMargin_ - 4;
        int labelH = 18;
        int titleH = 12;
        int totalH = titleH + 2 + labelH;
        int groupY = row.getY() + (row.getHeight() - totalH) / 2;
        pbRangeLabel_->setBounds(2, groupY + titleH + 2, labelW, labelH);
        pbRangeLabel_->toFront(false);
    }
}

void MidiDrawerComponent::showAddLaneMenu() {
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
