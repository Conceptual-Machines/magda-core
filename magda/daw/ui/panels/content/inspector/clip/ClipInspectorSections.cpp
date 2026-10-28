// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>

#include "../../../../../audio/AudioThumbnailManager.hpp"
#include "../../../../components/common/ColourSwatch.hpp"
#include "../../../../components/common/SvgButton.hpp"
#include "../../../../components/common/Toast.hpp"
#include "../../../../state/TimelineController.hpp"
#include "../../../../themes/ActiveTheme.hpp"
#include "../../../../themes/FontManager.hpp"
#include "../../../../themes/InspectorComboBoxLookAndFeel.hpp"
#include "../../../../themes/SmallButtonLookAndFeel.hpp"
#include "../../../../utils/TimelineUtils.hpp"
#include "../ClipInspector.hpp"
#include "BinaryData.h"
#include "ClipInspectorStyle.hpp"
#include "audio/CompService.hpp"
#include "audio/WarpMarkerManager.hpp"
#include "core/AudioClipSourceDisplay.hpp"
#include "core/ClipBatchEdit.hpp"
#include "core/ClipCommands.hpp"
#include "core/ClipDisplayInfo.hpp"
#include "core/ClipOperations.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/TempoUtils.hpp"
#include "core/TimeStretchModes.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "engine/AudioEngine.hpp"
#include "project/ProjectManager.hpp"
#include "ui/components/common/ToggleChips.hpp"

namespace magda::daw::ui {
namespace {

double getAudioFileDurationForInspector(const magda::ClipInfo& clip) {
    if (!clip.isAudio() || audioEventRef(clip).sourceFilePath().isEmpty())
        return 0.0;

    if (auto* thumbnail = magda::AudioThumbnailManager::getInstance().getThumbnail(
            audioEventRef(clip).sourceFilePath())) {
        return thumbnail->getTotalLength();
    }

    return audioEventRef(clip).sourceDurationSeconds();
}

double displayBeatsToAudioSourceSeconds(const magda::ClipInfo& clip, double displayBeats,
                                        double projectBpm) {
    const double bpm = projectBpm > 0.0 ? projectBpm : 120.0;
    const auto info =
        magda::ClipDisplayInfo::from(clip, bpm, getAudioFileDurationForInspector(clip));
    const double displaySeconds = magda::TimelineUtils::beatsToSeconds(displayBeats, bpm);
    return info.timelineToSource(displaySeconds);
}

double minClipLengthBeats(double bpm) {
    const double validBpm = magda::isValidBpm(bpm) ? bpm : 120.0;
    return magda::ClipOperations::MIN_CLIP_LENGTH * validBpm / 60.0;
}

double timelineLengthSeconds(const magda::ClipInfo& clip, double bpm) {
    return clip.getTimelineLength(bpm);
}

}  // namespace

// ========================================================================
// Header
// ========================================================================

void ClipInspector::initHeader() {
    clipNameValue_.setFont(FontManager::getInstance().getUIFont(14.0f));
    clipNameValue_.setColour(juce::Label::textColourId, ActiveTheme::getTextColour());
    clipNameValue_.setBorderSize({});
    clipNameValue_.setEditable(false, true);
    clipNameValue_.onTextChange = [this]() {
        if (primaryClipId() != magda::INVALID_CLIP_ID) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetClipNameCommand>(primaryClipId(),
                                                            clipNameValue_.getText()));
        }
    };
    addChildComponent(clipNameValue_);

    clipSubline_.setFont(FontManager::getInstance().getMonoFont(10.0f));
    clipSubline_.setColour(juce::Label::textColourId,
                           ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    clipSubline_.setBorderSize({});
    clipSubline_.setMinimumHorizontalScale(1.0f);
    addChildComponent(clipSubline_);

    // The colour spine. Clicking it, or right-clicking the name, picks the colour.
    colourSwatch_ = std::make_unique<magda::ColourSwatch>();
    auto* swatch = static_cast<magda::ColourSwatch*>(colourSwatch_.get());
    swatch->onColourClicked = [this, swatch]() {
        auto pid = primaryClipId();
        if (pid == magda::INVALID_CLIP_ID)
            return;

        auto menu = juce::PopupMenu();
        menu.addItem(1, "None");
        menu.addSeparator();

        auto makeChip = [](juce::Colour colour) {
            juce::Image chip(juce::Image::ARGB, 14, 14, true);
            juce::Graphics cg(chip);
            cg.setColour(colour);
            cg.fillRoundedRectangle(0.0f, 0.0f, 14.0f, 14.0f, 2.0f);
            auto drawable = std::make_unique<juce::DrawableImage>();
            drawable->setImage(chip);
            return drawable;
        };

        menu.addItem(2, "Inherit from Track");
        menu.addSeparator();

        const auto palette =
            magda::ProjectManager::getInstance().getCurrentProjectInfo().defaults.colourPalette;
        for (size_t i = 0; i < palette.size(); ++i) {
            const auto colour = juce::Colour(palette[i].colour);
            menu.addItem(static_cast<int>(i + 3), palette[i].name, true, false, makeChip(colour));
        }

        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&clipNameValue_),
            [this, swatch, palette](int result) {
                if (result == 0)
                    return;
                auto pid = primaryClipId();
                if (pid == magda::INVALID_CLIP_ID)
                    return;
                if (result == 1) {
                    swatch->clearColour();
                    magda::ClipBatchEdit batch("Set Clip Colour", selectedClipIds_.size());
                    for (auto cid : selectedClipIds_) {
                        batch.execute(std::make_unique<magda::SetClipColourCommand>(
                            cid, juce::Colour(0xFF444444)));
                    }
                } else if (result == 2) {
                    magda::ClipBatchEdit batch("Set Clip Colour", selectedClipIds_.size());
                    for (auto cid : selectedClipIds_) {
                        const auto* clip = magda::ClipManager::getInstance().getClip(cid);
                        if (clip) {
                            const auto* track =
                                magda::TrackManager::getInstance().getTrack(clip->trackId);
                            if (track) {
                                if (cid == pid)
                                    swatch->setColour(track->colour);
                                batch.execute(std::make_unique<magda::SetClipColourCommand>(
                                    cid, track->colour));
                            }
                        }
                    }
                } else {
                    const auto idx = static_cast<size_t>(result - 3);
                    if (idx < palette.size()) {
                        const auto colour = juce::Colour(palette[idx].colour);
                        swatch->setColour(colour);
                        magda::ClipBatchEdit batch("Set Clip Colour", selectedClipIds_.size());
                        for (auto cid : selectedClipIds_) {
                            batch.execute(
                                std::make_unique<magda::SetClipColourCommand>(cid, colour));
                        }
                    }
                }
            });
    };
    namePopupListener_.onPopup = [swatch]() {
        if (swatch->onColourClicked)
            swatch->onColourClicked();
    };
    clipNameValue_.addMouseListener(&namePopupListener_, false);
    addChildComponent(*colourSwatch_);

    // Ghost indicator (same link glyph as the clip header), shown only when the clip mirrors a
    // link group. A badge with a tooltip, no click action.
    clipGhostIcon_ = std::make_unique<magda::SvgButton>("Ghost", BinaryData::link_flat_svg,
                                                        BinaryData::link_flat_svgSize);
    clipGhostIcon_->setOriginalColor(juce::Colour(0xFFB3B3B3));
    clipGhostIcon_->setNormalColor(ActiveTheme::getColour(ActiveTheme::TEXT_SECONDARY));
    clipGhostIcon_->setIconPadding(5.0f);
    clipGhostIcon_->setTooltip("Ghost clip");
    addChildComponent(*clipGhostIcon_);
    addChildComponent(araChip_);

    saveLibraryButton_.setTooltip(
        "Save current clip settings or MIDI clip file to the media library");
    saveLibraryButton_.onClick = [this]() { saveToLibrary(); };
    addChildComponent(saveLibraryButton_);

    clipEnabledToggle_ = std::make_unique<magda::SvgButton>("ClipEnabled", BinaryData::power_svg,
                                                            BinaryData::power_svgSize);
    applyPowerChip(*clipEnabledToggle_, 26.0f);
    clipEnabledToggle_->setTooltip("Enable/disable clip");
    clipEnabledToggle_->onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;

        const bool newState = !clip->enabled;
        clipEnabledToggle_->setActive(newState);
        magda::ClipBatchEdit batch("Enable/Disable Clips", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, newState ? "Enable Clip" : "Disable Clip",
                [newState](auto& manager, magda::ClipId id) {
                    manager.setClipEnabled(id, newState);
                }));
        }
    };
    addChildComponent(*clipEnabledToggle_);
}

void ClipInspector::saveToLibrary() {
    auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
    if (clip == nullptr)
        return;
    const bool savingMidiClip = clip->isMidi();
    std::optional<std::vector<magda::WarpMarker>> markers;
    if (clip->isAudio()) {
        // Edits are already in the model; the widgets are rounded copies. Only a
        // tempo-less event takes the cached detection it has been showing as a hint.
        auto* event = clip->primaryEvent();
        if (event != nullptr && !event->hasInterpretedBpm()) {
            const double cachedBpm = magda::AudioThumbnailManager::getInstance().getCachedBPM(
                audioEventRef(*clip).sourceFilePath());
            const auto display = magda::computeAudioClipSourceDisplay(
                *clip, projectBpm(), getAudioFileDurationForInspector(*clip), cachedBpm);
            if (display.bpm > 0.0) {
                magda::ClipManager::getInstance().adoptAnalysis(
                    primaryClipId(), event->sourceFilePath(), display.bpm);
            }
        }

        if (magda::audioEventRef(*clip).warpEnabled)
            markers = magda::audioEventRef(*clip).warpMarkers;
    }

    const bool saved =
        magda::ClipManager::getInstance().saveClipToLibrary(primaryClipId(), std::move(markers));
    updateFromSelectedClip();
    if (saved) {
        Toast::showGlobal("Saved to library");
    } else if (savingMidiClip) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Save MIDI Clip Failed",
            "Could not write the MIDI clip file or add it to the media library.");
    } else {
        Toast::showGlobal("Save to library failed");
    }
}

// ========================================================================
// Clip section: position and loop
// ========================================================================

void ClipInspector::initClipSection() {
    clipHeader_.setCaption("bar.beat.tick");
    clipHeader_.onCollapsedChanged = [this]() { updateFromSelectedClip(); };
    clipPropsContainer_.addAndMakeVisible(clipHeader_);

    clip_style::columnHeading(startColumnLabel_, "Start");
    clip_style::columnHeading(endColumnLabel_, "End");
    clip_style::columnHeading(lengthColumnLabel_, "Length");
    clip_style::caption(clipRowLabel_, "Clip");
    clip_style::caption(loopRowLabel_, "Loop");
    clip_style::columnHeading(clipLoopPhaseLabel_, "Phase");
    for (auto* label : {&startColumnLabel_, &endColumnLabel_, &lengthColumnLabel_, &clipRowLabel_,
                        &loopRowLabel_, &clipLoopPhaseLabel_})
        clipPropsContainer_.addChildComponent(*label);

    clipStartValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipStartValue_->setRange(0.0, 10000.0, 0.0);
    clipStartValue_->setDoubleClickResetsValue(false);
    clipStartValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        const double bpm = projectBpm();
        double currentValue = clipStartValue_->getValue();
        double deltaBeats = currentValue - multiStartDragStart_;
        magda::ClipBatchEdit batch("Move Clips", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->view != magda::ClipView::Session) {
                double newStart = juce::jmax(0.0, c->getStartBeats(bpm) + deltaBeats);
                batch.execute(std::make_unique<magda::MoveClipCommand>(
                    cid, magda::BeatPosition{newStart}, bpm));
            }
        }
        multiStartDragStart_ = currentValue;
    };
    clipPropsContainer_.addChildComponent(*clipStartValue_);

    clipEndValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipEndValue_->setRange(0.0, 10000.0, 4.0);
    clipEndValue_->setDoubleClickResetsValue(false);
    clipEndValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        const double bpm = projectBpm();
        double currentValue = clipEndValue_->getValue();
        double deltaBeats = currentValue - multiEndDragStart_;
        magda::ClipBatchEdit batch("Resize Clips", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->view != magda::ClipView::Session) {
                double newLength =
                    juce::jmax(minClipLengthBeats(bpm), c->getLengthInBeats() + deltaBeats);
                batch.execute(std::make_unique<magda::ResizeClipCommand>(
                    cid, magda::BeatDuration{newLength}, false, bpm));
            }
        }
        multiEndDragStart_ = currentValue;
    };
    clipPropsContainer_.addChildComponent(*clipEndValue_);

    clipLengthValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLengthValue_->setRange(0.25, 10000.0, 4.0);
    clipLengthValue_->setBarsBeatsIsPosition(false);
    clipLengthValue_->setDoubleClickResetsValue(false);
    clipLengthValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        const double bpm = projectBpm();
        const double currentValue = clipLengthValue_->getValue();
        const double deltaBeats = currentValue - multiLengthDragStart_;
        magda::ClipBatchEdit batch("Resize Clips", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->view != magda::ClipView::Session) {
                const double newLength =
                    juce::jmax(minClipLengthBeats(bpm), c->getLengthInBeats() + deltaBeats);
                batch.execute(std::make_unique<magda::ResizeClipCommand>(
                    cid, magda::BeatDuration{newLength}, false, bpm));
            }
        }
        multiLengthDragStart_ = currentValue;
    };
    clipPropsContainer_.addChildComponent(*clipLengthValue_);

    // Drives the same loop as the clip-editor toolbar's loop chip.
    clipLoopSwitch_.setTooltip("Loop");
    clipLoopSwitch_.onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        const bool newState = !clipLoopSwitch_.isOn();
        clipLoopSwitch_.setOn(newState);
        const double bpm = projectBpm();
        magda::ClipBatchEdit batch("Set Clip Loop", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Loop", [newState, bpm](auto& manager, magda::ClipId id) {
                    manager.setClipLoopEnabled(id, newState, bpm);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(clipLoopSwitch_);

    clipLoopStartValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLoopStartValue_->setRange(0.0, 10000.0, 0.0);
    clipLoopStartValue_->setDoubleClickResetsValue(true);
    clipLoopStartValue_->onValueChange = [this]() {
        if (primaryClipId() == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;

        const double bpm = projectBpm();
        double newLoopStartBeats = clipLoopStartValue_->getValue();
        if (clip->isMidi()) {
            const double newOffsetBeats =
                juce::jmax(0.0, newLoopStartBeats + clip->midiOffset - clip->loopStartBeats);
            magda::UndoManager::getInstance().beginCompoundOperation("Set Clip Loop Start");
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetMidiClipLoopStartBeatsCommand>(primaryClipId(),
                                                                          newLoopStartBeats, bpm));
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetClipOffsetCommand>(primaryClipId(), newOffsetBeats));
            magda::UndoManager::getInstance().endCompoundOperation();
            return;
        }

        double currentPhase = magda::audioEventRef(*clip).anchorSeconds() -
                              magda::audioEventRef(*clip).loopStartSeconds();
        double newLoopStartSeconds =
            displayBeatsToAudioSourceSeconds(*clip, newLoopStartBeats, bpm);
        newLoopStartSeconds = std::max(newLoopStartSeconds, 0.0);
        double newOffset = newLoopStartSeconds + currentPhase;
        // Atomic: change loopStart, then place offset to preserve phase. Undo
        // collapses both in a single step.
        magda::UndoManager::getInstance().beginCompoundOperation("Set Clip Loop Start");
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipLoopStartCommand>(primaryClipId(), newLoopStartSeconds,
                                                             bpm));
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetClipOffsetCommand>(primaryClipId(), newOffset));
        magda::UndoManager::getInstance().endCompoundOperation();
    };
    clipPropsContainer_.addChildComponent(*clipLoopStartValue_);

    // Loop end (derived: loopStart + loopLength)
    clipLoopEndValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLoopEndValue_->setRange(0.25, 10000.0, 4.0);
    clipLoopEndValue_->setDoubleClickResetsValue(false);
    clipLoopEndValue_->onValueChange = [this]() {
        if (primaryClipId() == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;

        const double bpm = projectBpm();
        double newLoopEndBeats = clipLoopEndValue_->getValue();

        double newLoopLengthSeconds = NAN;
        if (clip->isAudio()) {
            const auto info =
                magda::ClipDisplayInfo::from(*clip, bpm, getAudioFileDurationForInspector(*clip));
            const double newLoopEndSeconds =
                magda::TimelineUtils::beatsToSeconds(newLoopEndBeats, bpm);
            const double newDisplayLength =
                juce::jmax(0.0, newLoopEndSeconds - info.loopStartPositionSeconds);
            newLoopLengthSeconds = info.timelineToSource(newDisplayLength);
        } else {
            double loopStartBeats = clip->loopStartBeats;
            double newLoopLengthBeats = newLoopEndBeats - loopStartBeats;
            newLoopLengthBeats = std::max(newLoopLengthBeats, 0.25);
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetMidiClipLoopLengthBeatsCommand>(
                    primaryClipId(), newLoopLengthBeats, bpm));
            return;
        }
        newLoopLengthSeconds =
            juce::jmax(magda::ClipOperations::MIN_SOURCE_LENGTH, newLoopLengthSeconds);

        bool shouldResizeClip = false;
        double resizeLengthSeconds = 0.0;

        if (clip->view == magda::ClipView::Session) {
            double clipEndSeconds = timelineLengthSeconds(*clip, bpm);
            const auto info =
                magda::ClipDisplayInfo::from(*clip, bpm, getAudioFileDurationForInspector(*clip));
            const double newDisplayEnd =
                info.loopStartPositionSeconds + info.sourceToTimeline(newLoopLengthSeconds);
            const bool displayEndMatchedClipEnd =
                std::abs(info.loopEndPositionSeconds - clipEndSeconds) < 0.001;

            if (displayEndMatchedClipEnd && newDisplayEnd > clipEndSeconds) {
                shouldResizeClip = true;
                resizeLengthSeconds = newDisplayEnd;
            } else if (newDisplayEnd > clipEndSeconds) {
                const double clampedDisplayLength =
                    juce::jmax(0.0, clipEndSeconds - info.loopStartPositionSeconds);
                newLoopLengthSeconds = info.timelineToSource(clampedDisplayLength);
            }
        }

        magda::ClipBatchEdit batch("Set Clip Loop End", shouldResizeClip ? 2u : 1u);
        if (shouldResizeClip) {
            batch.execute(std::make_unique<magda::ResizeClipCommand>(
                primaryClipId(), magda::BeatDuration{resizeLengthSeconds * bpm / 60.0}, false,
                bpm));
        }

        const auto& event = magda::audioEventRef(*clip);
        if (event.hasInterpretedBpm()) {
            const double sourceBeats = newLoopLengthSeconds * event.interpBpm / 60.0;
            batch.execute(std::make_unique<magda::SetAudioClipLoopLengthBeatsCommand>(
                primaryClipId(), sourceBeats));
        } else {
            batch.execute(std::make_unique<magda::SetClipLoopLengthCommand>(
                primaryClipId(), newLoopLengthSeconds, bpm));
        }
    };
    clipPropsContainer_.addChildComponent(*clipLoopEndValue_);

    // Source read offset / loop phase (same model field, mode-specific label)
    clipLoopPhaseValue_ = std::make_unique<magda::BarsBeatsTicksLabel>();
    clipLoopPhaseValue_->setRange(0.0, 10000.0, 0.0);
    clipLoopPhaseValue_->setBarsBeatsIsPosition(false);
    clipLoopPhaseValue_->setDoubleClickResetsValue(true);
    clipLoopPhaseValue_->onValueChange = [this]() {
        if (primaryClipId() == magda::INVALID_CLIP_ID)
            return;
        const auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;

        double newPhaseOrOffsetBeats = std::max(0.0, clipLoopPhaseValue_->getValue());
        const bool loopOn = clip->view == magda::ClipView::Session || clip->loopEnabled ||
                            magda::audioEventRef(*clip).autoTempo;
        if (clip->isMidi()) {
            // MIDI phase/offset lives in midiOffset (beats)
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetClipOffsetCommand>(primaryClipId(),
                                                              newPhaseOrOffsetBeats));
        } else {
            double newSeconds =
                displayBeatsToAudioSourceSeconds(*clip, newPhaseOrOffsetBeats, projectBpm());
            if (loopOn) {
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetClipLoopPhaseCommand>(primaryClipId(), newSeconds));
            } else {
                magda::UndoManager::getInstance().executeCommand(
                    std::make_unique<magda::SetClipOffsetCommand>(primaryClipId(), newSeconds));
            }
        }
    };
    clipPropsContainer_.addChildComponent(*clipLoopPhaseValue_);

    takesSection_ = std::make_unique<ClipTakesSection>();
    clipPropsContainer_.addChildComponent(*takesSection_);

    chordProgressionSection_ = std::make_unique<ChordProgressionSection>();
    clipPropsContainer_.addChildComponent(*chordProgressionSection_);
}

// ========================================================================
// Warp & pitch section (audio)
// ========================================================================

void ClipInspector::initWarpSection() {
    warpHeader_.setAccessory(&clipWarpSwitch_, ToggleSwitch::kWidth);
    warpHeader_.onCollapsedChanged = [this]() { updateFromSelectedClip(); };
    clipPropsContainer_.addChildComponent(warpHeader_);

    clipWarpSwitch_.setTooltip("Warp markers");
    clipWarpSwitch_.onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        const bool newState = !clipWarpSwitch_.isOn();
        clipWarpSwitch_.setOn(newState);
        magda::ClipBatchEdit batch("Set Clip Warp", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Warp", [newState](auto& manager, magda::ClipId id) {
                    manager.setClipWarpEnabled(id, newState);
                }));
        }
    };

    auto getSelectedAudioClipIds = [this]() {
        std::vector<magda::ClipId> clipIds;
        for (auto cid : selectedClipIds_) {
            const auto* clip = magda::ClipManager::getInstance().getClip(cid);
            if (clip && clip->isAudio())
                clipIds.push_back(cid);
        }
        return clipIds;
    };

    auto applyAutoTempo = [this](bool enable, const std::vector<magda::ClipId>& clipIds) {
        const double bpm = projectBpm();
        magda::ClipBatchEdit batch("Set Clip Beat Mode", clipIds.size());
        for (auto cid : clipIds) {
            auto* clip = magda::ClipManager::getInstance().getClip(cid);
            if (!clip || !clip->isAudio())
                continue;
            batch.execute(std::make_unique<magda::SetPlaybackIntentCommand>(
                cid, enable ? magda::PlaybackIntent::Beat : magda::PlaybackIntent::Free, bpm,
                enable && magda::Config::getInstance().getAssumeProjectTempo()));
        }
        updateFromSelectedClip();
    };

    playbackModeStrip_.setOptions({"Beat", "Free"});
    playbackModeStrip_.setTooltip(
        "Beat: follow the project tempo in bars and beats.\nFree: play at the file's own rate.");
    playbackModeStrip_.onChange = [this, getSelectedAudioClipIds, applyAutoTempo](int index) {
        auto targetClipIds = getSelectedAudioClipIds();
        if (targetClipIds.empty())
            return;

        if (index == 1) {
            applyAutoTempo(false, targetClipIds);
            return;
        }

        // Clips no tier could answer for stay out of beat mode; say so once.
        auto applyAndReport = [this, applyAutoTempo](const std::vector<magda::ClipId>& clipIds) {
            applyAutoTempo(true, clipIds);
            for (auto cid : clipIds) {
                const auto* c = magda::ClipManager::getInstance().getClip(cid);
                if (!c || !c->isAudio())
                    continue;
                const auto& event = magda::audioEventRef(*c);
                if (event.autoTempo && event.bpmFrom == magda::Provenance::Project) {
                    Toast::showGlobal("No tempo found for " +
                                      juce::File(event.sourceFilePath()).getFileName() +
                                      ": assumed " + juce::String(event.interpBpm, 2) +
                                      " BPM. Edit it in the Tempo field.");
                    break;
                }
                if (event.autoTempo)
                    continue;
                Toast::showGlobal(
                    "No tempo found for " +
                    juce::File(magda::audioEventRef(*c).sourceFilePath()).getFileName() +
                    ". Type its BPM to use beat mode.");
                // The request stays pending, so the tempo field is live: open it.
                if (clipIds.size() == 1 && clipBpmValue_.isVisible() && clipBpmValue_.isEnabled())
                    clipBpmValue_.showEditor();
                break;
            }
        };

        // Beat mode needs a tempo the file may not have said yet, so ask for it
        // first, then switch (#2674). Beat mode plays at speed 1.0.
        playbackModeStrip_.setEnabled(false);
        juce::Component::SafePointer<SegmentStrip> strip(&playbackModeStrip_);
        magda::ClipManager::getInstance().detectMissingTempo(
            targetClipIds, projectBpm(), [strip, applyAndReport, targetClipIds]() {
                if (strip == nullptr)
                    return;
                strip->setEnabled(true);
                applyAndReport(targetClipIds);
            });
    };
    clipPropsContainer_.addChildComponent(playbackModeStrip_);

    clip_style::combo(stretchModeCombo_);
    // Mode values match TimeStretcher::Mode enum (combo ID = mode + 1)
    stretchModeCombo_.addItem("Off", time_stretch_mode::kDisabled + 1);
    stretchModeCombo_.addItem("Signalsmith", time_stretch_mode::kSignalsmith + 1);
    stretchModeCombo_.addItem("SoundTouch", time_stretch_mode::kSoundTouchNormal + 1);
    stretchModeCombo_.addItem("SoundTouch HQ", time_stretch_mode::kSoundTouchBetter + 1);
    stretchModeCombo_.setSelectedId(1, juce::dontSendNotification);
    stretchModeCombo_.setTooltip("Stretch engine");
    stretchModeCombo_.onChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        int mode = stretchModeCombo_.getSelectedId() - 1;
        magda::ClipBatchEdit batch("Set Clip Stretch Mode", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipStretchModeCommand>(cid, mode));
        }
    };
    clipPropsContainer_.addChildComponent(stretchModeCombo_);

    clip_style::caption(transientSensitivityLabel_, "Transient sensitivity");
    clipPropsContainer_.addChildComponent(transientSensitivityLabel_);

    clip_style::caption(araNoteLabel_, {});
    araNoteLabel_.setColour(juce::Label::textColourId,
                            ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    clipPropsContainer_.addChildComponent(araNoteLabel_);

    transientSensitivityValue_ =
        std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Percentage);
    transientSensitivityValue_->setRange(0.0, 1.0, 0.01);
    transientSensitivityValue_->setValue(0.5, juce::dontSendNotification);
    transientSensitivityValue_->setDoubleClickResetsValue(true);
    transientSensitivityValue_->setFont(FontManager::getInstance().getMonoFont(11.0f));
    transientSensitivityValue_->setFillColour(ActiveTheme::getAccentColour().withAlpha(0.22f));
    transientSensitivityValue_->onValueChange = [this]() {
        if (primaryClipId() == magda::INVALID_CLIP_ID)
            return;
        magda::WarpMarkerManager::getInstance().setTransientSensitivity(
            primaryClipId(), static_cast<float>(transientSensitivityValue_->getValue()));
    };
    clipPropsContainer_.addChildComponent(*transientSensitivityValue_);

    // Source tempo. A Label, not a drag field: typing is the edit, and the refresh paths
    // check isBeingEdited() so a landing tempo does not close the editor.
    clip_style::caption(tempoLabel_, "Tempo");
    clipPropsContainer_.addChildComponent(tempoLabel_);

    clipBpmValue_.setFont(FontManager::getInstance().getMonoFont(11.0f));
    clipBpmValue_.setJustificationType(juce::Justification::centredRight);
    clipBpmValue_.setBorderSize(juce::BorderSize<int>(0, 6, 0, 34));
    clipBpmValue_.setEditable(true);
    clipBpmValue_.onTextChange = [this]() {
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip || !clip->isAudio())
            return;

        // Older builds stored the unit in the text, so strip it defensively.
        juce::String text = clipBpmValue_.getText().trimCharactersAtEnd(" BPMbpm");
        double newBPM = text.getDoubleValue();
        if (newBPM < 20.0 || newBPM > 999.0) {
            updateFromSelectedClip();  // show what the clip holds, not the rejected text
            return;
        }

        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetSourceTempoCommand>(primaryClipId(), newBPM));
        updateFromSelectedClip();
    };
    clipPropsContainer_.addChildComponent(clipBpmValue_);

    clipBpmUnitLabel_.setText("BPM", juce::dontSendNotification);
    clipBpmUnitLabel_.setFont(FontManager::getInstance().getMonoFont(11.0f));
    clipBpmUnitLabel_.setJustificationType(juce::Justification::centredRight);
    clipBpmUnitLabel_.setInterceptsMouseClicks(false, false);
    clipPropsContainer_.addChildComponent(clipBpmUnitLabel_);

    // ÷2 / ×2 reinterpret the source at half or double its tempo; the beat count follows.
    auto scaleTempo = [this](double factor) {
        if (primaryClipId() == magda::INVALID_CLIP_ID || displayedSourceBpm_ <= 0.0)
            return;
        const double newBPM = displayedSourceBpm_ * factor;
        if (newBPM < 20.0 || newBPM > 999.0)
            return;
        magda::UndoManager::getInstance().executeCommand(
            std::make_unique<magda::SetSourceTempoCommand>(primaryClipId(), newBPM));
    };
    halveTempoButton_.setButtonText(juce::String::fromUTF8("\xc3\xb7"
                                                           "2"));
    halveTempoButton_.setTooltip("Halve the source tempo");
    halveTempoButton_.onClick = [scaleTempo]() { scaleTempo(0.5); };
    doubleTempoButton_.setButtonText(juce::String::fromUTF8("\xc3\x97"
                                                            "2"));
    doubleTempoButton_.setTooltip("Double the source tempo");
    doubleTempoButton_.onClick = [scaleTempo]() { scaleTempo(2.0); };
    for (auto* button : {&halveTempoButton_, &doubleTempoButton_}) {
        clip_style::action(*button);
        clipPropsContainer_.addChildComponent(*button);
    }

    // Source length in beats: the other half of the interpretation the tempo edits.
    clip_style::caption(sourceLengthLabel_, "Length");
    clipPropsContainer_.addChildComponent(sourceLengthLabel_);

    clipBeatsLengthValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    clipBeatsLengthValue_->setRange(0.25, 4096.0, 4.0);
    clipBeatsLengthValue_->setDecimalPlaces(2);
    clipBeatsLengthValue_->setSnapToInteger(true);
    clip_style::field(*clipBeatsLengthValue_, "beats");
    clipBeatsLengthValue_->onValueChange = [this]() {
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (clip != nullptr && clip->isAudio()) {
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetSourceBeatCountCommand>(
                    primaryClipId(), clipBeatsLengthValue_->getValue()));
        }
    };
    clipPropsContainer_.addChildComponent(*clipBeatsLengthValue_);

    // Speed ratio, for free clips (beat mode plays at 1.0).
    clip_style::caption(speedLabel_, "Speed");
    clipPropsContainer_.addChildComponent(speedLabel_);

    clipStretchValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    clipStretchValue_->setRange(0.25, 4.0, 1.0);
    clipStretchValue_->setDecimalPlaces(3);
    clip_style::field(*clipStretchValue_, "x");
    clipStretchValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double currentValue = clipStretchValue_->getValue();
        double delta = currentValue - multiSpeedRatioDragStart_;
        magda::ClipBatchEdit batch("Set Clip Speed", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio()) {
                double newVal =
                    juce::jlimit(0.25, 4.0, magda::audioEventRef(*c).speedRatio + delta);
                batch.execute(std::make_unique<magda::SetClipSpeedRatioCommand>(cid, newVal));
            }
        }
        multiSpeedRatioDragStart_ = currentValue;
        computeClipRange();
        refreshClipRangeDisplay();
    };
    clipPropsContainer_.addChildComponent(*clipStretchValue_);

    analogPitchToggle_.setButtonText("Analog");
    clip_style::toggle(analogPitchToggle_);
    analogPitchToggle_.setTooltip(
        "Analog pitch shift: resample instead of time-stretch.\n"
        "Changes playback speed to change pitch (tape/vinyl/sampler behavior).");
    analogPitchToggle_.onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;
        bool newState = !magda::audioEventRef(*clip).analogPitch;
        magda::ClipBatchEdit batch("Set Clip Analog Pitch", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Analog Pitch", [newState](auto& manager, magda::ClipId id) {
                    manager.setAnalogPitch(id, newState);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(analogPitchToggle_);

    // Pitch is one model value, pitchChange in semitones, shown as whole semitones and cents.
    clip_style::caption(pitchLabel_, "Pitch");
    clipPropsContainer_.addChildComponent(pitchLabel_);

    pitchSemitonesValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    pitchSemitonesValue_->setRange(-48.0, 48.0, 0.0);
    pitchSemitonesValue_->setDecimalPlaces(0);
    pitchSemitonesValue_->setSnapToInteger(true);
    clip_style::field(*pitchSemitonesValue_, "st");
    pitchSemitonesValue_->onValueChange = [this]() {
        const double current = pitchSemitonesValue_->getValue();
        applyPitchDelta(current - multiSemitonesDragStart_);
        multiSemitonesDragStart_ = current;
    };
    clipPropsContainer_.addChildComponent(*pitchSemitonesValue_);

    pitchCentsValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    pitchCentsValue_->setRange(-100.0, 100.0, 0.0);
    pitchCentsValue_->setDecimalPlaces(0);
    pitchCentsValue_->setSnapToInteger(true);
    clip_style::field(*pitchCentsValue_, "ct");
    pitchCentsValue_->onValueChange = [this]() {
        const double current = pitchCentsValue_->getValue();
        applyPitchDelta((current - multiCentsDragStart_) / 100.0);
        multiCentsDragStart_ = current;
    };
    clipPropsContainer_.addChildComponent(*pitchCentsValue_);

    // Source key root. ID 1 is "not set"; IDs 2-13 map onto kKeyRoots. Scale (major/minor)
    // is not exposed.
    clip_style::caption(clipKeyLabel_, "Key");
    clipPropsContainer_.addChildComponent(clipKeyLabel_);

    static constexpr const char* kKeyRoots[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                "F#", "G",  "G#", "A",  "A#", "B"};
    clip_style::combo(clipKeyRootCombo_);
    clipKeyRootCombo_.addItem("Not set", 1);
    for (int i = 0; i < 12; ++i)
        clipKeyRootCombo_.addItem(kKeyRoots[i], i + 2);
    clipKeyRootCombo_.setSelectedId(1, juce::dontSendNotification);
    clipKeyRootCombo_.onChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        const int rootId = clipKeyRootCombo_.getSelectedId();
        std::string root;
        if (rootId >= 2 && rootId <= 13)
            root = kKeyRoots[rootId - 2];
        // Mirror onto every selected audio clip's interpretation so the
        // inspector reflects the change immediately.
        for (auto cid : selectedClipIds_) {
            auto* clip = magda::ClipManager::getInstance().getClip(cid);
            if (clip == nullptr || !clip->isAudio())
                continue;
            if (auto* event = clip->primaryEvent())
                event->keyRoot = root;
            magda::ClipManager::getInstance().forceNotifyClipPropertyChanged(cid);
        }
    };
    clipPropsContainer_.addChildComponent(clipKeyRootCombo_);
}

void ClipInspector::applyPitchDelta(double semitones) {
    if (selectedClipIds_.empty() || semitones == 0.0)
        return;
    magda::ClipBatchEdit batch("Set Clip Pitch", selectedClipIds_.size());
    for (auto cid : selectedClipIds_) {
        const auto* c = magda::ClipManager::getInstance().getClip(cid);
        if (c && c->isAudio()) {
            const float newVal =
                juce::jlimit(-48.0f, 48.0f,
                             magda::audioEventRef(*c).pitchChange + static_cast<float>(semitones));
            batch.execute(std::make_unique<magda::SetClipPitchCommand>(cid, newVal));
        }
    }
    computeClipRange();
    refreshClipRangeDisplay();
}

// ========================================================================
// Session clip launch properties
// ========================================================================

void ClipInspector::initSessionLaunchSection() {
    launchHeader_.onCollapsedChanged = [this]() { updateFromSelectedClip(); };
    clipPropsContainer_.addChildComponent(launchHeader_);

    clip_style::caption(launchQuantizeLabel_, "Quantize");
    clipPropsContainer_.addChildComponent(launchQuantizeLabel_);

    launchQuantizeCombo_.addItem("None", 1);
    launchQuantizeCombo_.addItem("8 Bars", 2);
    launchQuantizeCombo_.addItem("4 Bars", 3);
    launchQuantizeCombo_.addItem("2 Bars", 4);
    launchQuantizeCombo_.addItem("1 Bar", 5);
    launchQuantizeCombo_.addItem("1/2", 6);
    launchQuantizeCombo_.addItem("1/4", 7);
    launchQuantizeCombo_.addItem("1/8", 8);
    launchQuantizeCombo_.addItem("1/16", 9);
    clip_style::combo(launchQuantizeCombo_);
    launchQuantizeCombo_.onChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto quantize =
            static_cast<magda::LaunchQuantize>(launchQuantizeCombo_.getSelectedId() - 1);
        magda::ClipBatchEdit batch("Set Clip Launch Quantize", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Launch Quantize", [quantize](auto& manager, magda::ClipId id) {
                    manager.setClipLaunchQuantize(id, quantize);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(launchQuantizeCombo_);

    clip_style::caption(followActionLabel_, "Follow action");
    clipPropsContainer_.addChildComponent(followActionLabel_);

    followActionCombo_.addItem("None", 1);
    followActionCombo_.addItem("Play Next", 2);
    followActionCombo_.addItem("Play Previous", 3);
    followActionCombo_.addItem("Play Random", 4);
    followActionCombo_.addItem("Stop", 5);
    followActionCombo_.addItem("Play Again", 6);
    clip_style::combo(followActionCombo_);
    followActionCombo_.onChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto action = static_cast<magda::FollowAction>(followActionCombo_.getSelectedId() - 1);
        magda::ClipBatchEdit batch("Set Clip Follow Action", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Follow Action", [action](auto& manager, magda::ClipId id) {
                    manager.setClipFollowAction(id, action);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(followActionCombo_);

    clip_style::caption(followActionDelayLabel_, "Delay (beats)");
    clipPropsContainer_.addChildComponent(followActionDelayLabel_);

    followActionDelaySlider_.setRange(0.0, 64.0, 0.25);
    followActionDelaySlider_.setOrientation(TextSlider::Orientation::Horizontal);
    followActionDelaySlider_.setDefaultValue(0.0);
    followActionDelaySlider_.onValueChanged = [this](double delayBeats) {
        if (selectedClipIds_.empty())
            return;
        magda::ClipBatchEdit batch("Set Clip Follow Delay", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Follow Delay", [delayBeats](auto& manager, magda::ClipId id) {
                    manager.setClipFollowActionDelayBeats(id, delayBeats);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(followActionDelaySlider_);

    clip_style::caption(followActionLoopCountLabel_, "Loops");
    clipPropsContainer_.addChildComponent(followActionLoopCountLabel_);

    followActionLoopCountSlider_.setRange(1.0, 64.0, 1.0);
    followActionLoopCountSlider_.setOrientation(TextSlider::Orientation::Horizontal);
    followActionLoopCountSlider_.setDefaultValue(1.0);
    followActionLoopCountSlider_.setValueFormatter(
        [](double value) { return juce::String(static_cast<int>(std::round(value))); });
    followActionLoopCountSlider_.setValueParser([](const juce::String& text) {
        return static_cast<double>(static_cast<int>(std::round(text.getDoubleValue())));
    });
    followActionLoopCountSlider_.onValueChanged = [this](double value) {
        if (selectedClipIds_.empty())
            return;
        const int loopCount = static_cast<int>(std::round(value));
        magda::ClipBatchEdit batch("Set Clip Follow Loops", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipPropertyCommand>(
                cid, "Set Clip Follow Loops", [loopCount](auto& manager, magda::ClipId id) {
                    manager.setClipFollowActionLoopCount(id, loopCount);
                }));
        }
    };
    clipPropsContainer_.addChildComponent(followActionLoopCountSlider_);
}

// ========================================================================
// Clip properties viewport (scrollable container)
// ========================================================================

void ClipInspector::initViewport() {
    clipPropsViewport_.setViewedComponent(&clipPropsContainer_, false);
    clipPropsViewport_.setScrollBarsShown(true, false);
    clipPropsViewport_.setScrollBarThickness(6);
    addChildComponent(clipPropsViewport_);
}

// ========================================================================
// Pitch section (MIDI): destructive transpose of the clip's notes
// ========================================================================

void ClipInspector::initPitchSection() {
    midiPitchHeader_.onCollapsedChanged = [this]() { updateFromSelectedClip(); };
    clipPropsContainer_.addChildComponent(midiPitchHeader_);

    clip_style::caption(midiTransposeLabel_, "Transpose");
    clipPropsContainer_.addChildComponent(midiTransposeLabel_);

    // The notes move; nothing is stored, so the middle names the action rather than a value.
    midiTransposeHint_.setText("notes", juce::dontSendNotification);
    midiTransposeHint_.setFont(FontManager::getInstance().getMonoFont(11.0f));
    midiTransposeHint_.setColour(juce::Label::textColourId,
                                 ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
    midiTransposeHint_.setJustificationType(juce::Justification::centred);
    midiTransposeHint_.setInterceptsMouseClicks(false, false);
    clipPropsContainer_.addChildComponent(midiTransposeHint_);

    static constexpr int kSteps[] = {-12, -1, 1, 12};
    for (size_t i = 0; i < midiTransposeButtons_.size(); ++i) {
        auto& button = midiTransposeButtons_[i];
        const int semitones = kSteps[i];
        button.setButtonText((semitones > 0 ? "+" : juce::String::fromUTF8("\xe2\x88\x92")) +
                             juce::String(std::abs(semitones)));
        button.setTooltip("Transpose notes " + juce::String(semitones > 0 ? "up " : "down ") +
                          (std::abs(semitones) == 12 ? "an octave" : "a semitone"));
        clip_style::action(button);
        button.onClick = [this, semitones]() {
            if (selectedClipIds_.empty())
                return;
            magda::ClipBatchEdit batch("Transpose MIDI Clips", selectedClipIds_.size());
            for (auto cid : selectedClipIds_) {
                const auto* c = magda::ClipManager::getInstance().getClip(cid);
                if (c && c->isMidi())
                    batch.execute(
                        std::make_unique<magda::TransposeMidiClipCommand>(cid, semitones));
            }
        };
        clipPropsContainer_.addChildComponent(button);
    }
}

// ========================================================================
// Per-clip mix section
// ========================================================================

void ClipInspector::initMixSection() {
    mixHeader_.onCollapsedChanged = [this]() { updateFromSelectedClip(); };
    clipPropsContainer_.addChildComponent(mixHeader_);

    clip_style::caption(volumeLabel_, "Volume");
    clip_style::caption(panLabel_, "Pan");
    clip_style::caption(gainLabel_, "Gain");
    for (auto* label : {&volumeLabel_, &panLabel_, &gainLabel_})
        clipPropsContainer_.addChildComponent(*label);

    const auto mixFill = ActiveTheme::getAccentColour().withAlpha(0.22f);

    clipVolumeValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Decibels);
    clipVolumeValue_->setRange(-100.0, 0.0, 0.0);
    clipVolumeValue_->setFont(FontManager::getInstance().getMonoFont(11.0f));
    clipVolumeValue_->setFillColour(mixFill);
    clipVolumeValue_->setUnit("dB");
    clipVolumeValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double currentValue = clipVolumeValue_->getValue();
        double delta = currentValue - multiVolumeDragStart_;
        magda::ClipBatchEdit batch("Set Clip Volume", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio()) {
                float newVal = juce::jlimit(-100.0f, 0.0f, c->volumeDB + static_cast<float>(delta));
                batch.execute(std::make_unique<magda::SetClipVolumeDBCommand>(cid, newVal));
            }
        }
        multiVolumeDragStart_ = currentValue;
        computeClipRange();
        refreshClipRangeDisplay();
    };
    clipPropsContainer_.addChildComponent(*clipVolumeValue_);

    clipPanValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Pan);
    clipPanValue_->setRange(-1.0, 1.0, 0.0);
    clipPanValue_->setFont(FontManager::getInstance().getMonoFont(11.0f));
    clipPanValue_->setFillColour(mixFill);
    clipPanValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double currentValue = clipPanValue_->getValue();
        double delta = currentValue - multiPanDragStart_;
        magda::ClipBatchEdit batch("Set Clip Pan", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio()) {
                float newVal = juce::jlimit(-1.0f, 1.0f, c->pan + static_cast<float>(delta));
                batch.execute(std::make_unique<magda::SetClipPanCommand>(cid, newVal));
            }
        }
        multiPanDragStart_ = currentValue;
        computeClipRange();
        refreshClipRangeDisplay();
    };
    clipPropsContainer_.addChildComponent(*clipPanValue_);

    clipGainValue_ = std::make_unique<DraggableValueLabel>(DraggableValueLabel::Format::Raw);
    clipGainValue_->setRange(0.0, 24.0, 0.0);
    clipGainValue_->setDecimalPlaces(1);
    clipGainValue_->setFont(FontManager::getInstance().getMonoFont(11.0f));
    clipGainValue_->setFillColour(mixFill);
    clipGainValue_->setUnit("dB");
    clipGainValue_->onValueChange = [this]() {
        if (selectedClipIds_.empty())
            return;
        double currentValue = clipGainValue_->getValue();
        double delta = currentValue - multiGainDragStart_;
        magda::ClipBatchEdit batch("Set Clip Gain", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            const auto* c = magda::ClipManager::getInstance().getClip(cid);
            if (c && c->isAudio()) {
                float newVal = juce::jlimit(0.0f, 24.0f, c->gainDB + static_cast<float>(delta));
                batch.execute(std::make_unique<magda::SetClipGainDBCommand>(cid, newVal));
            }
        }
        multiGainDragStart_ = currentValue;
        computeClipRange();
        refreshClipRangeDisplay();
    };
    clipPropsContainer_.addChildComponent(*clipGainValue_);

    reverseToggle_.setButtonText("Reverse");
    clip_style::toggle(reverseToggle_);
    reverseToggle_.onClick = [this]() {
        if (selectedClipIds_.empty())
            return;
        auto* clip = magda::ClipManager::getInstance().getClip(primaryClipId());
        if (!clip)
            return;
        bool newState = !magda::audioEventRef(*clip).reversed;
        magda::ClipBatchEdit batch("Set Clip Reverse", selectedClipIds_.size());
        for (auto cid : selectedClipIds_) {
            batch.execute(std::make_unique<magda::SetClipReversedCommand>(cid, newState));
        }
    };
    clipPropsContainer_.addChildComponent(reverseToggle_);

    // A view flag, so not an undo step; the primary clip decides, as for reverse.
    gainEnvelopeToggle_.setButtonText("Gain envelope");
    clip_style::toggle(gainEnvelopeToggle_);
    gainEnvelopeToggle_.onClick = [this]() {
        auto& clipManager = magda::ClipManager::getInstance();
        const auto* clip = clipManager.getClip(primaryClipId());
        if (clip == nullptr)
            return;
        const auto shown = !clip->gainEnvelopeShown;
        for (auto cid : selectedClipIds_)
            clipManager.setGainEnvelopeShown(cid, shown);
    };
    clipPropsContainer_.addChildComponent(gainEnvelopeToggle_);
}

// ========================================================================
// Fades section
// ========================================================================

void ClipInspector::initFadesSection() {
    fadesSection_ = std::make_unique<ClipFadesSection>();
    fadesSection_->onCollapsedChanged = [this]() { updateFromSelectedClip(); };
    clipPropsContainer_.addChildComponent(*fadesSection_);
}

void ClipInspector::initTimeBendSection() {
    timeBendSection_ = std::make_unique<ClipTimeBendSection>();
    timeBendSection_->onCollapsedChanged = [this]() { updateFromSelectedClip(); };
    clipPropsContainer_.addChildComponent(*timeBendSection_);
}

void ClipInspector::initLabelsSection() {
    labelsSection_ = std::make_unique<ClipLabelsSection>();
    labelsSection_->onCollapsedChanged = [this]() { updateFromSelectedClip(); };
    clipPropsContainer_.addChildComponent(*labelsSection_);
}

}  // namespace magda::daw::ui
