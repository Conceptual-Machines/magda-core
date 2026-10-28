// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "NoteExpressionLaneComponent.hpp"

#include <algorithm>

#include "../../themes/ActiveTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "VelocityLaneUtils.hpp"
#include "core/ClipManager.hpp"
#include "core/MidiNoteCommands.hpp"
#include "core/MpeWire.hpp"
#include "core/PitchExpressionCurve.hpp"
#include "core/UndoManager.hpp"

namespace magda {

namespace {

double restOf(NoteExpression dimension) {
    return mpe::valueForWire(dimension == NoteExpression::Timbre ? mpe::kTimbreRest
                                                                 : mpe::kPressureRest);
}

}  // namespace

NoteExpressionLaneComponent::NoteExpressionLaneComponent(NoteExpression dimension)
    : dimension_(dimension) {
    setName(getLaneName() + "Lane");
    setWantsKeyboardFocus(true);
    setDrawMode(CurveDrawMode::Select);
    setPadding(3);
}

// ============================================================================
// Configuration
// ============================================================================

void NoteExpressionLaneComponent::setClip(ClipId clipId) {
    if (clipId_ == clipId)
        return;

    clipId_ = clipId;
    noteIndex_.reset();
    if (const auto* clip = ClipManager::getInstance().getClip(clipId_))
        setCurveColour(clip->colour);
    reload();
}

void NoteExpressionLaneComponent::setSelectedNoteIndices(const std::vector<size_t>& indices) {
    const auto next = indices.size() == 1 ? std::optional(indices.front()) : std::nullopt;
    if (next == noteIndex_)
        return;

    noteIndex_ = next;
    clearSelection();
    reload();
}

void NoteExpressionLaneComponent::setPixelsPerBeat(double ppb) {
    if (pixelsPerBeat_ == ppb)
        return;
    pixelsPerBeat_ = ppb;
    updatePointPositions();
    repaint();
}

void NoteExpressionLaneComponent::setScrollOffset(int offsetX) {
    if (scrollOffsetX_ == offsetX)
        return;
    scrollOffsetX_ = offsetX;
    updatePointPositions();
    repaint();
}

void NoteExpressionLaneComponent::setLeftPadding(int padding) {
    if (leftPadding_ == padding)
        return;
    leftPadding_ = padding;
    updatePointPositions();
    repaint();
}

void NoteExpressionLaneComponent::setRelativeMode(bool relative) {
    if (relativeMode_ == relative)
        return;
    relativeMode_ = relative;
    reload();
}

void NoteExpressionLaneComponent::setClipStartBeats(double startBeats) {
    if (clipStartBeats_ == startBeats)
        return;
    clipStartBeats_ = startBeats;
    reload();
}

void NoteExpressionLaneComponent::refreshNotes() {
    reload();
}

void NoteExpressionLaneComponent::reload() {
    pointsCacheDirty_ = true;
    rebuildPointComponents();
    repaint();
}

// ============================================================================
// Coordinates and values
// ============================================================================

double NoteExpressionLaneComponent::pixelToX(int px) const {
    return velocity_lane::pixelToBeat(px, pixelsPerBeat_, leftPadding_, scrollOffsetX_);
}

int NoteExpressionLaneComponent::xToPixel(double x) const {
    return velocity_lane::beatToPixel(x, pixelsPerBeat_, leftPadding_, scrollOffsetX_);
}

juce::String NoteExpressionLaneComponent::formatValueLabel(double y) const {
    return juce::String(mpe::wireForValue(y));
}

double NoteExpressionLaneComponent::noteOrigin(const MidiNote& note) const {
    return relativeMode_ ? note.startBeat : clipStartBeats_ + note.startBeat;
}

const MidiNote* NoteExpressionLaneComponent::editableNote() const {
    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (!clip || !clip->isMidi() || !noteIndex_ || *noteIndex_ >= clip->midiNotes.size())
        return nullptr;
    return &clip->midiNotes[*noteIndex_];
}

const std::vector<CurvePoint>& NoteExpressionLaneComponent::getPoints() const {
    if (!pointsCacheDirty_)
        return cachedPoints_;

    cachedPoints_.clear();
    pointsCacheDirty_ = false;

    const auto* note = editableNote();
    if (!note)
        return cachedPoints_;

    const auto& lane = expressionLane(*note, dimension_);
    for (size_t i = 0; i < lane.size(); ++i) {
        CurvePoint point;
        point.id = static_cast<uint32_t>(i);
        point.x = noteOrigin(*note) + lane[i].beat;
        point.y = lane[i].value;
        point.curveType = CurveType::Linear;
        point.tension = lane[i].tension;
        cachedPoints_.push_back(point);
    }
    return cachedPoints_;
}

// ============================================================================
// Painting
// ============================================================================

void NoteExpressionLaneComponent::paintGrid(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    const auto width = static_cast<float>(bounds.getWidth());

    g.setColour(ActiveTheme::getColour(ActiveTheme::BACKGROUND_ALT));
    g.fillRect(bounds);

    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER).withAlpha(0.5f));
    for (const auto fraction : {0.25, 0.5, 0.75})
        g.drawHorizontalLine(yToPixel(fraction), 0.0f, width);

    const auto* clip = ClipManager::getInstance().getClip(clipId_);
    if (clip && clip->isMidi()) {
        const auto colour = getCurveColour();
        const auto rest = restOf(dimension_);

        for (size_t i = 0; i < clip->midiNotes.size(); ++i) {
            const auto& note = clip->midiNotes[i];
            const auto left = xToPixel(noteOrigin(note));
            const auto right = xToPixel(noteOrigin(note) + note.lengthBeats);
            if (right < 0 || left > bounds.getWidth())
                continue;

            const auto edited = noteIndex_ == i;
            g.setColour(colour.withAlpha(edited ? 0.14f : 0.05f));
            g.fillRect(left, 0, std::max(1, right - left), bounds.getHeight());

            // The edited note's curve is the editor's own; the rest are context.
            if (edited)
                continue;

            const auto& lane = expressionLane(note, dimension_);
            juce::Path path;
            for (auto px = left; px <= right; px += 2) {
                const auto beat = pixelToX(px) - noteOrigin(note);
                const auto value = lane.empty() ? rest : evaluateNoteExpressionCurve(lane, beat);
                const auto y = static_cast<float>(yToPixelF(value));
                if (px == left)
                    path.startNewSubPath(static_cast<float>(px), y);
                else
                    path.lineTo(static_cast<float>(px), y);
            }
            g.setColour(colour.withAlpha(lane.empty() ? 0.12f : 0.3f));
            g.strokePath(path, juce::PathStrokeType(1.0f));
        }
    }

    if (!editableNote()) {
        g.setFont(FontManager::getInstance().getUIFont(10.0f));
        g.setColour(ActiveTheme::getColour(ActiveTheme::TEXT_DIM));
        g.drawText("Select one note to edit its " + getLaneName().toLowerCase(), bounds,
                   juce::Justification::centred, true);
    }

    g.setColour(ActiveTheme::getColour(ActiveTheme::BORDER));
    g.drawHorizontalLine(0, 0.0f, width);
}

void NoteExpressionLaneComponent::paintCurve(juce::Graphics& g) {
    const auto* note = editableNote();
    if (!note)
        return;

    const auto left = xToPixel(noteOrigin(*note));
    const auto right = xToPixel(noteOrigin(*note) + note->lengthBeats);

    // A note with no curve sits at rest: shown as such, so a first point has a line to leave.
    if (expressionLane(*note, dimension_).empty()) {
        const auto y = static_cast<float>(yToPixelF(restOf(dimension_)));
        g.setColour(getCurveColour().withAlpha(0.5f));
        const float dashes[] = {4.0f, 3.0f};
        g.drawDashedLine({static_cast<float>(left), y, static_cast<float>(right), y}, dashes, 2);
        return;
    }

    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(left, 0, std::max(1, right - left), getHeight());
    CurveEditorBase::paintCurve(g);
}

// ============================================================================
// Edits: each one replaces the note's lane, as one undo step
// ============================================================================

void NoteExpressionLaneComponent::commit(std::vector<MidiNoteExpressionPoint> points) {
    if (!noteIndex_)
        return;

    std::ranges::stable_sort(points, {}, &MidiNoteExpressionPoint::beat);
    UndoManager::getInstance().executeCommand(std::make_unique<SetNoteExpressionCommand>(
        clipId_, *noteIndex_, dimension_, std::move(points)));
}

void NoteExpressionLaneComponent::constrainPointPosition(uint32_t /*pointId*/, double& x,
                                                         double& y) {
    if (const auto* note = editableNote())
        x = std::clamp(x, noteOrigin(*note), noteOrigin(*note) + note->lengthBeats);
    y = std::clamp(y, 0.0, 1.0);
}

void NoteExpressionLaneComponent::onPointAdded(double x, double y, CurveType /*curveType*/) {
    const auto* note = editableNote();
    if (!note)
        return;

    const auto beat = x - noteOrigin(*note);
    if (beat < 0.0 || beat > note->lengthBeats)
        return;

    auto points = expressionLane(*note, dimension_);
    points.push_back({.beat = beat, .value = std::clamp(y, 0.0, 1.0)});
    commit(std::move(points));
}

void NoteExpressionLaneComponent::onPointMoved(uint32_t pointId, double newX, double newY) {
    onSelectedPointsMoved({{pointId, {newX, newY}}});
}

void NoteExpressionLaneComponent::onSelectedPointsMoved(
    const std::map<uint32_t, std::pair<double, double>>& finalPositions) {
    const auto* note = editableNote();
    if (!note)
        return;

    auto points = expressionLane(*note, dimension_);
    for (const auto& [id, position] : finalPositions) {
        if (id >= points.size())
            continue;
        points[id].beat = std::clamp(position.first - noteOrigin(*note), 0.0, note->lengthBeats);
        points[id].value = std::clamp(position.second, 0.0, 1.0);
    }
    commit(std::move(points));
}

void NoteExpressionLaneComponent::onPointDeleted(uint32_t pointId) {
    onDeleteSelectedPoints({pointId});
}

void NoteExpressionLaneComponent::onDeleteSelectedPoints(const std::set<uint32_t>& pointIds) {
    const auto* note = editableNote();
    if (!note || pointIds.empty())
        return;

    auto points = expressionLane(*note, dimension_);
    for (auto id = pointIds.rbegin(); id != pointIds.rend(); ++id)
        if (*id < points.size())
            points.erase(points.begin() + static_cast<std::ptrdiff_t>(*id));
    clearSelection();
    commit(std::move(points));
}

void NoteExpressionLaneComponent::onTensionChanged(uint32_t pointId, double tension) {
    const auto* note = editableNote();
    if (!note)
        return;

    auto points = expressionLane(*note, dimension_);
    if (pointId >= points.size())
        return;
    points[pointId].tension = tension;
    commit(std::move(points));
}

void NoteExpressionLaneComponent::onPointSelected(uint32_t /*pointId*/) {}

// Expression segments bend by tension alone, as the pitch glide does.
void NoteExpressionLaneComponent::onHandlesChanged(uint32_t /*pointId*/,
                                                   const CurveHandleData& /*inHandle*/,
                                                   const CurveHandleData& /*outHandle*/) {}

}  // namespace magda
