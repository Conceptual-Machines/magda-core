#include "CCLaneComponent.hpp"

#include <algorithm>

#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "VelocityLaneUtils.hpp"
#include "core/ClipInfo.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda {

namespace {

CurveType midiCurveTypeToCurveType(MidiCurveType t) {
    switch (t) {
        case MidiCurveType::Step:
            return CurveType::Step;
        case MidiCurveType::Linear:
            return CurveType::Linear;
        case MidiCurveType::Bezier:
            return CurveType::Bezier;
    }
    return CurveType::Step;
}

MidiCurveType curveTypeToMidiCurveType(CurveType t) {
    switch (t) {
        case CurveType::Step:
            return MidiCurveType::Step;
        case CurveType::Linear:
            return MidiCurveType::Linear;
        case CurveType::Bezier:
            return MidiCurveType::Bezier;
    }
    return MidiCurveType::Step;
}

}  // namespace

CCLaneComponent::CCLaneComponent() {
    setName("CCLane");
    setWantsKeyboardFocus(true);

    // Default to select mode; Cmd+drag for pencil draw, Alt+drag for line tool
    setDrawMode(CurveDrawMode::Select);

    // Use compact padding for small lane area
    setPadding(3);
}

// ============================================================================
// Configuration
// ============================================================================

void CCLaneComponent::setClip(ClipId clipId) {
    if (clipId_ != clipId) {
        clipId_ = clipId;
        invalidateCache();

        // Set curve colour from clip
        const auto* clip = ClipManager::getInstance().getClip(clipId_);
        if (clip)
            setCurveColour(clip->colour);

        rebuildPointComponents();
    }
}

void CCLaneComponent::setCCNumber(int ccNumber) {
    if (ccNumber_ != ccNumber) {
        ccNumber_ = ccNumber;
        invalidateCache();
        rebuildPointComponents();
    }
}

void CCLaneComponent::setIsPitchBend(bool isPitchBend) {
    if (isPitchBend_ != isPitchBend) {
        isPitchBend_ = isPitchBend;
        invalidateCache();
        rebuildPointComponents();
    }
}

void CCLaneComponent::setPitchBendRange(int semitones) {
    pitchBendRange_ = juce::jlimit(1, 96, semitones);
    repaint();
}

void CCLaneComponent::setPixelsPerBeat(double ppb) {
    if (pixelsPerBeat_ != ppb) {
        pixelsPerBeat_ = ppb;
        updatePointPositions();
        repaint();
    }
}

void CCLaneComponent::setScrollOffset(int offsetX) {
    if (scrollOffsetX_ != offsetX) {
        scrollOffsetX_ = offsetX;
        updatePointPositions();
        repaint();
    }
}

void CCLaneComponent::setLeftPadding(int padding) {
    if (leftPadding_ != padding) {
        leftPadding_ = padding;
        updatePointPositions();
        repaint();
    }
}

void CCLaneComponent::setRelativeMode(bool relative) {
    if (relativeMode_ != relative) {
        relativeMode_ = relative;
        invalidateCache();
        rebuildPointComponents();
    }
}

void CCLaneComponent::setClipStartBeats(double startBeats) {
    if (clipStartBeats_ != startBeats) {
        clipStartBeats_ = startBeats;
        invalidateCache();
        rebuildPointComponents();
    }
}

void CCLaneComponent::setClipLengthBeats(double lengthBeats) {
    if (clipLengthBeats_ != lengthBeats) {
        clipLengthBeats_ = lengthBeats;
        repaint();
    }
}

void CCLaneComponent::setLoopRegion(double offsetBeats, double lengthBeats, bool enabled) {
    loopOffsetBeats_ = offsetBeats;
    loopLengthBeats_ = lengthBeats;
    loopEnabled_ = enabled;
    repaint();
}

void CCLaneComponent::refreshEvents() {
    invalidateCache();
    rebuildPointComponents();
}

void CCLaneComponent::invalidateCache() {
    pointsCacheDirty_ = true;
}

// ============================================================================
// Display name
// ============================================================================

juce::String CCLaneComponent::getLaneName() const {
    if (isPitchBend_)
        return "Pitch";

    switch (ccNumber_) {
        case 1:
            return "Mod";
        case 7:
            return "Vol";
        case 11:
            return "Expr";
        case 64:
            return "Sus";
        default:
            return "CC " + juce::String(ccNumber_);
    }
}

// ============================================================================
// Value label formatting
// ============================================================================

juce::String CCLaneComponent::formatValueLabel(double y) const {
    if (isPitchBend_) {
        // Bipolar: y=0.5 is center (0 semitones), y=0 is -range, y=1 is +range
        double semitones = (y - 0.5) * 2.0 * pitchBendRange_;
        if (std::abs(semitones) < 0.05)
            return "0";
        juce::String sign = semitones > 0 ? "+" : "";
        return sign + juce::String(semitones, 1) + " st";
    }
    int value = normalizedToValue(y);
    return juce::String(value);
}

// ============================================================================
// Coordinate conversion
// ============================================================================

double CCLaneComponent::pixelToX(int px) const {
    return velocity_lane::pixelToBeat(px, pixelsPerBeat_, leftPadding_, scrollOffsetX_);
}

int CCLaneComponent::xToPixel(double x) const {
    return velocity_lane::beatToPixel(x, pixelsPerBeat_, leftPadding_, scrollOffsetX_);
}

// ============================================================================
// Value conversion
// ============================================================================

int CCLaneComponent::normalizedToValue(double y) const {
    return juce::jlimit(0, getMaxValue(), static_cast<int>(std::round(y * getMaxValue())));
}

double CCLaneComponent::valueToNormalized(int value) const {
    int maxVal = getMaxValue();
    if (maxVal <= 0)
        return 0.0;
    return juce::jlimit(0.0, 1.0, static_cast<double>(value) / maxVal);
}

// ============================================================================
// Points cache
// ============================================================================

const std::vector<CurvePoint>& CCLaneComponent::getPoints() const {
    if (pointsCacheDirty_)
        updatePointsCache();
    return cachedPoints_;
}

void CCLaneComponent::updatePointsCache() const {
    cachedPoints_.clear();

    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || clip->type != ClipType::MIDI) {
        pointsCacheDirty_ = false;
        return;
    }

    int maxVal = getMaxValue();

    if (isPitchBend_) {
        cachedPoints_.reserve(clip->midiPitchBendData.size());
        for (size_t i = 0; i < clip->midiPitchBendData.size(); ++i) {
            const auto& pb = clip->midiPitchBendData[i];
            CurvePoint cp;
            cp.id = static_cast<uint32_t>(i);
            cp.x = relativeMode_ ? pb.beatPosition : (clipStartBeats_ + pb.beatPosition);
            cp.y = static_cast<double>(pb.value) / maxVal;
            cp.curveType = midiCurveTypeToCurveType(pb.curveType);
            cp.tension = pb.tension;
            cp.inHandle.x = pb.inHandle.dx;
            cp.inHandle.y = pb.inHandle.dy;
            cp.inHandle.linked = pb.inHandle.linked;
            cp.outHandle.x = pb.outHandle.dx;
            cp.outHandle.y = pb.outHandle.dy;
            cp.outHandle.linked = pb.outHandle.linked;
            cachedPoints_.push_back(cp);
        }
    } else {
        // Filter by CC number and build sequential IDs
        // Store the original index in the id field for mutation lookup
        for (size_t i = 0; i < clip->midiCCData.size(); ++i) {
            const auto& cc = clip->midiCCData[i];
            if (cc.controller != ccNumber_)
                continue;

            CurvePoint cp;
            cp.id = static_cast<uint32_t>(i);  // Original index in midiCCData
            cp.x = relativeMode_ ? cc.beatPosition : (clipStartBeats_ + cc.beatPosition);
            cp.y = static_cast<double>(cc.value) / maxVal;
            cp.curveType = midiCurveTypeToCurveType(cc.curveType);
            cp.tension = cc.tension;
            cp.inHandle.x = cc.inHandle.dx;
            cp.inHandle.y = cc.inHandle.dy;
            cp.inHandle.linked = cc.inHandle.linked;
            cp.outHandle.x = cc.outHandle.dx;
            cp.outHandle.y = cc.outHandle.dy;
            cp.outHandle.linked = cc.outHandle.linked;
            cachedPoints_.push_back(cp);
        }
    }

    // Sort by x position
    std::sort(cachedPoints_.begin(), cachedPoints_.end());

    pointsCacheDirty_ = false;
}

size_t CCLaneComponent::pointIdToEventIndex(uint32_t pointId) const {
    // The point ID is the original index into the clip's CC/PB data vector
    return static_cast<size_t>(pointId);
}

// ============================================================================
// Grid painting
// ============================================================================

void CCLaneComponent::paintGrid(juce::Graphics& g) {
    auto bounds = getLocalBounds();

    // Background
    g.setColour(DarkTheme::getColour(DarkTheme::BACKGROUND_ALT));
    g.fillRect(bounds);

    g.setFont(FontManager::getInstance().getUIFont(9.0f));
    auto labelColour = DarkTheme::getColour(DarkTheme::TEXT_SECONDARY).withAlpha(0.6f);
    constexpr int labelMargin = 2;
    constexpr int labelWidth = 36;
    constexpr int labelH = 12;

    // Clamp label Y so it stays within visible bounds
    auto clampLabelY = [&](int y) {
        int maxY = juce::jmax(1, bounds.getHeight() - labelH - 1);
        return juce::jlimit(1, maxY, y - labelH / 2);
    };

    if (isPitchBend_) {
        // --- Bipolar pitch bend grid ---

        // Center line (0 semitones = y 0.5)
        int centerY = yToPixel(0.5);
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.8f));
        g.drawHorizontalLine(centerY, 0.0f, static_cast<float>(bounds.getWidth()));

        // Draw semitone grid lines symmetrically around center
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.4f));
        for (int st = 1; st <= pitchBendRange_; ++st) {
            double frac = static_cast<double>(st) / pitchBendRange_;
            int yUp = yToPixel(0.5 + frac * 0.5);
            g.drawHorizontalLine(yUp, 0.0f, static_cast<float>(bounds.getWidth()));
            int yDown = yToPixel(0.5 - frac * 0.5);
            g.drawHorizontalLine(yDown, 0.0f, static_cast<float>(bounds.getWidth()));
        }

        // Labels
        g.setColour(labelColour);

        g.drawText("0", labelMargin, clampLabelY(centerY), labelWidth, labelH,
                   juce::Justification::centredLeft, false);
        g.drawText("+" + juce::String(pitchBendRange_), labelMargin, clampLabelY(yToPixel(1.0)),
                   labelWidth, labelH, juce::Justification::centredLeft, false);
        g.drawText("-" + juce::String(pitchBendRange_), labelMargin, clampLabelY(yToPixel(0.0)),
                   labelWidth, labelH, juce::Justification::centredLeft, false);

        // Intermediate labels if enough room
        if (bounds.getHeight() > 80 && pitchBendRange_ >= 2) {
            int halfRange = pitchBendRange_ / 2;
            if (halfRange > 0) {
                double fracHalf = static_cast<double>(halfRange) / pitchBendRange_;
                g.drawText("+" + juce::String(halfRange), labelMargin,
                           clampLabelY(yToPixel(0.5 + fracHalf * 0.5)), labelWidth, labelH,
                           juce::Justification::centredLeft, false);
                g.drawText("-" + juce::String(halfRange), labelMargin,
                           clampLabelY(yToPixel(0.5 - fracHalf * 0.5)), labelWidth, labelH,
                           juce::Justification::centredLeft, false);
            }
        }
    } else {
        // --- Unipolar CC grid ---

        // Horizontal grid lines at 25%, 50%, 75%, 100%
        g.setColour(DarkTheme::getColour(DarkTheme::BORDER).withAlpha(0.5f));
        for (int pct : {25, 50, 75, 100}) {
            int y = yToPixel(pct / 100.0);
            g.drawHorizontalLine(y, 0.0f, static_cast<float>(bounds.getWidth()));
        }

        // Value labels
        int maxVal = getMaxValue();
        g.setColour(labelColour);

        auto drawLabel = [&](int pct) {
            int value = (maxVal * pct) / 100;
            int y = yToPixel(pct / 100.0);
            g.drawText(juce::String(value), labelMargin, clampLabelY(y), labelWidth, labelH,
                       juce::Justification::centredLeft, false);
        };

        drawLabel(0);
        drawLabel(100);
        if (bounds.getHeight() > 60)
            drawLabel(50);
        if (bounds.getHeight() > 120) {
            drawLabel(25);
            drawLabel(75);
        }
    }

    // Top border
    g.setColour(DarkTheme::getColour(DarkTheme::BORDER));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(bounds.getWidth()));
}

// ============================================================================
// Data mutation callbacks
// ============================================================================

void CCLaneComponent::onPointAdded(double x, double y, CurveType curveType) {
    if (clipId_ == INVALID_CLIP_ID)
        return;

    // Convert from normalized coordinates back to raw values
    double beatPos = relativeMode_ ? x : (x - clipStartBeats_);
    int value = normalizedToValue(y);

    if (isPitchBend_) {
        MidiPitchBendData event;
        event.value = value;
        event.beatPosition = beatPos;
        event.curveType = curveTypeToMidiCurveType(curveType);
        UndoManager::getInstance().executeCommand(
            std::make_unique<AddMidiPitchBendEventCommand>(clipId_, event));
    } else {
        MidiCCData event;
        event.controller = ccNumber_;
        event.value = value;
        event.beatPosition = beatPos;
        event.curveType = curveTypeToMidiCurveType(curveType);
        UndoManager::getInstance().executeCommand(
            std::make_unique<AddMidiCCEventCommand>(clipId_, event));
    }
}

void CCLaneComponent::onPointMoved(uint32_t pointId, double newX, double newY) {
    if (clipId_ == INVALID_CLIP_ID)
        return;

    size_t eventIndex = pointIdToEventIndex(pointId);
    double newBeatPos = relativeMode_ ? newX : (newX - clipStartBeats_);
    int newValue = normalizedToValue(newY);

    if (isPitchBend_) {
        UndoManager::getInstance().executeCommand(std::make_unique<MoveMidiPitchBendEventCommand>(
            clipId_, eventIndex, newBeatPos, newValue));
    } else {
        UndoManager::getInstance().executeCommand(
            std::make_unique<MoveMidiCCEventCommand>(clipId_, eventIndex, newBeatPos, newValue));
    }
}

void CCLaneComponent::onPointDeleted(uint32_t pointId) {
    if (clipId_ == INVALID_CLIP_ID)
        return;

    size_t eventIndex = pointIdToEventIndex(pointId);

    if (isPitchBend_) {
        UndoManager::getInstance().executeCommand(
            std::make_unique<DeleteMidiPitchBendEventCommand>(clipId_, eventIndex));
    } else {
        UndoManager::getInstance().executeCommand(
            std::make_unique<DeleteMidiCCEventCommand>(clipId_, eventIndex));
    }
}

void CCLaneComponent::onPointSelected(uint32_t /*pointId*/) {
    // Selection is managed by CurveEditorBase::selectedPointIds_
}

void CCLaneComponent::onDeleteSelectedPoints(const std::set<uint32_t>& pointIds) {
    if (clipId_ == INVALID_CLIP_ID || pointIds.empty())
        return;

    // Convert point IDs to event indices
    std::vector<size_t> eventIndices;
    eventIndices.reserve(pointIds.size());
    for (uint32_t pid : pointIds) {
        eventIndices.push_back(pointIdToEventIndex(pid));
    }

    if (isPitchBend_) {
        UndoManager::getInstance().executeCommand(
            std::make_unique<DeleteMultipleMidiPitchBendEventsCommand>(clipId_,
                                                                       std::move(eventIndices)));
    } else {
        UndoManager::getInstance().executeCommand(
            std::make_unique<DeleteMultipleMidiCCEventsCommand>(clipId_, std::move(eventIndices)));
    }
}

void CCLaneComponent::onTensionChanged(uint32_t pointId, double tension) {
    if (clipId_ == INVALID_CLIP_ID)
        return;

    size_t eventIndex = pointIdToEventIndex(pointId);

    if (isPitchBend_) {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetMidiPitchBendEventTensionCommand>(clipId_, eventIndex, tension));
    } else {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetMidiCCEventTensionCommand>(clipId_, eventIndex, tension));
    }
}

void CCLaneComponent::onHandlesChanged(uint32_t pointId, const CurveHandleData& inHandle,
                                       const CurveHandleData& outHandle) {
    if (clipId_ == INVALID_CLIP_ID)
        return;

    size_t eventIndex = pointIdToEventIndex(pointId);

    MidiCurveHandle inH;
    inH.dx = inHandle.x;
    inH.dy = inHandle.y;
    inH.linked = inHandle.linked;

    MidiCurveHandle outH;
    outH.dx = outHandle.x;
    outH.dy = outHandle.y;
    outH.linked = outHandle.linked;

    if (isPitchBend_) {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetMidiPitchBendEventHandlesCommand>(clipId_, eventIndex, inH, outH));
    } else {
        UndoManager::getInstance().executeCommand(
            std::make_unique<SetMidiCCEventHandlesCommand>(clipId_, eventIndex, inH, outH));
    }
}

}  // namespace magda
