#include "../../../../themes/DarkTheme.hpp"
#include "../ClipInspector.hpp"

namespace magda::daw::ui {

void ClipInspector::resized() {
    auto bounds = getLocalBounds().reduced(10);

    // Clip name as header with type icon (outside viewport)
    {
        const int iconSize = 18;
        const int gap = 6;
        auto headerRow = bounds.removeFromTop(24);
        clipTypeIcon_->setBounds(
            headerRow.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize));
        headerRow.removeFromLeft(gap);
        clipNameValue_.setBounds(headerRow);
    }
    bounds.removeFromTop(8);

    // Viewport takes remaining space for scrollable clip properties
    clipPropsViewport_.setBounds(bounds);

    // Layout all clip properties inside the container
    const int containerWidth = bounds.getWidth() - 12;  // Account for scrollbar
    auto cb = juce::Rectangle<int>(0, 0, containerWidth, 0);
    auto addRow = [&](int height) -> juce::Rectangle<int> {
        auto row = juce::Rectangle<int>(0, cb.getHeight(), containerWidth, height);
        cb.setHeight(cb.getHeight() + height);
        return row;
    };
    auto addSpace = [&](int height) { cb.setHeight(cb.getHeight() + height); };

    // Clear separator positions for this layout pass
    clipPropsContainer_.separatorYPositions.clear();
    auto addSeparator = [&]() {
        addSpace(4);
        clipPropsContainer_.separatorYPositions.push_back(cb.getHeight());
        addSpace(5);
    };

    const int iconSize = 22;
    const int gap = 3;
    const int labelHeight = 14;
    const int valueHeight = 22;
    int fieldWidth = (containerWidth - iconSize - gap * 3) / 3;

    // Position row: position icon — start, end, offset (always visible)
    {
        auto labelRow = addRow(labelHeight);
        labelRow.removeFromLeft(iconSize + gap);
        clipStartLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));
        labelRow.removeFromLeft(gap);
        clipEndLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));
        labelRow.removeFromLeft(gap);
        clipOffsetLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));

        auto valueRow = addRow(valueHeight);
        clipPositionIcon_->setBounds(valueRow.removeFromLeft(iconSize));
        valueRow.removeFromLeft(gap);
        clipStartValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
        valueRow.removeFromLeft(gap);
        clipEndValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
        valueRow.removeFromLeft(gap);
        clipContentOffsetValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
    }

    addSeparator();

    // File path label (full width)
    clipFilePathLabel_.setBounds(addRow(16));

    addSeparator();

    // Loop row: loop toggle + lstart | lend | phase (only when loop is on)
    if (clipLoopToggle_->isVisible()) {
        const auto* clip = magda::ClipManager::getInstance().getClip(selectedClipId_);
        bool loopOn = clip && (clip->loopEnabled || clip->view == magda::ClipView::Session);

        if (loopOn) {
            // Loop ON: loop toggle — start, end, phase
            auto labelRow = addRow(labelHeight);
            labelRow.removeFromLeft(iconSize + gap);
            clipLoopStartLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));
            labelRow.removeFromLeft(gap);
            clipLoopEndLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));
            labelRow.removeFromLeft(gap);
            clipLoopPhaseLabel_.setBounds(labelRow.removeFromLeft(fieldWidth));

            auto valueRow = addRow(valueHeight);
            clipLoopToggle_->setBounds(
                valueRow.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize));
            valueRow.removeFromLeft(gap);
            clipLoopStartValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
            valueRow.removeFromLeft(gap);
            clipLoopEndValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
            valueRow.removeFromLeft(gap);
            clipLoopPhaseValue_->setBounds(valueRow.removeFromLeft(fieldWidth));
        } else {
            // Loop OFF: just the toggle icon (offset is in position row)
            auto valueRow = addRow(valueHeight);
            clipLoopToggle_->setBounds(
                valueRow.removeFromLeft(iconSize).withSizeKeepingCentre(iconSize, iconSize));
        }
    }
    addSeparator();

    // 2-column grid: warp toggles | combo  /  BPM | speed/beats
    {
        const int colGap = 8;
        int halfWidth = (containerWidth - colGap) / 2;

        // Row 1: [WARP] [BEAT] centered | [stretch combo]
        if (clipWarpToggle_.isVisible() || clipAutoTempoToggle_.isVisible()) {
            auto row1 = addRow(24);
            auto left = row1.removeFromLeft(halfWidth);
            row1.removeFromLeft(colGap);
            auto right = row1;

            const int btnWidth = 46;
            const int btnGap = 4;
            int numBtns =
                (clipWarpToggle_.isVisible() ? 1 : 0) + (clipAutoTempoToggle_.isVisible() ? 1 : 0);
            int totalBtnsWidth = numBtns * btnWidth + (numBtns > 1 ? btnGap : 0);
            int btnOffset = (left.getWidth() - totalBtnsWidth) / 2;
            left.removeFromLeft(btnOffset);

            if (clipWarpToggle_.isVisible()) {
                clipWarpToggle_.setBounds(left.removeFromLeft(btnWidth).reduced(0, 1));
                left.removeFromLeft(btnGap);
            }
            if (clipAutoTempoToggle_.isVisible()) {
                clipAutoTempoToggle_.setBounds(left.removeFromLeft(btnWidth).reduced(0, 1));
            }
            if (stretchModeCombo_.isVisible()) {
                stretchModeCombo_.setBounds(right.reduced(0, 1));
            }
        }

        // Row 2: [BPM] centered | [speed OR beats]
        if (clipBpmValue_.isVisible() || (clipStretchValue_ && clipStretchValue_->isVisible()) ||
            clipBeatsLengthValue_->isVisible()) {
            addSpace(4);
            auto row2 = addRow(22);
            auto left = row2.removeFromLeft(halfWidth);
            row2.removeFromLeft(colGap);
            auto right = row2;

            if (clipBpmValue_.isVisible()) {
                int bpmWidth = 96;  // matches WARP(46) + gap(4) + BEAT(46)
                int bpmOffset = (left.getWidth() - bpmWidth) / 2;
                clipBpmValue_.setBounds(left.withX(left.getX() + bpmOffset).withWidth(bpmWidth));
            }
            if (clipStretchValue_ && clipStretchValue_->isVisible()) {
                clipStretchValue_->setBounds(right.reduced(0, 1));
            }
            if (clipBeatsLengthValue_->isVisible()) {
                clipBeatsLengthValue_->setBounds(right.reduced(0, 1));
            }
        }
    }

    // Separator: after position/warp rows, before Pitch
    if (pitchSectionLabel_.isVisible())
        addSeparator();

    // Pitch section (audio clips only)
    if (pitchSectionLabel_.isVisible()) {
        pitchSectionLabel_.setBounds(addRow(16));
        if (autoPitchToggle_.isVisible()) {
            addSpace(4);
            auto row = addRow(22);
            int halfWidth = (containerWidth - 8) / 2;
            autoPitchToggle_.setBounds(row.removeFromLeft(halfWidth).reduced(0, 1));
            row.removeFromLeft(8);
            autoPitchModeCombo_.setBounds(row.removeFromLeft(halfWidth).reduced(0, 1));
        }
        addSpace(4);
        {
            auto row = addRow(22);
            int halfWidth = (containerWidth - 8) / 2;
            pitchChangeValue_->setBounds(row.removeFromLeft(halfWidth));
            row.removeFromLeft(8);
            transposeValue_->setBounds(row.removeFromLeft(halfWidth));
        }
    }

    // Separator: between Pitch and Mix
    if (clipMixSectionLabel_.isVisible())
        addSeparator();

    // Mix section (audio clips only) — 2-column: volume/pan, reverse/LR
    if (clipMixSectionLabel_.isVisible()) {
        clipMixSectionLabel_.setBounds(addRow(16));
        addSpace(4);
        const int colGap = 8;
        int halfWidth = (containerWidth - colGap) / 2;

        // Row 1: [volume] | [pan]
        {
            auto row = addRow(22);
            clipGainValue_->setBounds(row.removeFromLeft(halfWidth));
            row.removeFromLeft(colGap);
            clipPanValue_->setBounds(row.removeFromLeft(halfWidth));
        }
        addSpace(4);
        // Row 2: [REVERSE full width]
        {
            auto row = addRow(22);
            reverseToggle_.setBounds(row.reduced(0, 1));
        }
    }

    // Separator: between Mix and Fades
    if (fadesSectionLabel_.isVisible())
        addSeparator();

    // Fades section (arrangement clips only, collapsible)
    if (fadesSectionLabel_.isVisible()) {
        {
            auto headerRow = addRow(16);
            fadesCollapseToggle_.setBounds(headerRow.removeFromLeft(16));
            fadesSectionLabel_.setBounds(headerRow);
        }
        if (!fadesCollapsed_) {
            addSpace(4);
            const int colGap = 8;
            int halfWidth = (containerWidth - colGap) / 2;

            // Row 1: [fade in] | [fade out]
            {
                auto row = addRow(22);
                fadeInValue_->setBounds(row.removeFromLeft(halfWidth));
                row.removeFromLeft(colGap);
                fadeOutValue_->setBounds(row.removeFromLeft(halfWidth));
            }
            addSpace(4);

            // Row 2: fade type buttons (4 icons each side)
            {
                auto row = addRow(24);
                auto left = row.removeFromLeft(halfWidth);
                row.removeFromLeft(colGap);
                auto right = row;

                const int btnSize = 24;
                const int btnGap = 2;
                for (int i = 0; i < 4; ++i) {
                    if (fadeInTypeButtons_[i]) {
                        fadeInTypeButtons_[i]->setBounds(left.removeFromLeft(btnSize));
                        if (i < 3)
                            left.removeFromLeft(btnGap);
                    }
                    if (fadeOutTypeButtons_[i]) {
                        fadeOutTypeButtons_[i]->setBounds(right.removeFromLeft(btnSize));
                        if (i < 3)
                            right.removeFromLeft(btnGap);
                    }
                }
            }
            addSpace(4);

            // Row 3: fade behavior buttons (2 icons each side)
            {
                auto row = addRow(24);
                auto left = row.removeFromLeft(halfWidth);
                row.removeFromLeft(colGap);
                auto right = row;

                const int btnSize = 24;
                const int btnGap = 2;
                for (int i = 0; i < 2; ++i) {
                    if (fadeInBehaviourButtons_[i]) {
                        fadeInBehaviourButtons_[i]->setBounds(left.removeFromLeft(btnSize));
                        if (i < 1)
                            left.removeFromLeft(btnGap);
                    }
                    if (fadeOutBehaviourButtons_[i]) {
                        fadeOutBehaviourButtons_[i]->setBounds(right.removeFromLeft(btnSize));
                        if (i < 1)
                            right.removeFromLeft(btnGap);
                    }
                }
            }
            addSpace(4);

            // Row 4: auto-crossfade toggle
            {
                auto row = addRow(22);
                autoCrossfadeToggle_.setBounds(row.reduced(0, 1));
            }
        }
    }

    // Separator: between Fades and Channels
    if (channelsSectionLabel_.isVisible())
        addSeparator();

    // Channels section (hidden for now, controls moved to Mix section)
    if (channelsSectionLabel_.isVisible()) {
        channelsSectionLabel_.setBounds(addRow(16));
        addSpace(4);
        const int btnWidth = 46;
        const int btnGap = 8;
        auto row = addRow(22);
        leftChannelToggle_.setBounds(row.removeFromLeft(btnWidth).reduced(0, 1));
        row.removeFromLeft(btnGap);
        rightChannelToggle_.setBounds(row.removeFromLeft(btnWidth).reduced(0, 1));
    }

    // Separator: after last visible section, before launch controls
    if (launchQuantizeLabel_.isVisible())
        addSeparator();

    // Session clip launch properties
    if (launchModeLabel_.isVisible()) {
        launchModeLabel_.setBounds(addRow(16));
        addSpace(4);
        launchModeCombo_.setBounds(addRow(22).reduced(0, 1));
    }
    if (launchQuantizeLabel_.isVisible()) {
        launchQuantizeLabel_.setBounds(addRow(16));
        addSpace(4);
        launchQuantizeCombo_.setBounds(addRow(22).reduced(0, 1));
    }

    // Set container bounds to accommodate all content
    clipPropsContainer_.setBounds(cb);
}

void ClipInspector::ClipPropsContainer::paint(juce::Graphics& g) {
    // Draw separator lines between sections
    g.setColour(DarkTheme::getColour(DarkTheme::SEPARATOR));
    for (int y : separatorYPositions) {
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }
}

}  // namespace magda::daw::ui
