#include "AutomationClipEditorContent.hpp"

#include "../../state/TimelineController.hpp"
#include "../../themes/DarkTheme.hpp"
#include "../../themes/FontManager.hpp"
#include "core/AutomationInfo.hpp"

namespace magda::daw::ui {

namespace {
constexpr int kTitleHeight = 22;
constexpr int kInset = 8;
// Horizontal breathing room inside the canvas so points at the clip edges
// aren't clipped by the viewport; the ruler's left padding matches, keeping
// beat 0 aligned between ruler and curve.
constexpr int kEdgePad = 10;
constexpr double kMinZoom = 2.0;
constexpr double kMaxZoom = 500.0;
}  // namespace

AutomationClipEditorContent::AutomationClipEditorContent() {
    titleLabel_.setFont(FontManager::getInstance().getUIFont(12.0f));
    titleLabel_.setColour(juce::Label::textColourId, DarkTheme::getTextColour());
    addAndMakeVisible(titleLabel_);

    timeRuler_ = std::make_unique<magda::TimeRuler>();
    timeRuler_->setDisplayMode(magda::TimeRuler::DisplayMode::BarsBeats);
    timeRuler_->setLeftPadding(kEdgePad);
    timeRuler_->setLinkedViewport(&viewport_);
    timeRuler_->onZoomChanged = [this](double newZoom, double anchorTime, int anchorScreenX) {
        performAnchorPointZoom(newZoom, anchorTime, anchorScreenX);
    };
    timeRuler_->onScrollRequested = [this](int deltaX) {
        viewport_.setViewPosition(juce::jmax(0, viewport_.getViewPositionX() + deltaX),
                                  viewport_.getViewPositionY());
    };
    addChildComponent(*timeRuler_);

    viewport_.setViewedComponent(&canvas_, false);
    viewport_.setScrollBarsShown(false, true);
    addAndMakeVisible(viewport_);
}

AutomationClipEditorContent::~AutomationClipEditorContent() {
    magda::SelectionManager::getInstance().removeListener(this);
    magda::AutomationManager::getInstance().removeListener(this);
}

void AutomationClipEditorContent::onActivated() {
    magda::SelectionManager::getInstance().addListener(this);
    magda::AutomationManager::getInstance().addListener(this);
    refreshFromSelection();
}

void AutomationClipEditorContent::onDeactivated() {
    magda::SelectionManager::getInstance().removeListener(this);
    magda::AutomationManager::getInstance().removeListener(this);
}

void AutomationClipEditorContent::paint(juce::Graphics& g) {
    g.fillAll(DarkTheme::getBackgroundColour());
    if (editor_ == nullptr) {
        g.setColour(DarkTheme::getSecondaryTextColour());
        g.setFont(FontManager::getInstance().getUIFont(13.0f));
        g.drawText("No automation clip selected", getLocalBounds(), juce::Justification::centred);
    }
}

void AutomationClipEditorContent::resized() {
    auto bounds = getLocalBounds().reduced(kInset);
    titleLabel_.setBounds(bounds.removeFromTop(kTitleHeight));
    timeRuler_->setBounds(bounds.removeFromTop(timeRuler_->getPreferredHeight()));
    viewport_.setBounds(bounds);
    updateView();
}

const magda::AutomationClipInfo* AutomationClipEditorContent::getClip() const {
    if (!selection_.isValid())
        return nullptr;
    return magda::AutomationManager::getInstance().getClip(selection_.clipId);
}

double AutomationClipEditorContent::viewSpanBeats(const magda::AutomationClipInfo& clip) const {
    // Looped -> one loop cycle (the clip's own timeline); else content length.
    const bool looped = clip.looping && clip.loopLengthBeats > 0.0;
    return juce::jmax(looped ? clip.loopLengthBeats : clip.lengthBeats, 0.25);
}

void AutomationClipEditorContent::refreshFromSelection() {
    auto& sm = magda::SelectionManager::getInstance();
    magda::AutomationClipSelection selection;
    if (sm.getSelectionType() == magda::SelectionType::AutomationClip)
        selection = sm.getAutomationClipSelection();

    if (selection.clipId == selection_.clipId && selection.laneId == selection_.laneId &&
        editor_ != nullptr)
        return;

    selection_ = selection;
    horizontalZoom_ = 0.0;  // refit for the new clip
    rebuildEditor();
}

void AutomationClipEditorContent::rebuildEditor() {
    editor_.reset();

    const auto* clip = getClip();
    if (clip == nullptr) {
        titleLabel_.setText("", juce::dontSendNotification);
        timeRuler_->setVisible(false);
        repaint();
        return;
    }

    juce::String title = clip->name;
    if (const auto* lane = magda::AutomationManager::getInstance().getLane(selection_.laneId))
        title = magda::getDisplayNameForTarget(lane->target) + "  -  " + clip->name;
    titleLabel_.setText(title, juce::dontSendNotification);

    editor_ = std::make_unique<magda::AutomationCurveEditor>(selection_.laneId);
    editor_->setClipId(selection_.clipId);
    editor_->setDrawMode(magda::AutomationDrawMode::Pencil);
    editor_->snapBeatToGrid = [](double beats) {
        if (auto* tc = TimelineController::getCurrent())
            return tc->getState().snapBeatsToGrid(beats);
        return beats;
    };
    editor_->getGridSpacingBeats = []() -> double {
        if (auto* tc = TimelineController::getCurrent())
            return tc->getState().getSnapBeatFraction();
        return 1.0;
    };
    canvas_.addAndMakeVisible(*editor_);
    updateView();
    repaint();
}

void AutomationClipEditorContent::updateView() {
    const auto* clip = getClip();
    if (clip == nullptr || editor_ == nullptr)
        return;

    const bool looped = clip->looping && clip->loopLengthBeats > 0.0;
    const double span = viewSpanBeats(*clip);

    // Abs view: coordinates in real timeline beats; loop view: the clip's own
    // timeline from 0.
    editor_->setClipOffset(looped ? 0.0 : clip->startBeats);

    // Fit once per clip; ruler gestures own the zoom afterwards.
    if (horizontalZoom_ <= 0.0) {
        const int fitWidth = viewport_.getMaximumVisibleWidth() - 2 * kEdgePad;
        if (fitWidth <= 0)
            return;  // not laid out yet; resized() comes back here
        horizontalZoom_ = juce::jlimit(kMinZoom, kMaxZoom, fitWidth / span);
    }

    layoutCanvas();

    // Ruler: looped clips read in the clip's own timeline from bar 1 (rel
    // mode); non-looped clips read in real arrangement time (abs).
    timeRuler_->setVisible(true);
    double bpm = 120.0;
    double startSeconds = 0.0;
    double lengthSeconds = span * 60.0 / bpm;
    if (auto* tc = TimelineController::getCurrent()) {
        const auto& state = tc->getState();
        bpm = state.tempo.bpm;
        timeRuler_->setTimeSignature(state.tempo.timeSignatureNumerator,
                                     state.tempo.timeSignatureDenominator);
        timeRuler_->setTimelineLength(state.timelineLength);
        startSeconds = state.beatsToSeconds(clip->startBeats);
        lengthSeconds =
            state.beatsToSeconds(clip->startBeats + span) - state.beatsToSeconds(clip->startBeats);
        timeRuler_->setGridResolution(state.getSnapBeatFraction());
    }
    timeRuler_->setTempo(bpm);
    timeRuler_->setBarOrigin(0.0);
    timeRuler_->setZoom(horizontalZoom_);
    timeRuler_->setClipLength(lengthSeconds);
    timeRuler_->setTimeOffset(looped ? 0.0 : startSeconds);
    timeRuler_->setRelativeMode(looped);
    timeRuler_->repaint();
}

void AutomationClipEditorContent::layoutCanvas() {
    const auto* clip = getClip();
    if (clip == nullptr || editor_ == nullptr || horizontalZoom_ <= 0.0) {
        canvas_.setSize(juce::jmax(1, viewport_.getMaximumVisibleWidth()),
                        juce::jmax(1, viewport_.getMaximumVisibleHeight()));
        return;
    }

    const double span = viewSpanBeats(*clip);
    const int contentWidth = static_cast<int>(std::round(span * horizontalZoom_));
    const int height = juce::jmax(1, viewport_.getMaximumVisibleHeight());
    canvas_.setSize(juce::jmax(contentWidth + 2 * kEdgePad, viewport_.getMaximumVisibleWidth()),
                    height);
    editor_->setBounds(kEdgePad, 0, contentWidth, height);
    editor_->setPixelsPerBeat(horizontalZoom_);
}

void AutomationClipEditorContent::performAnchorPointZoom(double newZoom, double anchorTime,
                                                         int anchorScreenX) {
    double bpm = 120.0;
    if (auto* tc = TimelineController::getCurrent())
        bpm = tc->getState().tempo.bpm;

    const double clamped = juce::jlimit(kMinZoom, kMaxZoom, newZoom);
    if (clamped == horizontalZoom_)
        return;

    const double anchorBeat = anchorTime * bpm / 60.0;
    horizontalZoom_ = clamped;
    layoutCanvas();
    timeRuler_->setZoom(horizontalZoom_);

    // Keep the beat under the pointer stationary while zooming.
    const int newAnchorX = static_cast<int>(std::round(anchorBeat * horizontalZoom_)) + kEdgePad;
    viewport_.setViewPosition(juce::jmax(0, newAnchorX - anchorScreenX),
                              viewport_.getViewPositionY());
    timeRuler_->repaint();
}

void AutomationClipEditorContent::selectionTypeChanged(magda::SelectionType newType) {
    juce::ignoreUnused(newType);
    refreshFromSelection();
}

void AutomationClipEditorContent::automationClipSelectionChanged(
    const magda::AutomationClipSelection& selection) {
    juce::ignoreUnused(selection);
    refreshFromSelection();
}

void AutomationClipEditorContent::automationLanesChanged() {
    // The lane (and its clips) may be gone entirely.
    if (selection_.isValid() &&
        magda::AutomationManager::getInstance().getLane(selection_.laneId) == nullptr) {
        selection_ = {};
        rebuildEditor();
    }
}

void AutomationClipEditorContent::automationClipsChanged(magda::AutomationLaneId laneId) {
    if (!selection_.isValid() || laneId != selection_.laneId)
        return;
    if (getClip() == nullptr) {
        // Our clip was deleted.
        selection_ = {};
        rebuildEditor();
        return;
    }
    // Length / loop / position changes update the view (offset, span, ruler).
    updateView();
    repaint();
}

}  // namespace magda::daw::ui
