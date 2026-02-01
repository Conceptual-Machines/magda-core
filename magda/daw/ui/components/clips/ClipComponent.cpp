#include "ClipComponent.hpp"

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "../tracks/TrackContentPanel.hpp"
#include "audio/AudioThumbnailManager.hpp"
#include "core/SelectionManager.hpp"

namespace magda {

ClipComponent::ClipComponent(ClipId clipId, TrackContentPanel* parent)
    : clipId_(clipId), parentPanel_(parent) {
    setName("ClipComponent");

    // Register as ClipManager listener
    ClipManager::getInstance().addListener(this);

    // Check if this clip is currently selected
    isSelected_ = ClipManager::getInstance().getSelectedClip() == clipId_;
}

ClipComponent::~ClipComponent() {
    ClipManager::getInstance().removeListener(this);
}

void ClipComponent::paint(juce::Graphics& g) {
    const auto* clip = getClipInfo();
    if (!clip) {
        return;
    }

    auto bounds = getLocalBounds();

    // Draw based on clip type
    if (clip->type == ClipType::Audio) {
        paintAudioClip(g, *clip, bounds);
    } else {
        paintMidiClip(g, *clip, bounds);
    }

    // Draw header (name, loop indicator)
    paintClipHeader(g, *clip, bounds);

    // Draw resize handles if selected
    if (isSelected_) {
        paintResizeHandles(g, bounds);
    }

    // Marquee highlight overlay (during marquee drag)
    if (isMarqueeHighlighted_) {
        g.setColour(juce::Colours::white.withAlpha(0.2f));
        g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);
    }

    // Selection border - show for both single selection and multi-selection
    if (isSelected_ || SelectionManager::getInstance().isClipSelected(clipId_)) {
        g.setColour(juce::Colours::white);
        g.drawRect(bounds, 2);
    }
}

void ClipComponent::paintAudioClip(juce::Graphics& g, const ClipInfo& clip,
                                   juce::Rectangle<int> bounds) {
    // Background - slightly darker than clip colour
    auto bgColour = clip.colour.darker(0.3f);
    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);

    // Waveform area (below header)
    auto waveformArea = bounds.reduced(2, HEADER_HEIGHT + 2);

    if (!clip.audioSources.empty() && clip.audioSources[0].filePath.isNotEmpty()) {
        const auto& source = clip.audioSources[0];
        auto& thumbnailManager = AudioThumbnailManager::getInstance();

        // Calculate visible region and file times directly in time domain
        // to avoid integer rounding errors from pixel→time→pixel conversions.
        double clipDisplayLength = clip.length;
        bool isResizeMode =
            (dragMode_ == DragMode::ResizeLeft || dragMode_ == DragMode::ResizeRight);
        bool isStretchMode =
            (dragMode_ == DragMode::StretchLeft || dragMode_ == DragMode::StretchRight);

        if (isDragging_ && (isResizeMode || isStretchMode) && previewLength_ > 0.0) {
            clipDisplayLength = previewLength_;
        }

        double pixelsPerSecond =
            (clipDisplayLength > 0.0)
                ? static_cast<double>(waveformArea.getWidth()) / clipDisplayLength
                : 0.0;

        if (pixelsPerSecond > 0.0) {
            // During left resize drag, simulate the source position adjustment
            // that will happen on commit. The clip start moves, so source.position
            // must shift by (previewLength - dragStartLength) to stay anchored.
            double adjustedSourcePosition = source.position;
            if (isDragging_ && dragMode_ == DragMode::ResizeLeft) {
                adjustedSourcePosition += (previewLength_ - dragStartLength_);
            }

            // Compute visible time region (clip-relative) by clamping source to clip bounds
            double visibleStart = juce::jmax(adjustedSourcePosition, 0.0);
            double visibleEnd =
                juce::jmin(adjustedSourcePosition + source.length, clipDisplayLength);

            if (visibleEnd > visibleStart) {
                // Convert visible region to pixels
                int drawX =
                    waveformArea.getX() + static_cast<int>(visibleStart * pixelsPerSecond + 0.5);
                int drawRight =
                    waveformArea.getX() + static_cast<int>(visibleEnd * pixelsPerSecond + 0.5);
                auto drawRect = juce::Rectangle<int>(drawX, waveformArea.getY(), drawRight - drawX,
                                                     waveformArea.getHeight());

                // Compute file time range directly from visible time region
                double fileStart =
                    source.offset + (visibleStart - adjustedSourcePosition) / source.stretchFactor;
                double fileEnd =
                    source.offset + (visibleEnd - adjustedSourcePosition) / source.stretchFactor;

                thumbnailManager.drawWaveform(g, drawRect, source.filePath, fileStart, fileEnd,
                                              clip.colour.brighter(0.2f));
            }
        }
    } else {
        // Fallback: draw placeholder if no audio source
        g.setColour(clip.colour.brighter(0.2f).withAlpha(0.3f));
        g.drawText("No Audio", waveformArea, juce::Justification::centred);
    }

    // Border
    g.setColour(clip.colour);
    g.drawRoundedRectangle(bounds.toFloat(), CORNER_RADIUS, 1.0f);
}

void ClipComponent::paintMidiClip(juce::Graphics& g, const ClipInfo& clip,
                                  juce::Rectangle<int> bounds) {
    // Background
    auto bgColour = clip.colour.darker(0.3f);
    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);

    // MIDI note representation area
    auto noteArea = bounds.reduced(2, HEADER_HEIGHT + 2);

    // Draw MIDI notes if we have them
    if (!clip.midiNotes.empty() && noteArea.getHeight() > 5) {
        g.setColour(clip.colour.brighter(0.3f));

        // Calculate clip length in beats using actual tempo
        double tempo = parentPanel_ ? parentPanel_->getTempo() : 120.0;
        double beatsPerSecond = tempo / 60.0;
        double clipLengthInBeats = clip.length * beatsPerSecond;
        double midiOffset = clip.midiOffset;

        // Use absolute MIDI range (0-127) for consistent vertical positioning across all clips
        const int MIDI_MIN = 0;
        const int MIDI_MAX = 127;
        const int MIDI_RANGE = MIDI_MAX - MIDI_MIN;
        double beatRange = juce::jmax(1.0, clipLengthInBeats);

        // Draw each note as a small rectangle
        for (const auto& note : clip.midiNotes) {
            // Notes are stored relative to clip start
            double displayStart = note.startBeat - midiOffset;
            double displayEnd = displayStart + note.lengthBeats;

            // Skip notes completely outside visible range
            if (displayEnd <= 0 || displayStart >= clipLengthInBeats)
                continue;

            // Clip note to visible range [0, clipLengthInBeats]
            double visibleStart = juce::jmax(0.0, displayStart);
            double visibleEnd = juce::jmin(clipLengthInBeats, displayEnd);
            double visibleLength = visibleEnd - visibleStart;

            // Absolute vertical position based on MIDI note number (0-127)
            float noteY = noteArea.getY() +
                          (MIDI_MAX - note.noteNumber) * noteArea.getHeight() / (MIDI_RANGE + 1);
            float noteHeight =
                juce::jmax(1.5f, static_cast<float>(noteArea.getHeight()) / (MIDI_RANGE + 1));
            float noteX = noteArea.getX() +
                          static_cast<float>(visibleStart / beatRange) * noteArea.getWidth();
            float noteWidth = juce::jmax(2.0f, static_cast<float>(visibleLength / beatRange) *
                                                   noteArea.getWidth());

            g.fillRoundedRectangle(noteX, noteY, noteWidth, noteHeight, 1.0f);
        }
    } else {
        // Draw placeholder pattern for empty MIDI clip
        g.setColour(clip.colour.withAlpha(0.3f));
        for (int i = 0; i < 4; i++) {
            int y = noteArea.getY() + i * (noteArea.getHeight() / 4);
            g.drawHorizontalLine(y, noteArea.getX(), noteArea.getRight());
        }
    }

    // Border
    g.setColour(clip.colour);
    g.drawRoundedRectangle(bounds.toFloat(), CORNER_RADIUS, 1.0f);
}

void ClipComponent::paintClipHeader(juce::Graphics& g, const ClipInfo& clip,
                                    juce::Rectangle<int> bounds) {
    auto headerArea = bounds.removeFromTop(HEADER_HEIGHT);

    // Header background
    g.setColour(clip.colour);
    g.fillRoundedRectangle(headerArea.toFloat().withBottom(headerArea.getBottom() + 2),
                           CORNER_RADIUS);

    // Clip name
    if (bounds.getWidth() > MIN_WIDTH_FOR_NAME) {
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.drawText(clip.name, headerArea.reduced(4, 0), juce::Justification::centredLeft, true);
    }

    // Loop indicator
    if (clip.internalLoopEnabled) {
        auto loopArea = headerArea.removeFromRight(14).reduced(2);
        g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND));
        g.drawText("L", loopArea, juce::Justification::centred, false);
    }
}

void ClipComponent::paintResizeHandles(juce::Graphics& g, juce::Rectangle<int> bounds) {
    auto handleColour = juce::Colours::white.withAlpha(0.5f);

    // Left handle
    auto leftHandle = bounds.removeFromLeft(RESIZE_HANDLE_WIDTH);
    if (hoverLeftEdge_) {
        g.setColour(handleColour);
        g.fillRect(leftHandle);
    }

    // Right handle
    auto rightHandle = bounds.removeFromRight(RESIZE_HANDLE_WIDTH);
    if (hoverRightEdge_) {
        g.setColour(handleColour);
        g.fillRect(rightHandle);
    }
}

void ClipComponent::resized() {
    // Nothing to do - clip bounds are set by parent
}

bool ClipComponent::hitTest(int x, int y) {
    // Determine if click is in upper vs lower zone based on TRACK height, not clip height
    // This ensures zone detection is consistent with TrackContentPanel::isInUpperTrackZone

    if (!parentPanel_) {
        // Fallback to clip-based detection
        int midY = getHeight() / 2;
        return y < midY && x >= 0 && x < getWidth();
    }

    // Convert local y to parent coordinates
    int parentY = getY() + y;

    // Check if click is in lower half of the track
    // Using the same logic as TrackContentPanel::isInUpperTrackZone
    int trackIndex = parentPanel_->getTrackIndexAtY(parentY);
    if (trackIndex < 0) {
        // Can't determine track, use clip-based fallback
        int midY = getHeight() / 2;
        return y < midY && x >= 0 && x < getWidth();
    }

    // Calculate track midpoint (same as isInUpperTrackZone)
    int trackY = parentPanel_->getTrackYPosition(trackIndex);
    int trackHeight = parentPanel_->getTrackHeight(trackIndex);
    int trackMidY = trackY + trackHeight / 2;

    // If click is in lower half of the track, let parent handle it
    if (parentY >= trackMidY) {
        return false;
    }

    // Click is in upper zone - check x bounds
    return x >= 0 && x < getWidth() && y >= 0;
}

// ============================================================================
// Mouse Handling
// ============================================================================

void ClipComponent::mouseDown(const juce::MouseEvent& e) {
    const auto* clip = getClipInfo();
    if (!clip) {
        return;
    }

    // Ensure parent panel has keyboard focus so shortcuts work
    if (parentPanel_) {
        parentPanel_->grabKeyboardFocus();
    }

    auto& selectionManager = SelectionManager::getInstance();
    bool isAlreadySelected = selectionManager.isClipSelected(clipId_);

    // Handle Cmd/Ctrl+click for toggle selection
    if (e.mods.isCommandDown()) {
        selectionManager.toggleClipSelection(clipId_);
        // Update local state
        isSelected_ = selectionManager.isClipSelected(clipId_);

        // Don't start dragging on Cmd+click - it's just for selection
        dragMode_ = DragMode::None;
        repaint();
        return;
    }

    // Handle Shift+click on edges for stretch, otherwise range selection
    if (e.mods.isShiftDown()) {
        if (isOnLeftEdge(e.x) || isOnRightEdge(e.x)) {
            // Shift+edge = stretch mode — fall through to drag setup below
        } else {
            selectionManager.extendSelectionTo(clipId_);
            isSelected_ = selectionManager.isClipSelected(clipId_);
            dragMode_ = DragMode::None;
            repaint();
            return;
        }
    }

    // Handle Alt+click for blade/split
    if (e.mods.isAltDown() && !e.mods.isCommandDown() && !e.mods.isShiftDown()) {
        // Calculate split time from click position
        if (parentPanel_) {
            auto parentPos = e.getEventRelativeTo(parentPanel_).getPosition();
            double splitTime = parentPanel_->pixelToTime(parentPos.x);

            // Apply snap if available
            if (snapTimeToGrid) {
                splitTime = snapTimeToGrid(splitTime);
            }

            // Verify split time is within clip bounds
            if (splitTime > clip->startTime && splitTime < clip->startTime + clip->length) {
                if (onClipSplit) {
                    onClipSplit(clipId_, splitTime);
                }
            }
        }
        dragMode_ = DragMode::None;
        return;
    }

    // If clicking on a clip that's already part of a multi-selection,
    // keep the selection and prepare for potential multi-drag
    if (isAlreadySelected && selectionManager.getSelectedClipCount() > 1) {
        // Keep existing multi-selection, prepare for multi-drag
        isSelected_ = true;
    } else {
        // Single click on unselected clip - select only this one
        selectionManager.selectClip(clipId_);
        isSelected_ = true;
    }

    if (onClipSelected) {
        onClipSelected(clipId_);
    }

    // Store drag start info - use parent's coordinate space so position
    // is stable when we move the component via setBounds()
    if (parentPanel_) {
        dragStartPos_ = e.getEventRelativeTo(parentPanel_).getPosition();
    } else {
        dragStartPos_ = e.getPosition();
    }
    dragStartBoundsPos_ = getBounds().getPosition();
    dragStartTime_ = clip->startTime;
    dragStartLength_ = clip->length;
    dragStartTrackId_ = clip->trackId;

    // Initialize preview state
    previewStartTime_ = clip->startTime;
    previewLength_ = clip->length;
    isDragging_ = false;

    // Determine drag mode based on click position
    // Shift+edge = stretch mode (time-stretches audio source along with clip)
    if (isOnLeftEdge(e.x)) {
        if (e.mods.isShiftDown() && clip->type == ClipType::Audio && !clip->audioSources.empty()) {
            dragMode_ = DragMode::StretchLeft;
            dragStartStretchFactor_ = clip->audioSources[0].stretchFactor;
        } else {
            dragMode_ = DragMode::ResizeLeft;
        }
    } else if (isOnRightEdge(e.x)) {
        if (e.mods.isShiftDown() && clip->type == ClipType::Audio && !clip->audioSources.empty()) {
            dragMode_ = DragMode::StretchRight;
            dragStartStretchFactor_ = clip->audioSources[0].stretchFactor;
        } else {
            dragMode_ = DragMode::ResizeRight;
        }
    } else {
        dragMode_ = DragMode::Move;
    }

    repaint();
}

void ClipComponent::mouseDrag(const juce::MouseEvent& e) {
    if (dragMode_ == DragMode::None || !parentPanel_) {
        return;
    }

    const auto* clip = getClipInfo();
    if (!clip) {
        return;
    }

    // Check if this is a multi-clip drag
    auto& selectionManager = SelectionManager::getInstance();
    bool isMultiDrag = dragMode_ == DragMode::Move && selectionManager.getSelectedClipCount() > 1 &&
                       selectionManager.isClipSelected(clipId_);

    if (isMultiDrag) {
        // Delegate to parent for coordinated multi-clip movement
        if (!isDragging_) {
            // First drag event - start multi-clip drag
            parentPanel_->startMultiClipDrag(clipId_,
                                             e.getEventRelativeTo(parentPanel_).getPosition());
            isDragging_ = true;
        } else {
            // Continue multi-clip drag
            parentPanel_->updateMultiClipDrag(e.getEventRelativeTo(parentPanel_).getPosition());
        }
        return;
    }

    // Single clip drag logic
    isDragging_ = true;

    // Alt+drag to duplicate: mark for duplication (created in mouseUp to avoid re-entrancy)
    if (dragMode_ == DragMode::Move && e.mods.isAltDown() && !isDuplicating_) {
        isDuplicating_ = true;
    }

    // Convert pixel delta to time delta
    double pixelsPerSecond = parentPanel_->getZoom();
    if (pixelsPerSecond <= 0) {
        return;
    }

    // Use parent's coordinate space for stable delta calculation
    // (component position changes during drag, but parent doesn't move)
    auto parentPos = e.getEventRelativeTo(parentPanel_).getPosition();
    int deltaX = parentPos.x - dragStartPos_.x;
    double deltaTime = deltaX / pixelsPerSecond;

    switch (dragMode_) {
        case DragMode::Move: {
            // Work entirely in time domain, then convert to pixels at the end
            double rawStartTime = juce::jmax(0.0, dragStartTime_ + deltaTime);
            double finalTime = rawStartTime;

            // Magnetic snap: if close to grid, snap to it
            if (snapTimeToGrid) {
                double snappedTime = snapTimeToGrid(rawStartTime);
                double snapDeltaPixels = std::abs((snappedTime - rawStartTime) * pixelsPerSecond);
                if (snapDeltaPixels <= SNAP_THRESHOLD_PIXELS) {
                    finalTime = snappedTime;
                }
            }

            previewStartTime_ = finalTime;

            if (isDuplicating_) {
                // Alt+drag duplicate: show ghost at NEW position, keep original in place
                const auto* clip = getClipInfo();
                if (clip && parentPanel_) {
                    int ghostX = parentPanel_->timeToPixel(finalTime);
                    int ghostWidth = static_cast<int>(dragStartLength_ * pixelsPerSecond);
                    juce::Rectangle<int> ghostBounds(ghostX, getY(), juce::jmax(10, ghostWidth),
                                                     getHeight());
                    parentPanel_->setClipGhost(clipId_, ghostBounds, clip->colour);
                }
                // Don't move the original clip component
            } else {
                // Normal move: update component position
                int newX = parentPanel_->timeToPixel(finalTime);
                int newWidth = static_cast<int>(dragStartLength_ * pixelsPerSecond);
                setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());
            }
            break;
        }

        case DragMode::ResizeLeft: {
            // Work in time domain: resizing from left changes start time and length
            double rawStartTime = juce::jmax(0.0, dragStartTime_ + deltaTime);
            double endTime = dragStartTime_ + dragStartLength_;  // End stays fixed
            double finalStartTime = rawStartTime;

            // Magnetic snap for left edge
            if (snapTimeToGrid) {
                double snappedTime = snapTimeToGrid(rawStartTime);
                double snapDeltaPixels = std::abs((snappedTime - rawStartTime) * pixelsPerSecond);
                if (snapDeltaPixels <= SNAP_THRESHOLD_PIXELS) {
                    finalStartTime = snappedTime;
                }
            }

            // Ensure minimum length
            finalStartTime = juce::jmin(finalStartTime, endTime - 0.1);
            double finalLength = endTime - finalStartTime;

            previewStartTime_ = finalStartTime;
            previewLength_ = finalLength;

            // Convert to pixels (using parent's method to account for padding)
            int newX = parentPanel_->timeToPixel(finalStartTime);
            int newWidth = static_cast<int>(finalLength * pixelsPerSecond);
            setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());
            break;
        }

        case DragMode::ResizeRight: {
            // Work in time domain: resizing from right changes length only
            double rawEndTime = dragStartTime_ + dragStartLength_ + deltaTime;
            double finalEndTime = rawEndTime;

            // Magnetic snap for right edge (end time)
            if (snapTimeToGrid) {
                double snappedEndTime = snapTimeToGrid(rawEndTime);
                double snapDeltaPixels = std::abs((snappedEndTime - rawEndTime) * pixelsPerSecond);
                if (snapDeltaPixels <= SNAP_THRESHOLD_PIXELS) {
                    finalEndTime = snappedEndTime;
                }
            }

            // Ensure minimum length
            double finalLength = juce::jmax(0.1, finalEndTime - dragStartTime_);
            previewLength_ = finalLength;

            // Convert to pixels (using parent's method to account for padding)
            int newX = parentPanel_->timeToPixel(dragStartTime_);
            int newWidth = static_cast<int>(finalLength * pixelsPerSecond);
            setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());
            break;
        }

        case DragMode::StretchRight: {
            // Shift+right edge: stretch clip and audio source proportionally
            double rawEndTime = dragStartTime_ + dragStartLength_ + deltaTime;
            double finalEndTime = rawEndTime;

            if (snapTimeToGrid) {
                double snappedEndTime = snapTimeToGrid(rawEndTime);
                double snapDeltaPixels = std::abs((snappedEndTime - rawEndTime) * pixelsPerSecond);
                if (snapDeltaPixels <= SNAP_THRESHOLD_PIXELS) {
                    finalEndTime = snappedEndTime;
                }
            }

            double finalLength = juce::jmax(0.1, finalEndTime - dragStartTime_);

            // Clamp by stretch factor limits [0.25, 4.0]
            double stretchRatio = finalLength / dragStartLength_;
            double newStretchFactor = dragStartStretchFactor_ * stretchRatio;
            newStretchFactor = juce::jlimit(0.25, 4.0, newStretchFactor);
            // Back-compute the allowed length from the clamped stretch factor
            finalLength = dragStartLength_ * (newStretchFactor / dragStartStretchFactor_);

            previewLength_ = finalLength;

            int newX = parentPanel_->timeToPixel(dragStartTime_);
            int newWidth = static_cast<int>(finalLength * pixelsPerSecond);
            setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());

            // Throttled live update to audio engine
            if (stretchThrottle_.check()) {
                auto& cm = ClipManager::getInstance();
                if (auto* mutableClip = cm.getClip(clipId_)) {
                    mutableClip->length = finalLength;
                    if (!mutableClip->audioSources.empty()) {
                        mutableClip->audioSources[0].length = finalLength;
                        mutableClip->audioSources[0].stretchFactor = newStretchFactor;
                    }
                    cm.forceNotifyClipPropertyChanged(clipId_);
                }
            }
            break;
        }

        case DragMode::StretchLeft: {
            // Shift+left edge: stretch from left, right edge stays fixed
            double endTime = dragStartTime_ + dragStartLength_;
            double rawStartTime = juce::jmax(0.0, dragStartTime_ + deltaTime);
            double finalStartTime = rawStartTime;

            if (snapTimeToGrid) {
                double snappedTime = snapTimeToGrid(rawStartTime);
                double snapDeltaPixels = std::abs((snappedTime - rawStartTime) * pixelsPerSecond);
                if (snapDeltaPixels <= SNAP_THRESHOLD_PIXELS) {
                    finalStartTime = snappedTime;
                }
            }

            finalStartTime = juce::jmin(finalStartTime, endTime - 0.1);
            double finalLength = endTime - finalStartTime;

            // Clamp by stretch factor limits
            double stretchRatio = finalLength / dragStartLength_;
            double newStretchFactor = dragStartStretchFactor_ * stretchRatio;
            newStretchFactor = juce::jlimit(0.25, 4.0, newStretchFactor);
            finalLength = dragStartLength_ * (newStretchFactor / dragStartStretchFactor_);
            finalStartTime = endTime - finalLength;

            previewStartTime_ = finalStartTime;
            previewLength_ = finalLength;

            int newX = parentPanel_->timeToPixel(finalStartTime);
            int newWidth = static_cast<int>(finalLength * pixelsPerSecond);
            setBounds(newX, getY(), juce::jmax(10, newWidth), getHeight());

            // Throttled live update to audio engine
            if (stretchThrottle_.check()) {
                auto& cm = ClipManager::getInstance();
                if (auto* mutableClip = cm.getClip(clipId_)) {
                    mutableClip->startTime = finalStartTime;
                    mutableClip->length = finalLength;
                    if (!mutableClip->audioSources.empty()) {
                        mutableClip->audioSources[0].position = 0.0;
                        mutableClip->audioSources[0].length = finalLength;
                        mutableClip->audioSources[0].stretchFactor = newStretchFactor;
                    }
                    cm.forceNotifyClipPropertyChanged(clipId_);
                }
            }
            break;
        }

        default:
            break;
    }

    // Emit real-time preview event via ClipManager (for global listeners like PianoRoll)
    ClipManager::getInstance().notifyClipDragPreview(clipId_, previewStartTime_, previewLength_);

    // Also call local callback if set
    if (onClipDragPreview) {
        onClipDragPreview(clipId_, previewStartTime_, previewLength_);
    }
}

void ClipComponent::mouseUp(const juce::MouseEvent& e) {
    // Handle right-click for context menu
    if (e.mods.isPopupMenu()) {
        showContextMenu();
        return;
    }

    // Check if we were doing a multi-clip drag
    auto& selectionManager = SelectionManager::getInstance();
    if (isDragging_ && parentPanel_ && selectionManager.getSelectedClipCount() > 1 &&
        selectionManager.isClipSelected(clipId_) && dragMode_ == DragMode::Move) {
        // Finish multi-clip drag via parent
        parentPanel_->finishMultiClipDrag();
        dragMode_ = DragMode::None;
        isDragging_ = false;
        return;
    }

    if (isDragging_ && dragMode_ != DragMode::None) {
        // Clear drag state BEFORE committing so that clipPropertyChanged notifications
        // aren't skipped — this allows the parent to relayout the component to match
        // the committed clip data, preventing a flash of stretched waveform.
        auto savedDragMode = dragMode_;
        dragMode_ = DragMode::None;
        isDragging_ = false;
        isCommitting_ = true;

        // Now apply snapping and commit to ClipManager
        switch (savedDragMode) {
            case DragMode::Move: {
                double finalStartTime = previewStartTime_;
                if (snapTimeToGrid) {
                    finalStartTime = snapTimeToGrid(finalStartTime);
                }
                finalStartTime = juce::jmax(0.0, finalStartTime);

                // Determine target track
                TrackId targetTrackId = dragStartTrackId_;
                if (parentPanel_) {
                    auto screenPos = e.getScreenPosition();
                    auto parentPos = parentPanel_->getScreenBounds().getPosition();
                    int localY = screenPos.y - parentPos.y;
                    int trackIndex = parentPanel_->getTrackIndexAtY(localY);

                    if (trackIndex >= 0) {
                        auto visibleTracks = TrackManager::getInstance().getVisibleTracks(
                            ViewModeController::getInstance().getViewMode());

                        if (trackIndex < static_cast<int>(visibleTracks.size())) {
                            targetTrackId = visibleTracks[trackIndex];
                        }
                    }
                }

                if (isDuplicating_) {
                    // Clear the ghost before creating the duplicate
                    if (parentPanel_) {
                        parentPanel_->clearClipGhost(clipId_);
                    }

                    // Alt+drag duplicate: create duplicate at final position
                    ClipId newClipId = ClipManager::getInstance().duplicateClipAt(
                        clipId_, finalStartTime, targetTrackId);
                    if (newClipId != INVALID_CLIP_ID) {
                        // Select the new duplicate
                        SelectionManager::getInstance().selectClip(newClipId);
                    }
                    // Reset duplication state
                    isDuplicating_ = false;
                    duplicateClipId_ = INVALID_CLIP_ID;
                } else {
                    // Normal move: update original clip position
                    if (onClipMoved) {
                        onClipMoved(clipId_, finalStartTime);
                    }
                    if (targetTrackId != dragStartTrackId_ && onClipMovedToTrack) {
                        onClipMovedToTrack(clipId_, targetTrackId);
                    }
                }
                break;
            }

            case DragMode::ResizeLeft: {
                double finalStartTime = previewStartTime_;
                double finalLength = previewLength_;

                if (snapTimeToGrid) {
                    finalStartTime = snapTimeToGrid(finalStartTime);
                    finalLength = dragStartLength_ - (finalStartTime - dragStartTime_);
                }

                finalStartTime = juce::jmax(0.0, finalStartTime);
                finalLength = juce::jmax(0.1, finalLength);

                if (onClipResized) {
                    // resizeClip(fromStart=true) adjusts clip->startTime and
                    // compensates audio source positions internally
                    onClipResized(clipId_, finalLength, true);
                }
                break;
            }

            case DragMode::ResizeRight: {
                double finalLength = previewLength_;

                if (snapTimeToGrid) {
                    double endTime = snapTimeToGrid(dragStartTime_ + finalLength);
                    finalLength = endTime - dragStartTime_;
                }

                finalLength = juce::jmax(0.1, finalLength);

                if (onClipResized) {
                    onClipResized(clipId_, finalLength, false);
                }
                break;
            }

            case DragMode::StretchRight: {
                stretchThrottle_.reset();

                double finalLength = previewLength_;

                if (snapTimeToGrid) {
                    double endTime = snapTimeToGrid(dragStartTime_ + finalLength);
                    finalLength = endTime - dragStartTime_;
                }

                // Compute final stretch factor from drag-start values
                double stretchRatio = finalLength / dragStartLength_;
                double newStretchFactor = dragStartStretchFactor_ * stretchRatio;
                newStretchFactor = juce::jlimit(0.25, 4.0, newStretchFactor);
                finalLength = dragStartLength_ * (newStretchFactor / dragStartStretchFactor_);

                // Apply directly (clip state may have been modified by throttled updates)
                auto& cm = ClipManager::getInstance();
                if (auto* clip = cm.getClip(clipId_)) {
                    clip->length = finalLength;
                    if (!clip->audioSources.empty()) {
                        clip->audioSources[0].length = finalLength;
                        clip->audioSources[0].stretchFactor = newStretchFactor;
                    }
                    cm.forceNotifyClipPropertyChanged(clipId_);
                }
                break;
            }

            case DragMode::StretchLeft: {
                stretchThrottle_.reset();

                double endTime = dragStartTime_ + dragStartLength_;
                double finalStartTime = previewStartTime_;
                double finalLength = previewLength_;

                if (snapTimeToGrid) {
                    finalStartTime = snapTimeToGrid(finalStartTime);
                    finalLength = endTime - finalStartTime;
                }

                // Compute final stretch factor from drag-start values
                double stretchRatio = finalLength / dragStartLength_;
                double newStretchFactor = dragStartStretchFactor_ * stretchRatio;
                newStretchFactor = juce::jlimit(0.25, 4.0, newStretchFactor);
                finalLength = dragStartLength_ * (newStretchFactor / dragStartStretchFactor_);
                finalStartTime = endTime - finalLength;

                // Apply directly (clip state may have been modified by throttled updates)
                auto& cm = ClipManager::getInstance();
                if (auto* clip = cm.getClip(clipId_)) {
                    clip->startTime = finalStartTime;
                    clip->length = finalLength;
                    if (!clip->audioSources.empty()) {
                        clip->audioSources[0].position = 0.0;
                        clip->audioSources[0].length = finalLength;
                        clip->audioSources[0].stretchFactor = newStretchFactor;
                    }
                    cm.forceNotifyClipPropertyChanged(clipId_);
                }
                break;
            }

            default:
                break;
        }
        isCommitting_ = false;
    } else {
        dragMode_ = DragMode::None;
        isDragging_ = false;
    }
}

void ClipComponent::mouseMove(const juce::MouseEvent& e) {
    bool wasHoverLeft = hoverLeftEdge_;
    bool wasHoverRight = hoverRightEdge_;

    hoverLeftEdge_ = isOnLeftEdge(e.x);
    hoverRightEdge_ = isOnRightEdge(e.x);

    // Always update cursor to check for Alt key (blade mode) and Shift key (stretch mode)
    updateCursor(e.mods.isAltDown(), e.mods.isShiftDown());

    if (hoverLeftEdge_ != wasHoverLeft || hoverRightEdge_ != wasHoverRight) {
        repaint();
    }
}

void ClipComponent::mouseExit(const juce::MouseEvent& /*e*/) {
    hoverLeftEdge_ = false;
    hoverRightEdge_ = false;
    updateCursor(false, false);
    repaint();
}

void ClipComponent::mouseDoubleClick(const juce::MouseEvent& /*e*/) {
    if (onClipDoubleClicked) {
        onClipDoubleClicked(clipId_);
    }
}

// ============================================================================
// ClipManagerListener
// ============================================================================

void ClipComponent::clipsChanged() {
    // Ignore updates while dragging to prevent flicker
    if (isDragging_) {
        return;
    }

    // Clip may have been deleted
    const auto* clip = getClipInfo();
    if (!clip) {
        // This clip was deleted - parent should remove this component
        return;
    }
    repaint();
}

void ClipComponent::clipPropertyChanged(ClipId clipId) {
    // Ignore updates while dragging to prevent flicker
    if (isDragging_) {
        return;
    }

    if (clipId == clipId_) {
        repaint();
    }
}

void ClipComponent::clipSelectionChanged(ClipId clipId) {
    // Ignore updates while dragging to prevent flicker
    if (isDragging_) {
        return;
    }

    bool wasSelected = isSelected_;
    // Check both single clip selection and multi-clip selection
    isSelected_ = (clipId == clipId_) || SelectionManager::getInstance().isClipSelected(clipId_);

    if (wasSelected != isSelected_) {
        repaint();
    }
}

// ============================================================================
// Selection
// ============================================================================

void ClipComponent::setSelected(bool selected) {
    if (isSelected_ != selected) {
        isSelected_ = selected;
        repaint();
    }
}

void ClipComponent::setMarqueeHighlighted(bool highlighted) {
    if (isMarqueeHighlighted_ != highlighted) {
        isMarqueeHighlighted_ = highlighted;
        repaint();
    }
}

bool ClipComponent::isPartOfMultiSelection() const {
    auto& selectionManager = SelectionManager::getInstance();
    return selectionManager.getSelectedClipCount() > 1 && selectionManager.isClipSelected(clipId_);
}

// ============================================================================
// Helpers
// ============================================================================

bool ClipComponent::isOnLeftEdge(int x) const {
    return x < RESIZE_HANDLE_WIDTH;
}

bool ClipComponent::isOnRightEdge(int x) const {
    return x > getWidth() - RESIZE_HANDLE_WIDTH;
}

void ClipComponent::updateCursor(bool isAltDown, bool isShiftDown) {
    // Alt key = blade/scissors mode
    if (isAltDown) {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }

    bool isClipSelected = SelectionManager::getInstance().isClipSelected(clipId_);

    if (isClipSelected && (hoverLeftEdge_ || hoverRightEdge_)) {
        if (isShiftDown) {
            // Shift+edge = stretch cursor
            setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
        } else {
            // Resize cursor only when selected
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        }
    } else if (isClipSelected) {
        // Grab cursor when selected (can drag)
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    } else {
        // Normal cursor when not selected (need to click to select first)
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

const ClipInfo* ClipComponent::getClipInfo() const {
    return ClipManager::getInstance().getClip(clipId_);
}

void ClipComponent::showContextMenu() {
    auto& clipManager = ClipManager::getInstance();
    auto& selectionManager = SelectionManager::getInstance();

    // Get selection state
    bool hasSelection = selectionManager.getSelectedClipCount() > 0;
    bool isMultiSelection = selectionManager.getSelectedClipCount() > 1;
    bool isThisClipSelected = selectionManager.isClipSelected(clipId_);

    // If right-clicking on an unselected clip, select it first
    if (!isThisClipSelected) {
        selectionManager.selectClip(clipId_);
        hasSelection = true;
        isMultiSelection = false;
    }

    juce::PopupMenu menu;

    // Copy/Cut/Paste
    menu.addItem(1, "Copy", hasSelection);
    menu.addItem(2, "Cut", hasSelection);
    menu.addItem(3, "Paste");  // Always available (will check clipboard when clicked)
    menu.addSeparator();

    // Duplicate
    menu.addItem(4, "Duplicate", hasSelection);
    menu.addSeparator();

    // Split
    if (!isMultiSelection) {
        menu.addItem(5, "Split at Playhead");
    }
    menu.addSeparator();

    // Delete
    menu.addItem(6, "Delete", hasSelection);
    menu.addSeparator();

    // Loop Settings (only for single clip)
    if (!isMultiSelection) {
        menu.addItem(7, "Loop Settings...");
    }

    // Show menu
    menu.showMenuAsync(juce::PopupMenu::Options(), [this, &clipManager,
                                                    &selectionManager](int result) {
        if (result == 0)
            return;  // Cancelled

        switch (result) {
            case 1: {  // Copy
                auto selectedClips = selectionManager.getSelectedClips();
                if (!selectedClips.empty()) {
                    clipManager.copyToClipboard(selectedClips);
                }
                break;
            }

            case 2: {  // Cut
                auto selectedClips = selectionManager.getSelectedClips();
                if (!selectedClips.empty()) {
                    clipManager.cutToClipboard(selectedClips);
                    selectionManager.clearSelection();
                }
                break;
            }

            case 3: {  // Paste
                if (clipManager.hasClipsInClipboard()) {
                    // Paste at playhead position (need to get from transport/timeline)
                    // For now, paste at clicked position
                    auto selectedClips = selectionManager.getSelectedClips();
                    double pasteTime = 0.0;
                    if (!selectedClips.empty()) {
                        // Paste after last selected clip
                        for (auto clipId : selectedClips) {
                            const auto* clip = clipManager.getClip(clipId);
                            if (clip) {
                                pasteTime = std::max(pasteTime, clip->startTime + clip->length);
                            }
                        }
                    }
                    auto newClips = clipManager.pasteFromClipboard(pasteTime);
                    if (!newClips.empty()) {
                        // Select the pasted clips
                        std::unordered_set<ClipId> newSelection(newClips.begin(), newClips.end());
                        selectionManager.selectClips(newSelection);
                    }
                }
                break;
            }

            case 4: {  // Duplicate
                auto selectedClips = selectionManager.getSelectedClips();
                for (auto clipId : selectedClips) {
                    clipManager.duplicateClip(clipId);
                }
                break;
            }

            case 5:  // Split at Playhead
                // TODO: Get playhead position from transport/timeline
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                       "Split at Playhead",
                                                       "Split at playhead not yet implemented");
                break;

            case 6: {  // Delete
                auto selectedClips = selectionManager.getSelectedClips();
                for (auto clipId : selectedClips) {
                    clipManager.deleteClip(clipId);
                }
                selectionManager.clearSelection();
                break;
            }

            case 7:  // Loop Settings
                // TODO: Show loop settings dialog
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Loop Settings",
                                                       "Loop settings dialog not yet implemented");
                break;

            default:
                break;
        }
    });
}

bool ClipComponent::keyPressed(const juce::KeyPress& key) {
    // ClipComponent doesn't handle any keys itself
    // Forward all keys to parent panel which will handle them or forward up the chain
    if (parentPanel_) {
        return parentPanel_->keyPressed(key);
    }

    return false;
}

}  // namespace magda
