#include "VelocityLaneComponent.hpp"

#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "VelocityLaneUtils.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"

namespace magda {

VelocityLaneComponent::VelocityLaneComponent() {
    setName("VelocityLane");
    setOpaque(true);  // Ensure proper repainting during drag
}

void VelocityLaneComponent::setClip(ClipId clipId) {
    if (clipId_ != clipId) {
        clipId_ = clipId;
        repaint();
    }
}

void VelocityLaneComponent::setClipIds(const std::vector<ClipId>& clipIds) {
    clipIds_ = clipIds;
    repaint();
}

void VelocityLaneComponent::setPixelsPerBeat(double ppb) {
    if (pixelsPerBeat_ != ppb) {
        pixelsPerBeat_ = ppb;
        repaint();
    }
}

void VelocityLaneComponent::setScrollOffset(int offsetX) {
    if (scrollOffsetX_ != offsetX) {
        scrollOffsetX_ = offsetX;
        repaint();
    }
}

void VelocityLaneComponent::setLeftPadding(int padding) {
    if (leftPadding_ != padding) {
        leftPadding_ = padding;
        repaint();
    }
}

void VelocityLaneComponent::setRelativeMode(bool relative) {
    if (relativeMode_ != relative) {
        relativeMode_ = relative;
        repaint();
    }
}

void VelocityLaneComponent::setClipStartBeats(double startBeats) {
    if (clipStartBeats_ != startBeats) {
        clipStartBeats_ = startBeats;
        repaint();
    }
}

void VelocityLaneComponent::setClipLengthBeats(double lengthBeats) {
    if (clipLengthBeats_ != lengthBeats) {
        clipLengthBeats_ = lengthBeats;
        repaint();
    }
}

void VelocityLaneComponent::setLoopRegion(double offsetBeats, double lengthBeats, bool enabled) {
    loopOffsetBeats_ = offsetBeats;
    loopLengthBeats_ = lengthBeats;
    loopEnabled_ = enabled;
    repaint();
}

void VelocityLaneComponent::refreshNotes() {
    repaint();
}

void VelocityLaneComponent::setNotePreviewPosition(size_t noteIndex, double previewBeat,
                                                   bool isDragging) {
    if (isDragging) {
        notePreviewPositions_[noteIndex] = previewBeat;
    } else {
        notePreviewPositions_.erase(noteIndex);
    }
    repaint();
}

void VelocityLaneComponent::setSelectedNoteIndices(const std::vector<size_t>& indices) {
    DBG("VelocityLane::setSelectedNoteIndices - count=" << indices.size());
    selectedNoteIndices_ = indices;
    // Reset curve state when selection changes
    isCurveHandleVisible_ = false;
    isCurveHandleDragging_ = false;
    curveAmount_ = 0.0f;
    previewVelocities_.clear();
    repaint();
}

int VelocityLaneComponent::beatToPixel(double beat) const {
    return velocity_lane::beatToPixel(beat, pixelsPerBeat_, leftPadding_, scrollOffsetX_);
}

double VelocityLaneComponent::pixelToBeat(int x) const {
    return velocity_lane::pixelToBeat(x, pixelsPerBeat_, leftPadding_, scrollOffsetX_);
}

int VelocityLaneComponent::velocityToY(int velocity) const {
    return velocity_lane::velocityToY(velocity, getHeight());
}

int VelocityLaneComponent::yToVelocity(int y) const {
    return velocity_lane::yToVelocity(y, getHeight());
}

size_t VelocityLaneComponent::findNoteAtX(int x) const {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || clip->type != ClipType::MIDI) {
        return SIZE_MAX;
    }

    double clickBeat = pixelToBeat(x);

    // Search for a note that contains this beat
    for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
        const auto& note = clip->midiNotes[i];

        // In absolute mode, offset by clip start
        double noteStart = relativeMode_ ? note.startBeat : (clipStartBeats_ + note.startBeat);
        double noteEnd = noteStart + note.lengthBeats;

        if (clickBeat >= noteStart && clickBeat < noteEnd) {
            return i;
        }
    }

    return SIZE_MAX;
}

juce::Colour VelocityLaneComponent::getClipColour() const {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip ? clip->colour : DarkTheme::getAccentColour();
}

int VelocityLaneComponent::interpolateVelocity(float t) const {
    return velocity_lane::interpolateVelocity(t, rampStartVelocity_, rampEndVelocity_,
                                              curveAmount_);
}

std::vector<std::pair<size_t, int>> VelocityLaneComponent::computeRampVelocities() const {
    std::vector<std::pair<size_t, int>> result;
    if (sortedSelectedIndices_.size() < 2) {
        return result;
    }

    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || clip->type != ClipType::MIDI) {
        return result;
    }

    // Get beat positions for normalization
    double firstBeat = clip->midiNotes[sortedSelectedIndices_.front()].startBeat;
    double lastBeat = clip->midiNotes[sortedSelectedIndices_.back()].startBeat;
    double range = lastBeat - firstBeat;

    for (size_t idx : sortedSelectedIndices_) {
        if (idx >= clip->midiNotes.size())
            continue;

        float t = (range > 0.0)
                      ? static_cast<float>((clip->midiNotes[idx].startBeat - firstBeat) / range)
                      : 0.0f;
        int vel = interpolateVelocity(t);
        result.emplace_back(idx, vel);
    }

    return result;
}

bool VelocityLaneComponent::hitTestCurveHandle(int x, int y) const {
    if (!isCurveHandleVisible_)
        return false;
    int half = CURVE_HANDLE_SIZE;
    return std::abs(x - curveHandleX_) <= half && std::abs(y - curveHandleY_) <= half;
}

void VelocityLaneComponent::updateCurveHandle() {
    if (sortedSelectedIndices_.size() < 2) {
        isCurveHandleVisible_ = false;
        return;
    }

    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || clip->type != ClipType::MIDI) {
        isCurveHandleVisible_ = false;
        return;
    }

    // Position at horizontal midpoint of selected notes
    double firstBeat = clip->midiNotes[sortedSelectedIndices_.front()].startBeat;
    double lastBeat = clip->midiNotes[sortedSelectedIndices_.back()].startBeat;
    double midBeat = (firstBeat + lastBeat) * 0.5;

    if (relativeMode_) {
        curveHandleX_ = beatToPixel(midBeat);
    } else {
        double tempo = 120.0;
        if (auto* controller = TimelineController::getCurrent()) {
            tempo = controller->getState().tempo.bpm;
        }
        double clipAbsStartBeats = 0.0;
        const auto* clipData = ClipManager::getInstance().getClip(clipId_);
        if (clipData) {
            clipAbsStartBeats = clipData->startTime * (tempo / 60.0);
        }
        curveHandleX_ = beatToPixel(midBeat + clipAbsStartBeats);
    }

    // Y at the interpolated velocity at t=0.5
    int midVel = interpolateVelocity(0.5f);
    curveHandleY_ = velocityToY(midVel);
}

void VelocityLaneComponent::updatePreviewVelocities() {
    previewVelocities_.clear();
    auto velocities = computeRampVelocities();
    for (const auto& [idx, vel] : velocities) {
        previewVelocities_[idx] = vel;
    }
}

void VelocityLaneComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(bounds);

    // Draw horizontal grid lines at 25%, 50%, 75%, 100%
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
    int margin = 2;
    int usableHeight = getHeight() - (margin * 2);

    for (int pct : {25, 50, 75, 100}) {
        int y = margin + usableHeight - (pct * usableHeight / 100);
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(bounds.getWidth()));
    }

    // Build list of clips to draw
    std::vector<ClipId> clipsToRender;
    if (clipIds_.size() > 1) {
        clipsToRender = clipIds_;
    } else if (clipId_ != INVALID_CLIP_ID) {
        clipsToRender.push_back(clipId_);
    }

    if (clipsToRender.empty()) {
        return;
    }

    auto& clipManager = ClipManager::getInstance();

    // Get tempo for multi-clip relative offset
    double tempo = 120.0;
    if (auto* controller = TimelineController::getCurrent()) {
        tempo = controller->getState().tempo.bpm;
    }
    double beatsPerSecond = tempo / 60.0;

    int minBarWidth = 4;

    for (ClipId renderClipId : clipsToRender) {
        const auto* clip = clipManager.getClip(renderClipId);
        if (!clip || clip->type != ClipType::MIDI) {
            continue;
        }

        // Compute per-clip offset for multi-clip relative mode
        double clipOffsetBeats = 0.0;
        if (relativeMode_ && clipIds_.size() > 1) {
            clipOffsetBeats = clip->startTime * beatsPerSecond - clipStartBeats_;
        }

        juce::Colour noteColour = clip->colour;
        bool isPrimaryClip = (renderClipId == clipId_);

        for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
            const auto& note = clip->midiNotes[i];

            // Calculate x position - use preview position if available (primary clip only)
            double noteStart = note.startBeat;
            if (isPrimaryClip) {
                auto previewIt = notePreviewPositions_.find(i);
                if (previewIt != notePreviewPositions_.end()) {
                    noteStart = previewIt->second;
                }
            }

            if (relativeMode_) {
                noteStart += clipOffsetBeats;
            } else {
                double clipAbsStartBeats = clip->startTime * beatsPerSecond;
                noteStart += clipAbsStartBeats;
            }

            int x = beatToPixel(noteStart);
            int barWidth =
                juce::jmax(minBarWidth, static_cast<int>(note.lengthBeats * pixelsPerBeat_));

            // Skip if out of view
            if (x + barWidth < 0 || x > bounds.getWidth()) {
                continue;
            }

            // Use drag velocity if this is the note being dragged (primary clip only)
            int velocity = note.velocity;
            if (isDragging_ && isPrimaryClip && i == draggingNoteIndex_) {
                velocity = currentDragVelocity_;
            }
            // Use preview velocity during ramp/curve editing
            if (isPrimaryClip && !previewVelocities_.empty()) {
                auto pvIt = previewVelocities_.find(i);
                if (pvIt != previewVelocities_.end()) {
                    velocity = pvIt->second;
                }
            }

            // Calculate stem position from velocity
            int barHeight = velocity * usableHeight / 127;
            int barY = margin + usableHeight - barHeight;
            int bottomY = margin + usableHeight;
            int centerX = x;
            float circleRadius = 3.0f;

            // Draw stem line
            g.setColour(noteColour.withAlpha(0.7f));
            g.drawVerticalLine(centerX, static_cast<float>(barY) + circleRadius,
                               static_cast<float>(bottomY));

            // Draw circle on top
            bool isBeingDragged = isDragging_ && isPrimaryClip && i == draggingNoteIndex_;
            g.setColour(isBeingDragged ? noteColour.brighter(0.5f) : noteColour);
            g.fillEllipse(static_cast<float>(centerX) - circleRadius,
                          static_cast<float>(barY) - circleRadius, circleRadius * 2.0f,
                          circleRadius * 2.0f);
        }
    }

    // Draw ghost loop-repeating velocity bars
    if (loopEnabled_ && loopLengthBeats_ > 0.0 && clipIds_.size() <= 1 && clipLengthBeats_ > 0.0) {
        const auto* loopClip = clipManager.getClip(clipId_);
        if (loopClip && loopClip->type == ClipType::MIDI) {
            int numRepetitions = static_cast<int>(std::ceil(clipLengthBeats_ / loopLengthBeats_));

            for (int rep = 1; rep < numRepetitions; ++rep) {
                for (const auto& note : loopClip->midiNotes) {
                    // Only notes within the loop region
                    if (note.startBeat < loopOffsetBeats_ ||
                        note.startBeat >= loopOffsetBeats_ + loopLengthBeats_) {
                        continue;
                    }

                    double relStart = (note.startBeat - loopOffsetBeats_) + rep * loopLengthBeats_;
                    if (relStart >= clipLengthBeats_) {
                        continue;
                    }

                    double displayStart = relativeMode_ ? relStart : (clipStartBeats_ + relStart);

                    int x = beatToPixel(displayStart);
                    int barWidth = juce::jmax(minBarWidth,
                                              static_cast<int>(note.lengthBeats * pixelsPerBeat_));

                    if (x + barWidth < 0 || x > bounds.getWidth()) {
                        continue;
                    }

                    int barHeight = note.velocity * usableHeight / 127;
                    int barY = margin + usableHeight - barHeight;
                    int bottomY = margin + usableHeight;
                    int centerX = x;
                    float circleRadius = 3.0f;

                    // Ghost stem
                    g.setColour(loopClip->colour.withAlpha(0.25f));
                    g.drawVerticalLine(centerX, static_cast<float>(barY) + circleRadius,
                                       static_cast<float>(bottomY));

                    // Ghost circle
                    g.setColour(loopClip->colour.withAlpha(0.35f));
                    g.fillEllipse(static_cast<float>(centerX) - circleRadius,
                                  static_cast<float>(barY) - circleRadius, circleRadius * 2.0f,
                                  circleRadius * 2.0f);
                }
            }
        }
    }

    // Draw ramp/curve line and handle when active
    if ((isRampDragging_ || isCurveHandleVisible_) && sortedSelectedIndices_.size() >= 2 &&
        clipId_ != INVALID_CLIP_ID) {
        const auto* curveClip = clipManager.getClip(clipId_);
        if (curveClip && curveClip->type == ClipType::MIDI) {
            // Compute absolute offset for beat->pixel
            double clipAbsOffset = 0.0;
            if (!relativeMode_) {
                double tempo = 120.0;
                if (auto* controller = TimelineController::getCurrent()) {
                    tempo = controller->getState().tempo.bpm;
                }
                clipAbsOffset = curveClip->startTime * (tempo / 60.0);
            }

            // Draw curve line through selected notes
            juce::Path curvePath;
            bool started = false;

            double firstBeat = curveClip->midiNotes[sortedSelectedIndices_.front()].startBeat;
            double lastBeat = curveClip->midiNotes[sortedSelectedIndices_.back()].startBeat;
            double beatRange = lastBeat - firstBeat;

            if (beatRange > 0.0) {
                // Draw smooth curve with multiple segments
                constexpr int numSegments = 40;
                for (int seg = 0; seg <= numSegments; ++seg) {
                    float t = static_cast<float>(seg) / static_cast<float>(numSegments);
                    double beat = firstBeat + t * beatRange;
                    int vel = interpolateVelocity(t);
                    float px = static_cast<float>(beatToPixel(beat + clipAbsOffset));
                    float py = static_cast<float>(velocityToY(vel));

                    if (!started) {
                        curvePath.startNewSubPath(px, py);
                        started = true;
                    } else {
                        curvePath.lineTo(px, py);
                    }
                }

                g.setColour(juce::Colours::white.withAlpha(0.6f));
                g.strokePath(curvePath, juce::PathStrokeType(1.5f));
            }

            // Draw curve handle
            if (isCurveHandleVisible_ && !isRampDragging_) {
                float hx = static_cast<float>(curveHandleX_);
                float hy = static_cast<float>(curveHandleY_);
                float hs = static_cast<float>(CURVE_HANDLE_SIZE);

                // Diamond shape
                juce::Path diamond;
                diamond.startNewSubPath(hx, hy - hs);
                diamond.lineTo(hx + hs, hy);
                diamond.lineTo(hx, hy + hs);
                diamond.lineTo(hx - hs, hy);
                diamond.closeSubPath();

                g.setColour(isCurveHandleDragging_ ? juce::Colours::white
                                                   : juce::Colours::white.withAlpha(0.8f));
                g.fillPath(diamond);
                g.setColour(juce::Colours::black.withAlpha(0.5f));
                g.strokePath(diamond, juce::PathStrokeType(1.0f));
            }
        }
    }

    // Draw top border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(bounds.getWidth()));
}

void VelocityLaneComponent::mouseDown(const juce::MouseEvent& e) {
    // Check for curve handle click first
    if (isCurveHandleVisible_ && hitTestCurveHandle(e.x, e.y)) {
        isCurveHandleDragging_ = true;
        curveHandleDragStartY_ = e.y;
        curveHandleDragStartAmount_ = curveAmount_;
        return;
    }

    // Alt+click with 2+ selected notes: start ramp drag
    DBG("VelocityLane::mouseDown - alt="
        << (e.mods.isAltDown() ? "YES" : "NO") << " cmd=" << (e.mods.isCommandDown() ? "YES" : "NO")
        << " shift=" << (e.mods.isShiftDown() ? "YES" : "NO")
        << " ctrl=" << (e.mods.isCtrlDown() ? "YES" : "NO") << " rawFlags=" << e.mods.getRawFlags()
        << " selectedNotes=" << selectedNoteIndices_.size() << " clipId=" << clipId_);
    if (e.mods.isAltDown() && selectedNoteIndices_.size() >= 2) {
        const auto* clip = ClipManager::getInstance().getClip(clipId_);
        if (clip && clip->type == ClipType::MIDI) {
            // Sort selected indices by beat position
            sortedSelectedIndices_ = selectedNoteIndices_;
            std::sort(sortedSelectedIndices_.begin(), sortedSelectedIndices_.end(),
                      [&clip](size_t a, size_t b) {
                          return clip->midiNotes[a].startBeat < clip->midiNotes[b].startBeat;
                      });

            isRampDragging_ = true;
            isCurveHandleVisible_ = false;
            curveAmount_ = 0.0f;
            rampStartVelocity_ = yToVelocity(e.y);
            rampEndVelocity_ = rampStartVelocity_;
            updatePreviewVelocities();
            repaint();
            return;
        }
    }

    // Clear ramp/curve visual state
    isCurveHandleVisible_ = false;
    isCurveHandleDragging_ = false;
    curveAmount_ = 0.0f;
    previewVelocities_.clear();

    // Normal single-note drag
    size_t noteIndex = findNoteAtX(e.x);

    if (noteIndex != SIZE_MAX) {
        const auto* clip = ClipManager::getInstance().getClip(clipId_);
        if (clip && noteIndex < clip->midiNotes.size()) {
            draggingNoteIndex_ = noteIndex;
            dragStartVelocity_ = clip->midiNotes[noteIndex].velocity;
            currentDragVelocity_ = yToVelocity(e.y);
            isDragging_ = true;
            repaint();
        }
    }
}

void VelocityLaneComponent::mouseDrag(const juce::MouseEvent& e) {
    if (isRampDragging_) {
        int newEnd = yToVelocity(e.y);
        if (newEnd != rampEndVelocity_) {
            rampEndVelocity_ = newEnd;
            updatePreviewVelocities();
            repaint();
        }
        return;
    }

    if (isCurveHandleDragging_) {
        // Map Y delta to curve amount (-1..1)
        int deltaY = curveHandleDragStartY_ - e.y;  // up = positive
        float newAmount = curveHandleDragStartAmount_ + deltaY / 100.0f;
        newAmount = juce::jlimit(-1.0f, 1.0f, newAmount);
        if (newAmount != curveAmount_) {
            curveAmount_ = newAmount;
            updatePreviewVelocities();
            updateCurveHandle();
            repaint();
        }
        return;
    }

    if (isDragging_ && draggingNoteIndex_ != SIZE_MAX) {
        int newVelocity = yToVelocity(e.y);
        if (newVelocity != currentDragVelocity_) {
            currentDragVelocity_ = newVelocity;
            repaint();
        }
    }
}

void VelocityLaneComponent::mouseUp(const juce::MouseEvent& e) {
    if (isRampDragging_) {
        rampEndVelocity_ = yToVelocity(e.y);
        auto velocities = computeRampVelocities();

        if (!velocities.empty() && onMultiVelocityChanged) {
            onMultiVelocityChanged(clipId_, velocities);
        }

        isRampDragging_ = false;
        previewVelocities_.clear();

        // Show curve handle for post-ramp bezier adjustment
        isCurveHandleVisible_ = true;
        curveAmount_ = 0.0f;
        updateCurveHandle();
        repaint();
        return;
    }

    if (isCurveHandleDragging_) {
        isCurveHandleDragging_ = false;

        auto velocities = computeRampVelocities();
        if (!velocities.empty() && onMultiVelocityChanged) {
            onMultiVelocityChanged(clipId_, velocities);
        }

        previewVelocities_.clear();
        updateCurveHandle();
        repaint();
        return;
    }

    if (isDragging_ && draggingNoteIndex_ != SIZE_MAX) {
        int finalVelocity = yToVelocity(e.y);

        // Only commit if velocity actually changed
        if (finalVelocity != dragStartVelocity_ && onVelocityChanged) {
            onVelocityChanged(clipId_, draggingNoteIndex_, finalVelocity);
        }

        draggingNoteIndex_ = SIZE_MAX;
        isDragging_ = false;
        repaint();
    }
}

}  // namespace magda
