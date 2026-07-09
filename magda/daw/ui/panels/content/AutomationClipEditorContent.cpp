#include "AutomationClipEditorContent.hpp"

#include <cmath>

#include "../../components/waveform/ClipWaveformPainter.hpp"
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
    magda::ClipManager::getInstance().removeListener(this);
}

void AutomationClipEditorContent::onActivated() {
    magda::SelectionManager::getInstance().addListener(this);
    magda::AutomationManager::getInstance().addListener(this);
    magda::ClipManager::getInstance().addListener(this);
    refreshFromSelection();
}

void AutomationClipEditorContent::onDeactivated() {
    magda::SelectionManager::getInstance().removeListener(this);
    magda::AutomationManager::getInstance().removeListener(this);
    magda::ClipManager::getInstance().removeListener(this);
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

void AutomationClipEditorContent::paintTrackGhost(juce::Graphics& g) {
    const auto* clip = getClip();
    if (clip == nullptr || editor_ == nullptr)
        return;
    const auto* lane = magda::AutomationManager::getInstance().getLane(selection_.laneId);
    if (lane == nullptr || lane->target.isEditScoped())
        return;
    const auto trackId = lane->target.devicePath.trackId;
    if (trackId == magda::INVALID_TRACK_ID)
        return;

    double tempo = 120.0;
    if (auto* tc = TimelineController::getCurrent())
        tempo = tc->getState().tempo.bpm;

    // Absolute view only: in the looped (relative) view every automation
    // cycle plays against DIFFERENT arrangement content, so any single
    // ghost would be wrong from the second cycle on.
    const bool looped = clip->looping && clip->loopLengthBeats > 0.0;
    if (looped)
        return;

    const double span = viewSpanBeats(*clip);
    const double arrStart = clip->startBeats;
    const double arrEnd = arrStart + span;
    // Editor x-domain is timeline beats in the absolute view.
    const auto beatToPixelF = [&](double arrBeat) {
        return static_cast<float>(editor_->xToPixelF(arrBeat));
    };

    const int editorH = editor_->getHeight();
    auto& clipManager = magda::ClipManager::getInstance();

    // MIDI ghosts get collected across all clips first so every note shares
    // ONE pitch axis (a per-clip fit would put the same pitch on different
    // rows for different clips).
    struct GhostNote {
        double beat, length;
        int pitch;
        juce::Colour colour;
    };
    std::vector<GhostNote> ghostNotes;
    int minPitch = 127;
    int maxPitch = 0;

    for (magda::ClipId cid : clipManager.getClipsOnTrack(trackId)) {
        const auto* c = clipManager.getClip(cid);
        if (c == nullptr || c->view == magda::ClipView::Session)
            continue;
        const double cStart = c->getStartBeats(tempo);
        const double cLenSeconds = c->getTimelineLength(tempo);
        const double cEnd = cStart + cLenSeconds * tempo / 60.0;
        if (cEnd <= arrStart || cStart >= arrEnd)
            continue;

        if (c->isMidi()) {
            // Mirror ClipComponent::paintMidiNotes' display math (loop
            // unroll, loop window, MIDI offsets) so the ghost agrees with
            // what the arrangement actually renders.
            const double beatsPerSecond = tempo / 60.0;
            const double clipLengthInBeats = cLenSeconds * beatsPerSecond;
            const double midiSrcLength =
                c->loopLength > 0.0 ? c->loopLength : cLenSeconds * c->speedRatio;
            const double loopLengthBeats =
                c->loopLengthBeats > 0.0
                    ? c->loopLengthBeats
                    : (midiSrcLength > 0.0 ? midiSrcLength * beatsPerSecond : clipLengthInBeats);
            const double midiOffset = c->loopEnabled ? c->midiOffset : c->midiTrimOffset;
            const double loopStart = c->loopStart * beatsPerSecond;
            const double loopEnd = loopStart + loopLengthBeats;

            const auto emitGhost = [&](double displayStart, double displayEnd, int pitch) {
                displayStart = juce::jmax(displayStart, 0.0);
                displayEnd = juce::jmin(displayEnd, clipLengthInBeats);
                if (displayEnd <= displayStart)
                    return;
                const double arrBeat = cStart + displayStart;
                if (arrBeat + (displayEnd - displayStart) <= arrStart || arrBeat >= arrEnd)
                    return;
                ghostNotes.push_back({arrBeat, displayEnd - displayStart, pitch, c->colour});
                minPitch = juce::jmin(minPitch, pitch);
                maxPitch = juce::jmax(maxPitch, pitch);
            };

            if (c->loopEnabled && loopLengthBeats > 0.0) {
                const int numReps =
                    static_cast<int>(std::ceil(clipLengthInBeats / loopLengthBeats));
                for (int rep = 0; rep < numReps; ++rep) {
                    for (const auto& note : c->midiNotes) {
                        const double sourceStart = juce::jmax(note.startBeat, loopStart);
                        const double sourceEnd =
                            juce::jmin(note.startBeat + note.lengthBeats, loopEnd);
                        const double sourceLength = sourceEnd - sourceStart;
                        if (sourceLength <= 0.0)
                            continue;
                        const double noteBeat =
                            loopStart +
                            magda::wrapPhase(sourceStart - midiOffset - loopStart, loopLengthBeats);
                        if (noteBeat < loopStart || noteBeat >= loopEnd)
                            continue;
                        const double displayStart = (noteBeat - loopStart) + rep * loopLengthBeats;
                        emitGhost(displayStart, displayStart + sourceLength, note.noteNumber);
                    }
                }
            } else {
                for (const auto& note : c->midiNotes) {
                    const double displayStart = note.startBeat - midiOffset;
                    emitGhost(displayStart, displayStart + note.lengthBeats, note.noteNumber);
                }
            }
        } else if (c->isAudio() && c->audio().source.filePath.isNotEmpty()) {
            const float x0 = beatToPixelF(cStart);
            const float x1 = beatToPixelF(cEnd);
            const auto area = juce::Rectangle<int>(
                juce::roundToInt(x0), 0, juce::jmax(1, juce::roundToInt(x1 - x0)), editorH);
            ClipWaveformSpec spec;
            spec.tempo = tempo;
            spec.colour = c->colour.withAlpha(0.3f);
            paintClipWaveform(g, *c, cid, area, cLenSeconds, spec);
        }
    }

    if (ghostNotes.empty())
        return;

    // Thin contour notes, not piano-roll rows: the ghost is for aligning the
    // curve to events in time, not for reading pitches, so notes stay a few
    // pixels tall and spread gently by relative pitch (centred, minimum
    // one-octave span so sparse material doesn't blow up dramatically).
    constexpr float kNoteH = 5.0f;
    const double midPitch = (minPitch + maxPitch) * 0.5;
    const double pitchSpan = juce::jmax(12.0, static_cast<double>(maxPitch - minPitch) + 6.0);
    const float bandTop = kNoteH;
    const float bandBottom = static_cast<float>(editorH) - kNoteH * 2.0f;
    for (const auto& n : ghostNotes) {
        const float x = beatToPixelF(n.beat);
        const float w = juce::jmax(2.0f, beatToPixelF(n.beat + n.length) - x);
        const double t = 0.5 - (n.pitch - midPitch) / pitchSpan;  // 0 top .. 1 bottom
        const float y = bandTop + static_cast<float>(t) * (bandBottom - bandTop);
        g.setColour(n.colour.withAlpha(0.35f));
        g.fillRoundedRectangle(juce::Rectangle<float>(x, y, w, kNoteH), 1.5f);
    }
}

void AutomationClipEditorContent::clipsChanged() {
    if (editor_ != nullptr)
        editor_->repaint();
}

void AutomationClipEditorContent::clipPropertyChanged(magda::ClipId clipId) {
    juce::ignoreUnused(clipId);
    if (editor_ != nullptr)
        editor_->repaint();
}

void AutomationClipEditorContent::updateTitle() {
    const auto* clip = getClip();
    if (clip == nullptr)
        return;
    juce::String title = clip->name;
    if (const auto* lane = magda::AutomationManager::getInstance().getLane(selection_.laneId))
        title = magda::getDisplayNameForTarget(lane->target) + "  -  " + clip->name;
    titleLabel_.setText(title, juce::dontSendNotification);
}

double AutomationClipEditorContent::gridResolutionBeats() const {
    const auto* clip = getClip();
    if (clip == nullptr)
        return 1.0;
    if (clip->gridAutoGrid) {
        // Auto: densest readable subdivision at the current zoom.
        constexpr int minPixelSpacing = 20;
        const double zoom = horizontalZoom_ > 0.0 ? horizontalZoom_ : kMinZoom;
        const double frac = magda::GridConstants::findBeatSubdivision(zoom, minPixelSpacing);
        return frac > 0.0 ? frac : 1.0;
    }
    return (4.0 * clip->gridNumerator) / static_cast<double>(clip->gridDenominator);
}

void AutomationClipEditorContent::gridSettingsChanged() {
    const auto* clip = getClip();
    if (clip == nullptr)
        return;
    const double res = gridResolutionBeats();
    timeRuler_->setGridResolution(res);
    timeRuler_->setSnapEnabled(clip->gridSnapEnabled);
    timeRuler_->repaint();
    if (editor_ != nullptr)
        editor_->repaint();
    if (clip->gridAutoGrid && onAutoGridDisplayChanged) {
        // Mirror the MIDI editor's display convention: auto grid reads 1/den.
        const int den = juce::jmax(1, static_cast<int>(std::round(4.0 / res)));
        onAutoGridDisplayChanged(1, den);
    }
}

void AutomationClipEditorContent::setGridSettingsFromUI(bool autoGrid, int numerator,
                                                        int denominator) {
    if (selection_.clipId == magda::INVALID_AUTOMATION_CLIP_ID)
        return;
    // Notifies clips-changed, which routes back through updateView().
    magda::AutomationManager::getInstance().setClipGridSettings(selection_.clipId, autoGrid,
                                                                numerator, denominator);
}

void AutomationClipEditorContent::setSnapEnabledFromUI(bool enabled) {
    if (selection_.clipId == magda::INVALID_AUTOMATION_CLIP_ID)
        return;
    magda::AutomationManager::getInstance().setClipSnapEnabled(selection_.clipId, enabled);
}

void AutomationClipEditorContent::refreshFromSelection() {
    auto& sm = magda::SelectionManager::getInstance();
    magda::AutomationClipSelection selection;
    if (sm.getSelectionType() == magda::SelectionType::AutomationClip) {
        selection = sm.getAutomationClipSelection();
    } else if (sm.getSelectionType() == magda::SelectionType::AutomationPoint) {
        // Point selection inside a clip: stay on that clip (touching the
        // curve selects points; the editor must not reset mid-gesture).
        const auto& pointSel = sm.getAutomationPointSelection();
        if (pointSel.clipId != magda::INVALID_AUTOMATION_CLIP_ID) {
            selection.clipId = pointSel.clipId;
            selection.laneId = pointSel.laneId;
        }
    }

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

    updateTitle();

    editor_ = std::make_unique<magda::AutomationCurveEditor>(selection_.laneId);
    editor_->setClipId(selection_.clipId);
    editor_->setShowClipBorders(true);
    editor_->setDrawMode(magda::AutomationDrawMode::Pencil);
    editor_->paintUnderlay = [this](juce::Graphics& g) { paintTrackGhost(g); };
    // Grid and snap come from the clip's own settings (header controls),
    // not the arrangement's — same model as the piano roll.
    editor_->snapBeatToGrid = [this](double beats) {
        const auto* clip = getClip();
        if (clip == nullptr || !clip->gridSnapEnabled)
            return beats;
        const double res = gridResolutionBeats();
        if (res <= 0.0)
            return beats;
        return std::round(beats / res) * res;
    };
    editor_->getGridSpacingBeats = [this]() { return gridResolutionBeats(); };
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
    }
    timeRuler_->setTempo(bpm);
    timeRuler_->setBarOrigin(0.0);
    timeRuler_->setZoom(horizontalZoom_);
    timeRuler_->setClipLength(lengthSeconds);
    timeRuler_->setTimeOffset(looped ? 0.0 : startSeconds);
    timeRuler_->setRelativeMode(looped);
    gridSettingsChanged();  // ruler grid + snap from the clip's settings
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
    // The editor spans the pads too, with the beat range inset by kEdgePad:
    // if it started at beat 0 exactly, points on the clip's first beat would
    // be clipped in half by the component edge.
    editor_->setBounds(0, 0, contentWidth + 2 * kEdgePad, height);
    editor_->setEdgeInsetPx(kEdgePad);
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
    gridSettingsChanged();  // auto grid follows zoom

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

void AutomationClipEditorContent::automationPointSelectionChanged(
    const magda::AutomationPointSelection& selection) {
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
    // Length / loop / position / name changes update the view and title.
    updateTitle();
    updateView();
    repaint();
    if (onClipStateChanged)
        onClipStateChanged();
}

}  // namespace magda::daw::ui
