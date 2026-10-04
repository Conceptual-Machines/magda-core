#include "modulation/LFOCurveEditor.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "DisplayListGraphics.hpp"
#include "core/TrackManager.hpp"
#include "core/UndoManager.hpp"
#include "ui/themes/FontManager.hpp"
#include "ui/themes/SdkColourRoles.hpp"

namespace magda {

namespace {

constexpr int kFrameIntervalMs = 33;
constexpr int kTriggerHoldFrames = 4;

bool sameCurvePoints(const std::vector<CurvePointData>& a, const std::vector<CurvePointData>& b) {
    if (a.size() != b.size())
        return false;

    constexpr float epsilon = 1.0e-6f;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto& lhs = a[i];
        const auto& rhs = b[i];
        if (std::abs(lhs.phase - rhs.phase) > epsilon ||
            std::abs(lhs.value - rhs.value) > epsilon ||
            std::abs(lhs.tension - rhs.tension) > epsilon || lhs.curveType != rhs.curveType ||
            std::abs(lhs.inHandleX - rhs.inHandleX) > epsilon ||
            std::abs(lhs.inHandleY - rhs.inHandleY) > epsilon ||
            std::abs(lhs.outHandleX - rhs.outHandleX) > epsilon ||
            std::abs(lhs.outHandleY - rhs.outHandleY) > epsilon) {
            return false;
        }
    }
    return true;
}

class SetLFOCurveStateCommand : public UndoableCommand {
  public:
    SetLFOCurveStateCommand(ChainNodePath ownerPath, int modIndex, CurvePreset beforePreset,
                            std::vector<CurvePointData> beforePoints, CurvePreset afterPreset,
                            std::vector<CurvePointData> afterPoints, juce::String description)
        : ownerPath_(std::move(ownerPath)),
          modIndex_(modIndex),
          beforePreset_(beforePreset),
          beforePoints_(std::move(beforePoints)),
          afterPreset_(afterPreset),
          afterPoints_(std::move(afterPoints)),
          description_(std::move(description)) {}

    void execute() override {
        TrackManager::getInstance().setModCurveState(ownerPath_, modIndex_, afterPreset_,
                                                     afterPoints_);
    }

    void undo() override {
        TrackManager::getInstance().setModCurveState(ownerPath_, modIndex_, beforePreset_,
                                                     beforePoints_);
    }

    juce::String getDescription() const override {
        return description_;
    }

  private:
    ChainNodePath ownerPath_;
    int modIndex_ = -1;
    CurvePreset beforePreset_ = CurvePreset::Custom;
    std::vector<CurvePointData> beforePoints_;
    CurvePreset afterPreset_ = CurvePreset::Custom;
    std::vector<CurvePointData> afterPoints_;
    juce::String description_;
};

CurvePointData curvePoint(float phase, float value, float tension = 0.0f) {
    CurvePointData p;
    p.phase = phase;
    p.value = value;
    p.tension = tension;
    return p;
}

std::vector<CurvePointData> presetPoints(CurvePreset preset) {
    switch (preset) {
        case CurvePreset::Sine:
            // Tension shapes each quarter: negative eases out, positive eases in.
            return {curvePoint(0.0f, 0.5f, -0.7f), curvePoint(0.25f, 1.0f, 0.7f),
                    curvePoint(0.5f, 0.5f, -0.7f), curvePoint(0.75f, 0.0f, 0.7f),
                    curvePoint(1.0f, 0.5f)};
        case CurvePreset::RampUp:
            return {curvePoint(0.0f, 0.0f), curvePoint(1.0f, 1.0f)};
        case CurvePreset::RampDown:
            return {curvePoint(0.0f, 1.0f), curvePoint(1.0f, 0.0f)};
        case CurvePreset::SCurve:
            return {curvePoint(0.0f, 0.0f, 0.8f), curvePoint(0.5f, 0.5f, -0.8f),
                    curvePoint(1.0f, 1.0f)};
        case CurvePreset::Exponential:
            return {curvePoint(0.0f, 0.0f, 1.2f), curvePoint(1.0f, 1.0f)};
        case CurvePreset::Logarithmic:
            return {curvePoint(0.0f, 0.0f, -1.2f), curvePoint(1.0f, 1.0f)};
        case CurvePreset::Triangle:
        case CurvePreset::Custom:
        default:
            return {curvePoint(0.0f, 0.0f), curvePoint(0.5f, 1.0f), curvePoint(1.0f, 0.0f)};
    }
}

sdk::CurveEditorModifiers modifiersOf(const juce::ModifierKeys& mods) {
    return {mods.isShiftDown(), mods.isCommandDown(), mods.isAltDown(), mods.isPopupMenu()};
}

sdk::CurveEditorPointer pointerOf(const juce::MouseEvent& e) {
    return {e.position.x, e.position.y, modifiersOf(e.mods), e.mods.isLeftButtonDown()};
}

}  // namespace

LFOCurveEditor::LFOCurveEditor() {
    setName("LFOCurveEditor");
    setWantsKeyboardFocus(true);
    core_.setTextMeasure([](std::string_view text, float fontSize) {
        const auto font = FontManager::getInstance().getUIFont(fontSize);
        return static_cast<float>(juce::GlyphArrangement::getStringWidthInt(
            font, juce::String::fromUTF8(text.data(), static_cast<int>(text.size()))));
    });
    startTimer(kFrameIntervalMs);
}

LFOCurveEditor::~LFOCurveEditor() {
    stopTimer();
}

void LFOCurveEditor::setUndoTarget(const ChainNodePath& ownerPath, int modIndex) {
    undoOwnerPath_ = ownerPath;
    undoModIndex_ = modIndex;
}

void LFOCurveEditor::setModInfo(ModInfo* mod) {
    // Every edit comes back here through the panels' refresh; a same-sized curve updates in place
    // so ids, selection and a gesture in progress survive it.
    if (mod != nullptr && mod == modInfo_ && !core_.editorPoints().empty() &&
        mod->curvePoints.size() == core_.editorPoints().size()) {
        core_.refreshPoints(mod->curvePoints);
        repaint();
        return;
    }

    modInfo_ = mod;
    if (mod != nullptr && !mod->curvePoints.empty()) {
        core_.setPoints(mod->curvePoints);
    } else if (mod != nullptr) {
        core_.setPoints(presetPoints(CurvePreset::Triangle));
        notifyWaveformChanged();
    } else {
        core_.setPoints({});
    }
    repaint();
}

void LFOCurveEditor::syncFromModInfo() {
    if (modInfo_ == nullptr)
        return;
    core_.syncPoints(modInfo_->curvePoints);
    repaint();
}

void LFOCurveEditor::setGridDivisionsX(int divisions) {
    core_.setGrid(divisions, core_.gridDivisionsY());
    repaint();
}

void LFOCurveEditor::setGridDivisionsY(int divisions) {
    core_.setGrid(core_.gridDivisionsX(), divisions);
    repaint();
}

void LFOCurveEditor::setSnapX(bool snap) {
    snapX_ = snap;
    core_.setSnap(snapX_, snapY_, snapLoop_);
}

void LFOCurveEditor::setSnapY(bool snap) {
    snapY_ = snap;
    core_.setSnap(snapX_, snapY_, snapLoop_);
}

void LFOCurveEditor::setSnapLoop(bool snap) {
    snapLoop_ = snap;
    core_.setSnap(snapX_, snapY_, snapLoop_);
}

void LFOCurveEditor::loadPreset(CurvePreset preset) {
    const auto beforePoints = core_.points();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    core_.setPoints(presetPoints(preset));
    if (modInfo_ != nullptr)
        modInfo_->curvePreset = preset;

    repaint();
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Load LFO Curve Preset");
}

void LFOCurveEditor::loadCurvePoints(const std::vector<CurvePointData>& points) {
    if (points.size() < 2) {
        loadPreset(CurvePreset::Triangle);
        return;
    }

    const auto beforePoints = core_.points();
    const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;

    auto loaded = points;
    for (auto& p : loaded) {
        p.phase = std::clamp(p.phase, 0.0f, 1.0f);
        p.value = std::clamp(p.value, 0.0f, 1.0f);
    }
    core_.setPoints(loaded);
    if (modInfo_ != nullptr)
        modInfo_->curvePreset = CurvePreset::Custom;

    repaint();
    notifyWaveformChanged();
    commitUndoableCurveEdit(beforePoints, beforePreset, "Load LFO Curve Preset");
}

void LFOCurveEditor::notifyWaveformChanged() {
    if (modInfo_ != nullptr)
        modInfo_->curvePoints = core_.points();
    if (onWaveformChanged)
        onWaveformChanged();
}

void LFOCurveEditor::commitUndoableCurveEdit(const std::vector<CurvePointData>& beforePoints,
                                             CurvePreset beforePreset,
                                             const juce::String& description) {
    if (!undoOwnerPath_.isValid() || undoModIndex_ < 0 || modInfo_ == nullptr)
        return;

    auto afterPoints = core_.points();
    const auto afterPreset = modInfo_->curvePreset;
    if (beforePreset == afterPreset && sameCurvePoints(beforePoints, afterPoints))
        return;

    UndoManager::getInstance().executeCommand(std::make_unique<SetLFOCurveStateCommand>(
        undoOwnerPath_, undoModIndex_, beforePreset, beforePoints, afterPreset,
        std::move(afterPoints), description));
}

void LFOCurveEditor::syncSurface() {
    core_.setSize(getWidth(), getHeight());
    if (modInfo_ == nullptr) {
        core_.setLoopRegion(false, 0.0f, 1.0f);
        core_.clearIndicator();
        return;
    }
    core_.setLoopRegion(showLoopRegion_ && modInfo_->useLoopRegion, modInfo_->loopStart,
                        modInfo_->loopEnd);
    core_.setIndicator(modInfo_->phase, modInfo_->value, triggerHoldFrames_ > 0);
}

void LFOCurveEditor::apply(const sdk::CurveEditorResponse& response) {
    using Commit = sdk::CurveEditorResponse::Commit;

    if (response.preview && modInfo_ != nullptr) {
        modInfo_->curvePoints = core_.effectivePoints();
        if (onDragPreview)
            onDragPreview();
    }

    if ((response.loopPreview || response.loopCommitted) && modInfo_ != nullptr) {
        modInfo_->loopStart = core_.loopStart();
        modInfo_->loopEnd = core_.loopEnd();
        if (response.loopCommitted)
            notifyWaveformChanged();
        else if (onDragPreview)
            onDragPreview();
    }

    if (response.commit != Commit::None) {
        const auto beforePreset = modInfo_ ? modInfo_->curvePreset : CurvePreset::Custom;
        notifyWaveformChanged();
        commitUndoableCurveEdit(response.before, beforePreset,
                                response.commit == Commit::StampStep ? "Stamp LFO Step"
                                                                     : "Edit LFO Curve");
    }

    if (response.repaint || response.preview || response.commit != Commit::None)
        repaint();
}

juce::Colour LFOCurveEditor::roleColour(sdk::display::ColourRole role) const {
    using Role = sdk::display::ColourRole;
    if (role == Role::Background)
        return ActiveTheme::getColour(ActiveTheme::CURVE_BACKGROUND);
    if (role == Role::Curve)
        return getCurveColour();
    return sdkRoleColour(role);
}

void LFOCurveEditor::paint(juce::Graphics& g) {
    syncSurface();
    core_.render(displayList_);
    sdk::juce_host::drawDisplayList(
        g, displayList_, [this](sdk::display::ColourRole role) { return roleColour(role); },
        [](float size) { return FontManager::getInstance().getUIFont(size); });
}

void LFOCurveEditor::resized() {
    core_.setSize(getWidth(), getHeight());
}

void LFOCurveEditor::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    syncSurface();
    apply(core_.pointerDown(pointerOf(e)));
    updateCursor(e.mods);
}

void LFOCurveEditor::mouseDrag(const juce::MouseEvent& e) {
    apply(core_.pointerDrag(pointerOf(e)));
}

void LFOCurveEditor::mouseUp(const juce::MouseEvent& e) {
    apply(core_.pointerUp(pointerOf(e)));
    updateCursor(e.mods);
}

void LFOCurveEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    apply(core_.doubleClick(pointerOf(e)));
}

void LFOCurveEditor::mouseMove(const juce::MouseEvent& e) {
    syncSurface();
    apply(core_.pointerMove(pointerOf(e)));
    updateCursor(e.mods);
}

void LFOCurveEditor::mouseEnter(const juce::MouseEvent& e) {
    updateCursor(e.mods);
}

void LFOCurveEditor::mouseExit(const juce::MouseEvent&) {
    apply(core_.pointerExit());
}

void LFOCurveEditor::modifierKeysChanged(const juce::ModifierKeys& modifiers) {
    updateCursor(modifiers);
}

void LFOCurveEditor::updateCursor(const juce::ModifierKeys& modifiers) {
    switch (core_.cursor(modifiersOf(modifiers))) {
        case sdk::CurveEditorCursor::PointingHand:
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            break;
        case sdk::CurveEditorCursor::Copying:
            setMouseCursor(juce::MouseCursor::CopyingCursor);
            break;
        case sdk::CurveEditorCursor::DraggingHand:
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            break;
        case sdk::CurveEditorCursor::Normal:
            setMouseCursor(juce::MouseCursor::NormalCursor);
            break;
    }
}

bool LFOCurveEditor::keyPressed(const juce::KeyPress& key) {
    const auto command = juce::ModifierKeys::commandModifier;
    if (key == juce::KeyPress('z', command, 0)) {
        if (UndoManager::getInstance().undo())
            setModInfo(modInfo_);
        return true;
    }
    if (key == juce::KeyPress('z', command | juce::ModifierKeys::shiftModifier, 0)) {
        if (UndoManager::getInstance().redo())
            setModInfo(modInfo_);
        return true;
    }

    sdk::CurveEditorKey coreKey;
    coreKey.mods = modifiersOf(key.getModifiers());
    if (key.isKeyCode(juce::KeyPress::deleteKey)) {
        coreKey.code = sdk::CurveEditorKey::Code::Delete;
    } else if (key.isKeyCode(juce::KeyPress::backspaceKey)) {
        coreKey.code = sdk::CurveEditorKey::Code::Backspace;
    } else if (key.getKeyCode() > 0 && key.getKeyCode() < 128) {
        coreKey.code = sdk::CurveEditorKey::Code::Character;
        coreKey.character = static_cast<char32_t>(
            juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode())));
    }

    const auto response = core_.keyPressed(coreKey);
    apply(response);
    return response.handled;
}

void LFOCurveEditor::timerCallback() {
    if (modInfo_ == nullptr)
        return;

    bool needsRepaint = false;
    if (modInfo_->triggerCount != lastSeenTriggerCount_) {
        lastSeenTriggerCount_ = modInfo_->triggerCount;
        triggerHoldFrames_ = kTriggerHoldFrames;
        needsRepaint = true;
    }
    if (triggerHoldFrames_ > 0) {
        --triggerHoldFrames_;
        needsRepaint = true;
    }

    if (std::abs(modInfo_->phase - lastPhase_) > 0.001f ||
        std::abs(modInfo_->value - lastValue_) > 0.001f) {
        lastPhase_ = modInfo_->phase;
        lastValue_ = modInfo_->value;
        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

}  // namespace magda
