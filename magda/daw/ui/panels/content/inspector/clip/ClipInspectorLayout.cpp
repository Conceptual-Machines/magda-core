// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../../../themes/ActiveTheme.hpp"
#include "../ClipInspector.hpp"
#include "ClipInspectorStyle.hpp"

namespace magda::daw::ui {

namespace {

constexpr int kHeaderHeight = 40;
constexpr int kChipSize = 26;
constexpr int kColumnGap = 8;
constexpr int kHeadingHeight = 14;
constexpr int kSectionPad = 8;

/** @brief Splits @p row into a left and right half with the column gap between. */
std::pair<juce::Rectangle<int>, juce::Rectangle<int>> halves(juce::Rectangle<int> row) {
    auto left = row.removeFromLeft((row.getWidth() - kColumnGap) / 2);
    row.removeFromLeft(kColumnGap);
    return {left, row};
}

}  // namespace

void ClipInspector::resized() {
    if (getWidth() < 1 || getHeight() < 1)
        return;

    auto bounds = getLocalBounds().reduced(10, 8);
    if (bounds.getWidth() < 1 || bounds.getHeight() < 1)
        return;

    // Preserve the scroll position across relayout: the container is re-anchored at the origin
    // below, and resized() runs on every property-driven update.
    const auto savedViewPos = clipPropsViewport_.getViewPosition();

    // ---- Header: spine | name over summary | ghost, save, power ------
    {
        auto row = bounds.removeFromTop(kHeaderHeight);
        if (nameFilled_) {
            colourSwatch_->setBounds({});
        } else {
            colourSwatch_->setBounds(row.removeFromLeft(4).reduced(0, 3));
            row.removeFromLeft(10);
        }

        auto chip = [&row](juce::Component& c) {
            c.setBounds(row.removeFromRight(kChipSize).withSizeKeepingCentre(kChipSize, kChipSize));
            row.removeFromRight(4);
        };
        chip(*clipEnabledToggle_);
        if (saveLibraryButton_.isVisible())
            chip(saveLibraryButton_);
        if (clipGhostIcon_->isVisible())
            chip(*clipGhostIcon_);
        if (araChip_.isVisible())
            chip(araChip_);
        row.removeFromRight(4);

        nameArea_ = row.reduced(nameFilled_ ? 6 : 0, 2);
        auto text = nameArea_;
        clipNameValue_.setBounds(text.removeFromTop(20));
        clipSubline_.setBounds(text.removeFromTop(14));
    }
    bounds.removeFromTop(6);
    clipPropsViewport_.setBounds(bounds);

    // ---- Sections -------------------------------------------------------
    const int width = juce::jmax(1, bounds.getWidth() - 8);  // room for the scrollbar
    int y = 0;
    auto addRow = [&](int height) {
        juce::Rectangle<int> row(0, y, width, juce::jmax(1, height));
        y += row.getHeight();
        return row;
    };
    auto addSpace = [&](int height) { y += height; };

    auto& separators = clipPropsContainer_.separatorYPositions;
    separators.clear();
    bool firstSection = true;
    auto beginSection = [&](juce::Component& header) {
        if (!firstSection) {
            separators.push_back(y);
            addSpace(1);
        }
        firstSection = false;
        header.setBounds(addRow(CollapsibleSectionHeader::kHeight));
    };
    auto endSection = [&](bool open) { addSpace(open ? kSectionPad + 2 : 2); };

    const int fieldH = clip_style::kFieldHeight;
    const int captionH = clip_style::kCaptionHeight;
    const int gap = clip_style::kGap;

    // Clip: column headings, the Clip row, the Loop row with PHASE under its third field.
    beginSection(clipHeader_);
    if (!clipHeader_.isCollapsed()) {
        const int rowLabelW = 62;
        const bool threeColumns = width >= 270;
        const int columns = threeColumns ? 3 : 2;
        const int fieldW = (width - rowLabelW - 4 * columns) / columns;

        auto fieldCells = [&](juce::Rectangle<int> row) {
            row.removeFromLeft(rowLabelW);
            std::array<juce::Rectangle<int>, 3> cells{};  // the third stays empty when narrow
            for (int i = 0; i < columns; ++i) {
                row.removeFromLeft(4);
                cells[static_cast<size_t>(i)] = row.removeFromLeft(fieldW);
            }
            return cells;
        };

        auto headings = fieldCells(addRow(kHeadingHeight));
        startColumnLabel_.setBounds(headings[0]);
        endColumnLabel_.setBounds(headings[1]);
        lengthColumnLabel_.setBounds(headings[2]);
        addSpace(2);

        if (clipStartValue_->isVisible()) {
            auto row = addRow(fieldH);
            clipRowLabel_.setBounds(row.withWidth(rowLabelW));
            auto cells = fieldCells(row);
            clipStartValue_->setBounds(cells[0]);
            clipEndValue_->setBounds(cells[1]);
            clipLengthValue_->setBounds(cells[2]);
            addSpace(gap);
        }

        auto row = addRow(fieldH);
        auto label = row.withWidth(rowLabelW);
        clipLoopSwitch_.setBounds(label.removeFromLeft(ToggleSwitch::kWidth));
        label.removeFromLeft(6);
        loopRowLabel_.setBounds(label);
        auto cells = fieldCells(row);
        clipLoopStartValue_->setBounds(cells[0]);
        clipLoopEndValue_->setBounds(cells[1]);
        clipLoopPhaseValue_->setBounds(cells[2]);
        clipLoopPhaseLabel_.setBounds(fieldCells(addRow(kHeadingHeight + 2))[2].withTrimmedTop(2));

        for (juce::Component* section : std::initializer_list<juce::Component*>{
                 takesSection_.get(), chordProgressionSection_.get()}) {
            const int h = section == takesSection_.get()
                              ? takesSection_->getPreferredHeight()
                              : chordProgressionSection_->getPreferredHeight();
            if (section->isVisible() && h > 0) {
                addSpace(gap);
                section->setBounds(addRow(h));
            }
        }
    }
    endSection(!clipHeader_.isCollapsed());

    // Mix: Volume | Pan | Gain, then Reverse | Gain envelope.
    if (mixHeader_.isVisible()) {
        beginSection(mixHeader_);
        if (!mixHeader_.isCollapsed()) {
            if (araNoteLabel_.isVisible() && araNoteInMix_) {
                araNoteLabel_.setBounds(addRow(captionH));
                addSpace(gap);
            }
            const int thirdW = (width - 2 * 6) / 3;
            auto thirds = [&](juce::Rectangle<int> row, juce::Component& a, juce::Component& b,
                              juce::Component& c) {
                a.setBounds(row.removeFromLeft(thirdW));
                row.removeFromLeft(6);
                b.setBounds(row.removeFromLeft(thirdW));
                row.removeFromLeft(6);
                c.setBounds(row);
            };
            thirds(addRow(captionH), volumeLabel_, panLabel_, gainLabel_);
            thirds(addRow(fieldH), *clipVolumeValue_, *clipPanValue_, *clipGainValue_);
            addSpace(gap);
            auto [left, right] = halves(addRow(fieldH));
            reverseToggle_.setBounds(left);
            gainEnvelopeToggle_.setBounds(right);
        }
        endSection(!mixHeader_.isCollapsed());
    }

    // Warp & pitch: mode and engine, transients, tempo and length, speed, pitch and key.
    if (warpHeader_.isVisible()) {
        beginSection(warpHeader_);
        if (!warpHeader_.isCollapsed()) {
            if (araNoteLabel_.isVisible() && !araNoteInMix_) {
                araNoteLabel_.setBounds(addRow(captionH));
                addSpace(gap);
            }
            {
                auto [left, right] = halves(addRow(fieldH));
                playbackModeStrip_.setBounds(left);
                stretchModeCombo_.setBounds(right);
            }
            if (transientSensitivityValue_->isVisible()) {
                addSpace(gap);
                transientSensitivityLabel_.setBounds(addRow(captionH));
                transientSensitivityValue_->setBounds(addRow(fieldH));
            }
            if (clipBpmValue_.isVisible()) {
                addSpace(gap);
                auto [tempoCaption, lengthCaption] = halves(addRow(captionH + 2));
                // The x2 / /2 buttons narrow before the "Tempo" caption clips (#3117).
                const int tempoButtonW =
                    juce::jlimit(18, 26, (tempoCaption.getWidth() - 3 - 34) / 2);
                doubleTempoButton_.setBounds(
                    tempoCaption.removeFromRight(tempoButtonW).withTrimmedBottom(2));
                tempoCaption.removeFromRight(3);
                halveTempoButton_.setBounds(
                    tempoCaption.removeFromRight(tempoButtonW).withTrimmedBottom(2));
                tempoLabel_.setBounds(tempoCaption);
                sourceLengthLabel_.setBounds(lengthCaption);
                addSpace(2);
                auto [tempo, length] = halves(addRow(fieldH));
                clipBpmValue_.setBounds(tempo);
                clipBpmUnitLabel_.setBounds(tempo.withTrimmedRight(6).removeFromRight(30));
                clipBeatsLengthValue_->setBounds(length);
            }
            if (clipStretchValue_->isVisible()) {
                addSpace(gap);
                speedLabel_.setBounds(addRow(captionH));
                auto [speed, analog] = halves(addRow(fieldH));
                clipStretchValue_->setBounds(speed);
                analogPitchToggle_.setBounds(analog);
            }
            addSpace(gap);
            {
                auto [pitchCaption, keyCaption] = halves(addRow(captionH));
                pitchLabel_.setBounds(pitchCaption);
                clipKeyLabel_.setBounds(keyCaption);
                auto [pitch, key] = halves(addRow(fieldH));
                pitchSemitonesValue_->setBounds(pitch.removeFromLeft((pitch.getWidth() - 4) / 2));
                pitch.removeFromLeft(4);
                pitchCentsValue_->setBounds(pitch);
                clipKeyRootCombo_.setBounds(key);
            }
        }
        endSection(!warpHeader_.isCollapsed());
    }

    // MIDI pitch: -12 -1 [notes] +1 +12.
    if (midiPitchHeader_.isVisible()) {
        beginSection(midiPitchHeader_);
        if (!midiPitchHeader_.isCollapsed()) {
            midiTransposeLabel_.setBounds(addRow(captionH));
            auto row = addRow(fieldH);
            // Buttons narrow so the "notes" hint keeps its 36px (#3117).
            const int buttonW = juce::jlimit(24, 34, (row.getWidth() - 12 - 36) / 4);
            midiTransposeButtons_[0].setBounds(row.removeFromLeft(buttonW));
            row.removeFromLeft(4);
            midiTransposeButtons_[1].setBounds(row.removeFromLeft(buttonW));
            row.removeFromLeft(4);
            midiTransposeButtons_[3].setBounds(row.removeFromRight(buttonW));
            row.removeFromRight(4);
            midiTransposeButtons_[2].setBounds(row.removeFromRight(buttonW));
            row.removeFromRight(4);
            midiTransposeHint_.setBounds(row);
        }
        endSection(!midiPitchHeader_.isCollapsed());
    }

    // Fades lays itself out under its own header.
    if (const int h = fadesSection_->getPreferredHeight(); h > 0) {
        beginSection(*fadesSection_);
        fadesSection_->setBounds(fadesSection_->getBounds().withHeight(h));
        y += h - CollapsibleSectionHeader::kHeight;
        endSection(h > CollapsibleSectionHeader::kHeight);
    }

    if (const int h = timeBendSection_->getPreferredHeight(width);
        h > 0 && timeBendSection_->isVisible()) {
        beginSection(*timeBendSection_);
        timeBendSection_->setBounds(timeBendSection_->getBounds().withHeight(h));
        y += h - CollapsibleSectionHeader::kHeight;
        endSection(h > CollapsibleSectionHeader::kHeight);
    }

    if (const int h = labelsSection_->getPreferredHeight(); h > 0) {
        beginSection(*labelsSection_);
        labelsSection_->setBounds(labelsSection_->getBounds().withHeight(h));
        y += h - CollapsibleSectionHeader::kHeight;
        endSection(h > CollapsibleSectionHeader::kHeight);
    }

    // Launch: quantize | follow action, then delay | loops when there is an action.
    if (launchHeader_.isVisible()) {
        beginSection(launchHeader_);
        if (!launchHeader_.isCollapsed()) {
            {
                auto [left, right] = halves(addRow(captionH));
                launchQuantizeLabel_.setBounds(left);
                followActionLabel_.setBounds(right);
                auto [quantize, follow] = halves(addRow(fieldH));
                launchQuantizeCombo_.setBounds(quantize);
                followActionCombo_.setBounds(follow);
            }
            if (followActionDelaySlider_.isVisible()) {
                addSpace(gap);
                auto [left, right] = halves(addRow(captionH));
                followActionDelayLabel_.setBounds(left);
                followActionLoopCountLabel_.setBounds(right);
                auto [delay, loops] = halves(addRow(fieldH));
                followActionDelaySlider_.setBounds(delay);
                followActionLoopCountSlider_.setBounds(loops);
            }
        }
        endSection(!launchHeader_.isCollapsed());
    }

    // Set container bounds to hold the content, then restore the scroll position
    // (the viewport clamps it if the content shrank).
    clipPropsContainer_.setBounds(0, 0, width, y);
    clipPropsViewport_.setViewPosition(savedViewPos);
}

void ClipInspector::ClipPropsContainer::paint(juce::Graphics& g) {
    g.setColour(ActiveTheme::getColour(ActiveTheme::SEPARATOR));
    for (int y : separatorYPositions)
        g.fillRect(0, y, getWidth(), 1);
}

}  // namespace magda::daw::ui
