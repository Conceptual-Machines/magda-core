#include "GainEnvelopeEditor.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "../../themes/FontManager.hpp"
#include "WaveformGridComponent.hpp"
#include "core/ClipManager.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/UndoManager.hpp"
#include "magda/sdk/curve/AutomationCurve.hpp"

namespace magda::daw::ui {

namespace {

magda::CurveType toCurveType(sdk::CurveInterpolation type) {
    switch (type) {
        case sdk::CurveInterpolation::Bezier:
            return magda::CurveType::Bezier;
        case sdk::CurveInterpolation::Step:
            return magda::CurveType::Step;
        case sdk::CurveInterpolation::HardCorner:
            return magda::CurveType::HardCorner;
        case sdk::CurveInterpolation::Linear:
            break;
    }
    return magda::CurveType::Linear;
}

sdk::CurveInterpolation toInterpolation(magda::CurveType type) {
    switch (type) {
        case magda::CurveType::Bezier:
            return sdk::CurveInterpolation::Bezier;
        case magda::CurveType::Step:
            return sdk::CurveInterpolation::Step;
        case magda::CurveType::HardCorner:
            return sdk::CurveInterpolation::HardCorner;
        case magda::CurveType::Linear:
            break;
    }
    return sdk::CurveInterpolation::Linear;
}

}  // namespace

GainEnvelopeEditor::GainEnvelopeEditor(WaveformGridComponent& grid) : grid_(grid) {
    setName("GainEnvelopeEditor");
    setWantsKeyboardFocus(true);
    setDrawMode(magda::CurveDrawMode::Select);
    setPadding(4);
    setCurveColour(juce::Colours::white);

    // The Flat tool always stamps a grid cell; everything else snaps with SNAP.
    snapXToGrid = [this](double x) {
        const auto snaps = grid_.isSnapEnabled() || tool() == magda::EditTool::Flat;
        return snaps ? grid_.snapTimeToGrid(x) : x;
    };
    getGridSpacingX = [this] { return grid_.getGridSpacingSeconds(); };

    magda::EditToolState::audioEditor().addChangeListener(this);
    updateCursorForModifiers(juce::ModifierKeys::currentModifiers);
}

GainEnvelopeEditor::~GainEnvelopeEditor() {
    magda::EditToolState::audioEditor().removeChangeListener(this);
}

void GainEnvelopeEditor::changeListenerCallback(juce::ChangeBroadcaster*) {
    updateCursorForModifiers(juce::ModifierKeys::currentModifiers);
}

magda::EditTool GainEnvelopeEditor::tool() const {
    // Pen, Line and Flat are the toolbox; anything else is the default, Line.
    const auto chosen = magda::EditToolState::audioEditor().getTool();
    const auto offered = chosen == magda::EditTool::Pencil || chosen == magda::EditTool::Flat;
    return offered ? chosen : magda::EditTool::Line;
}

magda::CurveDrawMode GainEnvelopeEditor::plainPressMode() const {
    switch (tool()) {
        case magda::EditTool::Flat:
            return magda::CurveDrawMode::Line;  // the base's grid-cell stamp
        case magda::EditTool::Line:
            return magda::CurveDrawMode::Select;  // drawn here, see mouseDown
        default:
            return magda::CurveDrawMode::Pencil;
    }
}

void GainEnvelopeEditor::updateCursorForModifiers(const juce::ModifierKeys& modifiers) {
    if (modifiers.isAnyModifierKeyDown()) {
        CurveEditorBase::updateCursorForModifiers(modifiers);
        return;
    }
    const auto cursor = magda::cursorForEditTool(tool());
    setMouseCursor(cursor.value_or(juce::MouseCursor::NormalCursor));
}

// ============================================================================
// The Line tool: press, drag, release
// ============================================================================

bool GainEnvelopeEditor::drawsLine(const juce::MouseEvent& e) const {
    return tool() == magda::EditTool::Line && e.mods.isLeftButtonDown() &&
           !e.mods.isAnyModifierKeyDown();
}

void GainEnvelopeEditor::mouseDown(const juce::MouseEvent& e) {
    if (!drawsLine(e)) {
        CurveEditorBase::mouseDown(e);
        return;
    }
    grabKeyboardFocus();
    line_ = std::pair{e.getPosition(), e.getPosition()};
    repaint();
}

void GainEnvelopeEditor::mouseDrag(const juce::MouseEvent& e) {
    if (!line_) {
        CurveEditorBase::mouseDrag(e);
        return;
    }
    line_->second = e.getPosition();
    repaint();
}

void GainEnvelopeEditor::mouseUp(const juce::MouseEvent& e) {
    if (!line_) {
        CurveEditorBase::mouseUp(e);
        return;
    }
    const auto [from, to] = *line_;
    line_.reset();
    commitLine(from, to);
    repaint();
}

void GainEnvelopeEditor::commitLine(juce::Point<int> from, juce::Point<int> to) {
    if (event() == nullptr)
        return;

    const auto toEnvelope = [this](juce::Point<int> pixel) {
        auto x = pixelToX(pixel.x);
        auto y = pixelToY(pixel.y);
        if (snapXToGrid)
            x = snapXToGrid(x);
        constrainPointPosition(magda::INVALID_CURVE_POINT_ID, x, y);
        return sdk::AutomationPoint{.beatPosition = sourceAt(x), .value = y};
    };

    auto points = modelPoints();
    const auto start = toEnvelope(from);
    const auto end = toEnvelope(to);

    // A click without a drag is a point; a drag replaces what lay under the ramp.
    if (from.getDistanceFrom(to) >= 3 && start.beatPosition != end.beatPosition) {
        const auto lo = std::min(start.beatPosition, end.beatPosition);
        const auto hi = std::max(start.beatPosition, end.beatPosition);
        std::erase_if(points, [&](const sdk::AutomationPoint& point) {
            return point.beatPosition >= lo && point.beatPosition <= hi;
        });
        points.push_back(end);
    }
    points.push_back(start);
    commit(std::move(points));
}

void GainEnvelopeEditor::setClip(magda::ClipId clipId) {
    if (clipId_ != clipId) {
        clipId_ = clipId;
        clearSelection();
    }
    if (const auto* clip = magda::ClipManager::getInstance().getClip(clipId_))
        setCurveColour(clip->colour);
    refresh();
}

void GainEnvelopeEditor::refresh() {
    rebuildPointComponents();
    repaint();
}

void GainEnvelopeEditor::rebuildPointComponents() {
    pointsCacheDirty_ = true;
    CurveEditorBase::rebuildPointComponents();
}

// ============================================================================
// Coordinates and values
// ============================================================================

double GainEnvelopeEditor::getPixelsPerX() const {
    return grid_.getHorizontalZoom();
}

double GainEnvelopeEditor::pixelToX(int px) const {
    return grid_.pixelToTime(px + getX());
}

int GainEnvelopeEditor::xToPixel(double x) const {
    return grid_.timeToPixel(x) - getX();
}

double GainEnvelopeEditor::xToPixelF(double x) const {
    return grid_.timeToPixelF(x) - getX();
}

juce::String GainEnvelopeEditor::formatValueLabel(double y) const {
    const auto db = gain_envelope::positionToDb(y);
    if (!std::isfinite(db))
        return "-inf dB";
    return juce::String(db, 1) + " dB";
}

double GainEnvelopeEditor::sourceAt(double displayTime) const {
    return grid_.displayTimeToSource(displayTime);
}

sdk::BezierHandle GainEnvelopeEditor::toSource(double displayX,
                                               const magda::CurveHandleData& handle) const {
    return {sourceAt(displayX + handle.x) - sourceAt(displayX), handle.y, handle.linked};
}

const magda::AudioEvent* GainEnvelopeEditor::event() const {
    const auto* clip = magda::ClipManager::getInstance().getClip(clipId_);
    return clip && clip->isAudio() ? clip->primaryEvent() : nullptr;
}

std::vector<sdk::AutomationPoint> GainEnvelopeEditor::modelPoints() const {
    const auto* ev = event();
    return ev ? ev->gainEnvelope.points : std::vector<sdk::AutomationPoint>{};
}

sdk::AutomationPoint* GainEnvelopeEditor::find(std::vector<sdk::AutomationPoint>& points,
                                               uint32_t id) const {
    const auto it = std::ranges::find(points, static_cast<int>(id), &sdk::AutomationPoint::id);
    return it == points.end() ? nullptr : &*it;
}

const std::vector<magda::CurvePoint>& GainEnvelopeEditor::getPoints() const {
    if (!pointsCacheDirty_)
        return cachedPoints_;

    cachedPoints_.clear();
    pointsCacheDirty_ = false;

    for (const auto& point : modelPoints()) {
        const auto x = grid_.sourceToDisplayTime(point.beatPosition);
        const auto displayOffset = [&](double sourceOffset) {
            return grid_.sourceToDisplayTime(point.beatPosition + sourceOffset) - x;
        };

        magda::CurvePoint cp;
        cp.id = static_cast<uint32_t>(point.id);
        cp.x = x;
        cp.y = point.value;
        cp.curveType = toCurveType(point.curveType);
        cp.tension = point.tension;
        cp.inHandle = {displayOffset(point.inHandle.beatOffset), point.inHandle.value,
                       point.inHandle.linked};
        cp.outHandle = {displayOffset(point.outHandle.beatOffset), point.outHandle.value,
                        point.outHandle.linked};
        cachedPoints_.push_back(cp);
    }

    // Reversed material puts source order right to left.
    std::ranges::sort(cachedPoints_, {}, &magda::CurvePoint::x);
    return cachedPoints_;
}

// ============================================================================
// Painting
// ============================================================================

void GainEnvelopeEditor::paint(juce::Graphics& g) {
    // Transparent over the waveform, unlike the base's own background.
    paintCurve(g);

    if (isDrawing_)
        paintDrawingPreview(g);

    if (line_) {
        g.setColour(getCurveColour().brighter(0.6f));
        g.drawLine(line_->first.toFloat().x, line_->first.toFloat().y, line_->second.toFloat().x,
                   line_->second.toFloat().y, 1.5f);
    }

    if (isLassoActive_ && !lassoRect_.isEmpty()) {
        g.setColour(getCurveColour().withAlpha(0.15f));
        g.fillRect(lassoRect_);
        g.setColour(getCurveColour().withAlpha(0.6f));
        g.drawRect(lassoRect_, 1);
    }
}

void GainEnvelopeEditor::paintGrid(juce::Graphics& /*g*/) {}

void GainEnvelopeEditor::paintCurve(juce::Graphics& g) {
    const auto [displayStart, displayEnd] = grid_.sourceDisplayRange();
    const auto left = std::max(0, xToPixel(displayStart));
    const auto right = std::min(getWidth(), xToPixel(displayEnd));
    if (right <= left)
        return;

    // The model's points with any drag preview applied, back in source seconds.
    auto points = modelPoints();
    for (const auto& cp : getPoints()) {
        if (auto* point = find(points, cp.id)) {
            const auto [x, y] = getEffectivePosition(cp);
            point->beatPosition = sourceAt(x);
            point->value = y;
        }
    }
    std::sort(points.begin(), points.end());

    paintLevelGuides(g, left, right);

    const auto colour = getCurveColour();
    if (points.empty()) {
        const auto y = static_cast<float>(yToPixelF(gain_envelope::unityPosition()));
        g.setColour(colour.brighter(0.8f));
        const float dashes[] = {4.0f, 3.0f};
        g.drawDashedLine({static_cast<float>(left), y, static_cast<float>(right), y}, dashes, 2);
        return;
    }

    juce::Path curve;
    for (auto px = left; px <= right; ++px) {
        const auto value = sdk::automation::valueAtBeat(points, sourceAt(pixelToX(px)));
        const auto y = static_cast<float>(yToPixelF(value));
        if (px == left)
            curve.startNewSubPath(static_cast<float>(px), y);
        else
            curve.lineTo(static_cast<float>(px), y);
    }

    auto fill = curve;
    const auto floor = static_cast<float>(yToPixelF(0.0));
    fill.lineTo(static_cast<float>(right), floor);
    fill.lineTo(static_cast<float>(left), floor);
    fill.closeSubPath();
    g.setColour(colour.withAlpha(0.1f));
    g.fillPath(fill);

    g.setColour(colour);
    g.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

void GainEnvelopeEditor::paintLevelGuides(juce::Graphics& g, int left, int right) {
    const auto border = ActiveTheme::getColour(ActiveTheme::BORDER);
    g.setFont(FontManager::getInstance().getUIFont(9.5f));

    for (const auto db : {6.0, 0.0, -6.0, -12.0, -24.0}) {
        const auto y = static_cast<float>(yToPixelF(gain_envelope::dbToPosition(db)));
        g.setColour(border.withAlpha(db == 0.0 ? 0.9f : 0.55f));
        g.drawHorizontalLine(juce::roundToInt(y), static_cast<float>(left),
                             static_cast<float>(right));

        const auto label = db > 0.0 ? "+" + juce::String(static_cast<int>(db))
                                    : juce::String(static_cast<int>(db));
        g.setColour(ActiveTheme::getSecondaryTextColour().withAlpha(0.8f));
        const auto labelY = y < 14.0f ? juce::roundToInt(y) + 1 : juce::roundToInt(y) - 12;
        g.drawText(label, left + 4, labelY, 28, 11, juce::Justification::centredLeft, false);
    }
}

// ============================================================================
// Edits: each one replaces the envelope, as one undo step
// ============================================================================

void GainEnvelopeEditor::commit(std::vector<sdk::AutomationPoint> points) {
    if (event() == nullptr)
        return;
    magda::UndoManager::getInstance().executeCommand(
        std::make_unique<magda::SetClipGainEnvelopeCommand>(
            clipId_, magda::ClipGainEnvelope{std::move(points)}));
}

void GainEnvelopeEditor::constrainPointPosition(uint32_t /*pointId*/, double& x, double& y) {
    const auto [displayStart, displayEnd] = grid_.sourceDisplayRange();
    x = std::clamp(x, displayStart, displayEnd);
    y = std::clamp(y, 0.0, 1.0);
}

void GainEnvelopeEditor::onPointAdded(double x, double y, magda::CurveType curveType) {
    const auto [displayStart, displayEnd] = grid_.sourceDisplayRange();
    if (event() == nullptr || x < displayStart || x > displayEnd)
        return;

    auto points = modelPoints();
    points.push_back({.beatPosition = sourceAt(x),
                      .value = std::clamp(y, 0.0, 1.0),
                      .curveType = toInterpolation(curveType)});
    commit(std::move(points));
}

void GainEnvelopeEditor::onPointMoved(uint32_t pointId, double newX, double newY) {
    onSelectedPointsMoved({{pointId, {newX, newY}}});
}

void GainEnvelopeEditor::onSelectedPointsMoved(
    const std::map<uint32_t, std::pair<double, double>>& finalPositions) {
    auto points = modelPoints();
    for (const auto& [id, position] : finalPositions) {
        if (auto* point = find(points, id)) {
            auto x = position.first;
            auto y = position.second;
            constrainPointPosition(id, x, y);
            point->beatPosition = sourceAt(x);
            point->value = y;
        }
    }
    commit(std::move(points));
}

void GainEnvelopeEditor::onPointDeleted(uint32_t pointId) {
    onDeleteSelectedPoints({pointId});
}

void GainEnvelopeEditor::onDeleteSelectedPoints(const std::set<uint32_t>& pointIds) {
    if (pointIds.empty())
        return;

    auto points = modelPoints();
    std::erase_if(points, [&](const sdk::AutomationPoint& point) {
        return pointIds.contains(static_cast<uint32_t>(point.id));
    });
    clearSelection();
    commit(std::move(points));
}

void GainEnvelopeEditor::onPointSelected(uint32_t /*pointId*/) {}

void GainEnvelopeEditor::onTensionChanged(uint32_t pointId, double tension) {
    auto points = modelPoints();
    if (auto* point = find(points, pointId)) {
        point->tension = tension;
        commit(std::move(points));
    }
}

void GainEnvelopeEditor::onPointCurveTypeChanged(uint32_t pointId, magda::CurveType newType) {
    auto points = modelPoints();
    if (auto* point = find(points, pointId)) {
        point->curveType = toInterpolation(newType);
        commit(std::move(points));
    }
}

void GainEnvelopeEditor::onHandlesChanged(uint32_t pointId, const magda::CurveHandleData& inHandle,
                                          const magda::CurveHandleData& outHandle) {
    auto points = modelPoints();
    if (auto* point = find(points, pointId)) {
        const auto x = grid_.sourceToDisplayTime(point->beatPosition);
        point->inHandle = toSource(x, inHandle);
        point->outHandle = toSource(x, outHandle);
        commit(std::move(points));
    }
}

void GainEnvelopeEditor::onSegmentShaperChanged(uint32_t leftPointId,
                                                const magda::CurveHandleData& leftInHandle,
                                                const magda::CurveHandleData& leftOutHandle,
                                                uint32_t rightPointId,
                                                const magda::CurveHandleData& rightInHandle,
                                                const magda::CurveHandleData& rightOutHandle,
                                                bool isPreview) {
    // Committing mid-drag would rebuild the handle being dragged; persist on release.
    if (isPreview)
        return;

    auto points = modelPoints();
    for (const auto& [id, in, out] : {std::tuple{leftPointId, leftInHandle, leftOutHandle},
                                      std::tuple{rightPointId, rightInHandle, rightOutHandle}}) {
        if (auto* point = find(points, id)) {
            const auto x = grid_.sourceToDisplayTime(point->beatPosition);
            point->inHandle = toSource(x, in);
            point->outHandle = toSource(x, out);
        }
    }
    commit(std::move(points));
}

void GainEnvelopeEditor::onStepStamped(double gridStart, double gridEnd, double y,
                                       uint32_t prevPointId, double prevValue) {
    // Shift+click: one grid cell held flat at y, cliffing in and back out to the level before it.
    auto points = modelPoints();
    auto recoveryType = sdk::CurveInterpolation::Linear;
    auto* before = find(points, prevPointId);
    const auto hasBefore = before != nullptr;
    if (hasBefore) {
        recoveryType = before->curveType;
        before->curveType = sdk::CurveInterpolation::Step;
    }

    const auto value = std::clamp(y, 0.0, 1.0);
    points.push_back({.beatPosition = sourceAt(gridStart),
                      .value = value,
                      .curveType = sdk::CurveInterpolation::Step});
    if (hasBefore && gridEnd > gridStart)
        points.push_back(
            {.beatPosition = sourceAt(gridEnd), .value = prevValue, .curveType = recoveryType});
    commit(std::move(points));
}

void GainEnvelopeEditor::onStrokeDrawn(const std::vector<std::pair<double, double>>& stroke,
                                       magda::CurveType curveType) {
    if (stroke.empty() || event() == nullptr)
        return;

    // The stroke replaces whatever the envelope had under it.
    const auto a = sourceAt(stroke.front().first);
    const auto b = sourceAt(stroke.back().first);
    const auto from = std::min(a, b);
    const auto to = std::max(a, b);

    auto points = modelPoints();
    if (stroke.size() > 1)
        std::erase_if(points, [&](const sdk::AutomationPoint& point) {
            return point.beatPosition >= from && point.beatPosition <= to;
        });

    for (auto [x, y] : stroke) {
        constrainPointPosition(magda::INVALID_CURVE_POINT_ID, x, y);
        points.push_back(
            {.beatPosition = sourceAt(x), .value = y, .curveType = toInterpolation(curveType)});
    }
    commit(std::move(points));
}

}  // namespace magda::daw::ui
