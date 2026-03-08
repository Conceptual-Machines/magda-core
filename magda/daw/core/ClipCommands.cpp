#include "ClipCommands.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

#include "../audio/AudioBridge.hpp"
#include "../audio/DrumGridPlugin.hpp"
#include "../audio/InstrumentRackManager.hpp"
#include "../audio/MagdaSamplerPlugin.hpp"
#include "../engine/TracktionEngineWrapper.hpp"
#include "../project/ProjectManager.hpp"
#include "../ui/state/TimelineController.hpp"
#include "ClipOperations.hpp"
#include "Config.hpp"
#include "TrackManager.hpp"

namespace magda {

namespace te = tracktion;

namespace {

/// Expand a file naming pattern with token substitution.
/// Tokens: <clip-name>, <track-name>, <project-name>, <date-time>
juce::String expandPattern(juce::String pattern, const juce::String& clipName,
                           const juce::String& trackName) {
    juce::String safeClip =
        clipName.isNotEmpty() ? clipName.replaceCharacters(" /\\:", "____") : "clip";
    juce::String safeTrack =
        trackName.isNotEmpty() ? trackName.replaceCharacters(" /\\:", "____") : "track";

    juce::String projName = ProjectManager::getInstance().getProjectName();
    if (projName.isEmpty())
        projName = "untitled";
    projName = projName.replaceCharacters(" /\\:", "____");

    juce::String timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");

    pattern = pattern.replace("<clip-name>", safeClip);
    pattern = pattern.replace("<track-name>", safeTrack);
    pattern = pattern.replace("<project-name>", projName);
    pattern = pattern.replace("<date-time>", timestamp);

    return pattern.replaceCharacters("/\\:", "___");
}

/// Expand the render/export file naming pattern (default: <project-name>_<date-time>).
juce::String expandRenderPattern(const juce::String& clipName, const juce::String& trackName) {
    juce::String pattern(Config::getInstance().getRenderFilePattern());
    if (pattern.isEmpty())
        pattern = "<project-name>_<date-time>";
    return expandPattern(pattern, clipName, trackName);
}

/// Expand the bounce file naming pattern (default: <clip-name>_<date-time>).
juce::String expandBouncePattern(const juce::String& clipName, const juce::String& trackName) {
    juce::String pattern(Config::getInstance().getBounceFilePattern());
    if (pattern.isEmpty())
        pattern = "<clip-name>_<date-time>";
    return expandPattern(pattern, clipName, trackName);
}

/**
 * Progress window for offline rendering that runs on a background thread
 * while pumping the message loop (via runThread()) so the UI stays responsive.
 */
class RenderProgressWindow : public juce::ThreadWithProgressWindow {
  public:
    RenderProgressWindow(const juce::String& title, const te::Renderer::Parameters& params)
        : ThreadWithProgressWindow(title, true, true), params_(params) {
        setStatusMessage("Preparing to render...");
    }

    void run() override {
        std::atomic<float> progress{0.0f};
        auto renderTask =
            std::make_unique<te::Renderer::RenderTask>("Render", params_, &progress, nullptr);

        setStatusMessage("Rendering...");

        while (!threadShouldExit()) {
            auto status = renderTask->runJob();
            setProgress(static_cast<double>(progress.load()));

            if (status == juce::ThreadPoolJob::jobHasFinished) {
                success_ = params_.destFile.existsAsFile() && params_.destFile.getSize() > 0;
                setProgress(1.0);
                break;
            }

            if (status != juce::ThreadPoolJob::jobNeedsRunningAgain)
                break;

            juce::Thread::sleep(1);
        }
    }

    bool wasSuccessful() const {
        return success_;
    }

  private:
    te::Renderer::Parameters params_;
    bool success_ = false;
};

}  // namespace

// ============================================================================
// SplitClipCommand
// ============================================================================

SplitClipCommand::SplitClipCommand(ClipId clipId, double splitTime, double tempo)
    : clipId_(clipId), splitTime_(splitTime), tempo_(tempo) {}

bool SplitClipCommand::canExecute() const {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip && splitTime_ > clip->startTime && splitTime_ < clip->getEndTime();
}

ClipInfo SplitClipCommand::captureState() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip ? *clip : ClipInfo{};
}

void SplitClipCommand::restoreState(const ClipInfo& state) {
    auto& clipManager = ClipManager::getInstance();

    // Delete the right clip if it exists
    if (rightClipId_ != INVALID_CLIP_ID) {
        clipManager.deleteClip(rightClipId_);
        rightClipId_ = INVALID_CLIP_ID;
    }

    // Restore original clip completely
    if (auto* clip = clipManager.getClip(clipId_)) {
        *clip = state;  // Full restoration - no missing fields!
        clipManager.forceNotifyClipsChanged();
    }
}

void SplitClipCommand::performAction() {
    rightClipId_ = ClipManager::getInstance().splitClip(clipId_, splitTime_, tempo_);
}

bool SplitClipCommand::validateState() const {
    auto& clipManager = ClipManager::getInstance();

    // Validate left clip exists and has correct track
    auto* leftClip = clipManager.getClip(clipId_);
    if (!leftClip) {
        return false;
    }

    // Validate clip has a valid track
    if (leftClip->trackId == INVALID_TRACK_ID) {
        DBG("ERROR: Clip " << clipId_ << " has invalid track!");
        return false;
    }

    // If executed, validate right clip exists
    if (executed_ && rightClipId_ != INVALID_CLIP_ID) {
        auto* rightClip = clipManager.getClip(rightClipId_);
        if (!rightClip) {
            return false;
        }

        // Validate right clip has valid track
        if (rightClip->trackId == INVALID_TRACK_ID) {
            DBG("ERROR: Right clip " << rightClipId_ << " has invalid track!");
            return false;
        }

        // Validate clips are adjacent and continuous
        if (std::abs(leftClip->getEndTime() - rightClip->startTime) > 0.001) {
            return false;
        }
    }

    return true;
}

// ============================================================================
// MoveClipCommand
// ============================================================================

MoveClipCommand::MoveClipCommand(ClipId clipId, double newStartTime)
    : clipId_(clipId), newStartTime_(newStartTime) {}

void MoveClipCommand::execute() {
    auto& clipManager = ClipManager::getInstance();

    // Snapshot all arrangement clips before the move
    if (!executed_) {
        arrangementSnapshot_ = clipManager.getArrangementClips();
    }

    clipManager.moveClip(clipId_, newStartTime_);
    executed_ = true;
}

void MoveClipCommand::undo() {
    if (!executed_)
        return;

    auto& clipManager = ClipManager::getInstance();

    // Delete all current arrangement clips
    auto currentClips = clipManager.getArrangementClips();
    for (const auto& clip : currentClips) {
        clipManager.deleteClip(clip.id);
    }

    // Restore from snapshot
    for (const auto& clip : arrangementSnapshot_) {
        clipManager.restoreClip(clip);
    }

    clipManager.forceNotifyClipsChanged();
}

bool MoveClipCommand::canMergeWith(const UndoableCommand* other) const {
    auto* otherMove = dynamic_cast<const MoveClipCommand*>(other);
    return otherMove != nullptr && otherMove->clipId_ == clipId_;
}

void MoveClipCommand::mergeWith(const UndoableCommand* other) {
    auto* otherMove = dynamic_cast<const MoveClipCommand*>(other);
    if (otherMove) {
        // Keep our original snapshot, just update the target position
        newStartTime_ = otherMove->newStartTime_;
    }
}

// ============================================================================
// MoveSessionClipCommand
// ============================================================================

MoveSessionClipCommand::MoveSessionClipCommand(ClipId clipId, TrackId targetTrackId,
                                               int targetSceneIndex)
    : clipId_(clipId), targetTrackId_(targetTrackId), targetSceneIndex_(targetSceneIndex) {}

bool MoveSessionClipCommand::canExecute() const {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip && targetTrackId_ != INVALID_TRACK_ID && targetSceneIndex_ >= 0;
}

void MoveSessionClipCommand::execute() {
    if (!canExecute())
        return;

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip)
        return;

    if (!executed_) {
        originalTrackId_ = clip->trackId;
        originalSceneIndex_ = clip->sceneIndex;
    }

    if (clip->trackId != targetTrackId_)
        clipManager.moveClipToTrack(clipId_, targetTrackId_);

    clipManager.setClipSceneIndex(clipId_, targetSceneIndex_);
    executed_ = true;
}

void MoveSessionClipCommand::undo() {
    if (!executed_)
        return;

    auto& clipManager = ClipManager::getInstance();

    if (originalTrackId_ != targetTrackId_)
        clipManager.moveClipToTrack(clipId_, originalTrackId_);

    clipManager.setClipSceneIndex(clipId_, originalSceneIndex_);
}

// ============================================================================
// MoveClipToTrackCommand
// ============================================================================

MoveClipToTrackCommand::MoveClipToTrackCommand(ClipId clipId, TrackId newTrackId)
    : clipId_(clipId), newTrackId_(newTrackId) {}

bool MoveClipToTrackCommand::canExecute() const {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip && newTrackId_ != INVALID_TRACK_ID;
}

void MoveClipToTrackCommand::execute() {
    if (!canExecute())
        return;

    auto& clipManager = ClipManager::getInstance();

    if (!executed_) {
        arrangementSnapshot_ = clipManager.getArrangementClips();
    }

    clipManager.moveClipToTrack(clipId_, newTrackId_);
    executed_ = true;
}

void MoveClipToTrackCommand::undo() {
    if (!executed_)
        return;

    auto& clipManager = ClipManager::getInstance();

    auto currentClips = clipManager.getArrangementClips();
    for (const auto& clip : currentClips) {
        clipManager.deleteClip(clip.id);
    }

    for (const auto& clip : arrangementSnapshot_) {
        clipManager.restoreClip(clip);
    }

    clipManager.forceNotifyClipsChanged();
}

// ============================================================================
// ResizeClipCommand
// ============================================================================

ResizeClipCommand::ResizeClipCommand(ClipId clipId, double newLength, bool fromStart, double tempo)
    : clipId_(clipId), newLength_(newLength), fromStart_(fromStart), tempo_(tempo) {}

ClipInfo ResizeClipCommand::captureState() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip ? *clip : ClipInfo{};
}

void ResizeClipCommand::restoreState(const ClipInfo& state) {
    auto& clipManager = ClipManager::getInstance();
    if (auto* clip = clipManager.getClip(clipId_)) {
        *clip = state;
        clipManager.forceNotifyClipsChanged();
    }
}

void ResizeClipCommand::performAction() {
    ClipManager::getInstance().resizeClip(clipId_, newLength_, fromStart_, tempo_);
}

bool ResizeClipCommand::canMergeWith(const UndoableCommand* other) const {
    auto* otherResize = dynamic_cast<const ResizeClipCommand*>(other);
    return otherResize != nullptr && otherResize->clipId_ == clipId_ &&
           otherResize->fromStart_ == fromStart_;
}

void ResizeClipCommand::mergeWith(const UndoableCommand* other) {
    auto* otherResize = dynamic_cast<const ResizeClipCommand*>(other);
    if (otherResize) {
        // Update to their new length
        newLength_ = otherResize->newLength_;
    }
}

// ============================================================================
// DeleteClipCommand
// ============================================================================

DeleteClipCommand::DeleteClipCommand(ClipId clipId) : clipId_(clipId) {}

ClipInfo DeleteClipCommand::captureState() {
    auto* clip = ClipManager::getInstance().getClip(clipId_);
    return clip ? *clip : ClipInfo{};
}

void DeleteClipCommand::restoreState(const ClipInfo& state) {
    ClipManager::getInstance().restoreClip(state);
}

void DeleteClipCommand::performAction() {
    ClipManager::getInstance().deleteClip(clipId_);
}

bool DeleteClipCommand::validateState() const {
    // Deletion is always valid - the clip state is stored in the snapshot
    // No need to validate since restoreState() handles both cases (clip exists/doesn't exist)
    return true;
}

// ============================================================================
// CreateClipCommand
// ============================================================================

CreateClipCommand::CreateClipCommand(ClipType type, TrackId trackId, double startTime,
                                     double length, const juce::String& audioFilePath,
                                     ClipView view)
    : type_(type),
      trackId_(trackId),
      startTime_(startTime),
      length_(length),
      audioFilePath_(audioFilePath),
      view_(view) {}

bool CreateClipCommand::canExecute() const {
    return trackId_ != INVALID_TRACK_ID && length_ > 0.0;
}

void CreateClipCommand::execute() {
    if (!canExecute())
        return;

    auto& clipManager = ClipManager::getInstance();

    if (!executed_) {
        arrangementSnapshot_ = clipManager.getArrangementClips();
    }

    if (type_ == ClipType::Audio) {
        createdClipId_ =
            clipManager.createAudioClip(trackId_, startTime_, length_, audioFilePath_, view_);
    } else {
        createdClipId_ = clipManager.createMidiClip(trackId_, startTime_, length_, view_);
    }

    executed_ = true;
}

void CreateClipCommand::undo() {
    if (!executed_)
        return;

    auto& clipManager = ClipManager::getInstance();

    auto currentClips = clipManager.getArrangementClips();
    for (const auto& clip : currentClips) {
        clipManager.deleteClip(clip.id);
    }

    for (const auto& clip : arrangementSnapshot_) {
        clipManager.restoreClip(clip);
    }

    createdClipId_ = INVALID_CLIP_ID;
    clipManager.forceNotifyClipsChanged();
}

// ============================================================================
// DuplicateClipCommand
// ============================================================================

DuplicateClipCommand::DuplicateClipCommand(ClipId sourceClipId, double startTime,
                                           TrackId targetTrackId, double tempo)
    : sourceClipId_(sourceClipId),
      startTime_(startTime),
      targetTrackId_(targetTrackId),
      tempo_(tempo) {}

bool DuplicateClipCommand::canExecute() const {
    return ClipManager::getInstance().getClip(sourceClipId_) != nullptr;
}

void DuplicateClipCommand::execute() {
    if (!canExecute())
        return;

    auto& clipManager = ClipManager::getInstance();

    if (!executed_) {
        arrangementSnapshot_ = clipManager.getArrangementClips();
    }

    if (startTime_ < 0) {
        duplicatedClipId_ = clipManager.duplicateClip(sourceClipId_);
    } else {
        duplicatedClipId_ =
            clipManager.duplicateClipAt(sourceClipId_, startTime_, targetTrackId_, tempo_);
    }

    executed_ = true;
}

void DuplicateClipCommand::undo() {
    if (!executed_)
        return;

    auto& clipManager = ClipManager::getInstance();

    auto currentClips = clipManager.getArrangementClips();
    for (const auto& clip : currentClips) {
        clipManager.deleteClip(clip.id);
    }

    for (const auto& clip : arrangementSnapshot_) {
        clipManager.restoreClip(clip);
    }

    duplicatedClipId_ = INVALID_CLIP_ID;
    clipManager.forceNotifyClipsChanged();
}

// ============================================================================
// PasteClipCommand Implementation
// ============================================================================

PasteClipCommand::PasteClipCommand(double pasteTime, TrackId targetTrackId, ClipView targetView,
                                   int targetSceneIndex)
    : pasteTime_(pasteTime),
      targetTrackId_(targetTrackId),
      targetView_(targetView),
      targetSceneIndex_(targetSceneIndex) {}

bool PasteClipCommand::canExecute() const {
    return ClipManager::getInstance().hasClipsInClipboard();
}

void PasteClipCommand::execute() {
    if (!canExecute())
        return;

    auto& clipManager = ClipManager::getInstance();

    if (!executed_) {
        arrangementSnapshot_ = clipManager.getArrangementClips();
        sessionSnapshot_ = clipManager.getSessionClips();
    }

    pastedClipIds_ =
        clipManager.pasteFromClipboard(pasteTime_, targetTrackId_, targetView_, targetSceneIndex_);
    executed_ = true;
}

void PasteClipCommand::undo() {
    if (!executed_)
        return;

    auto& clipManager = ClipManager::getInstance();

    // Restore arrangement clips
    auto currentArrangement = clipManager.getArrangementClips();
    for (const auto& clip : currentArrangement) {
        clipManager.deleteClip(clip.id);
    }
    for (const auto& clip : arrangementSnapshot_) {
        clipManager.restoreClip(clip);
    }

    // Restore session clips
    auto currentSession = clipManager.getSessionClips();
    for (const auto& clip : currentSession) {
        clipManager.deleteClip(clip.id);
    }
    for (const auto& clip : sessionSnapshot_) {
        clipManager.restoreClip(clip);
    }

    pastedClipIds_.clear();
    clipManager.forceNotifyClipsChanged();
}

// ============================================================================
// JoinClipsCommand
// ============================================================================

JoinClipsCommand::JoinClipsCommand(ClipId leftClipId, ClipId rightClipId, double tempo)
    : leftClipId_(leftClipId), rightClipId_(rightClipId), tempo_(tempo) {}

bool JoinClipsCommand::canExecute() const {
    auto& clipManager = ClipManager::getInstance();
    auto* left = clipManager.getClip(leftClipId_);
    auto* right = clipManager.getClip(rightClipId_);

    if (!left || !right)
        return false;

    // Must be same track, same type
    if (left->trackId != right->trackId)
        return false;
    if (left->type != right->type)
        return false;

    // Must be adjacent (left ends where right starts)
    if (std::abs(left->getEndTime() - right->startTime) > 0.001)
        return false;

    return true;
}

JoinClipsState JoinClipsCommand::captureState() {
    auto& clipManager = ClipManager::getInstance();
    JoinClipsState state;

    auto* left = clipManager.getClip(leftClipId_);
    auto* right = clipManager.getClip(rightClipId_);

    state.leftClip = left ? *left : ClipInfo{};
    state.rightClip = right ? *right : ClipInfo{};

    return state;
}

void JoinClipsCommand::restoreState(const JoinClipsState& state) {
    auto& clipManager = ClipManager::getInstance();

    // Restore left clip from snapshot
    if (auto* left = clipManager.getClip(leftClipId_)) {
        *left = state.leftClip;
    }

    // Restore right clip (may have been deleted)
    if (!clipManager.getClip(rightClipId_)) {
        clipManager.restoreClip(state.rightClip);
    } else {
        *clipManager.getClip(rightClipId_) = state.rightClip;
    }

    clipManager.forceNotifyClipsChanged();
}

void JoinClipsCommand::performAction() {
    auto& clipManager = ClipManager::getInstance();
    auto* left = clipManager.getClip(leftClipId_);
    auto* right = clipManager.getClip(rightClipId_);

    if (!left || !right)
        return;

    if (left->type == ClipType::MIDI) {
        // MIDI join: copy right clip's notes into left, adjusting beat positions
        const double beatsPerSecond = tempo_ / 60.0;
        double beatOffset = (right->startTime - left->startTime) * beatsPerSecond;

        for (const auto& note : right->midiNotes) {
            MidiNote adjustedNote = note;
            adjustedNote.startBeat += beatOffset;
            left->midiNotes.push_back(adjustedNote);
        }
    } else if (left->type == ClipType::Audio) {
        // Audio join: extend left clip length to cover both clips
        // (offset and speedRatio remain from left clip)
    }

    // Extend left clip length
    left->length += right->length;

    // Delete right clip
    clipManager.deleteClip(rightClipId_);
}

bool JoinClipsCommand::validateState() const {
    auto& clipManager = ClipManager::getInstance();

    auto* left = clipManager.getClip(leftClipId_);
    if (!left)
        return false;

    if (left->trackId == INVALID_TRACK_ID)
        return false;

    return true;
}

// ============================================================================
// StretchClipCommand
// ============================================================================

StretchClipCommand::StretchClipCommand(ClipId clipId, const ClipInfo& beforeState)
    : clipId_(clipId), beforeState_(beforeState) {}

void StretchClipCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip)
        return;

    if (afterState_.id == INVALID_CLIP_ID) {
        // First execution: clip is already in final state from drag updates.
        // Just capture it for redo.
        afterState_ = *clip;
    } else {
        // Redo: restore the after-state
        *clip = afterState_;
        clipManager.forceNotifyClipsChanged();
    }
}

void StretchClipCommand::undo() {
    auto& clipManager = ClipManager::getInstance();
    if (auto* clip = clipManager.getClip(clipId_)) {
        *clip = beforeState_;
        clipManager.forceNotifyClipsChanged();
    }
}

// ============================================================================
// SetFadeCommand
// ============================================================================

SetFadeCommand::SetFadeCommand(ClipId clipId, const ClipInfo& beforeState)
    : clipId_(clipId), beforeState_(beforeState) {}

void SetFadeCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip)
        return;

    if (afterState_.id == INVALID_CLIP_ID) {
        // First execution: clip is already in final state from drag updates.
        // Just capture it for redo.
        afterState_ = *clip;
    } else {
        // Redo: restore the after-state
        *clip = afterState_;
        clipManager.forceNotifyClipsChanged();
    }
}

void SetFadeCommand::undo() {
    auto& clipManager = ClipManager::getInstance();
    if (auto* clip = clipManager.getClip(clipId_)) {
        *clip = beforeState_;
        clipManager.forceNotifyClipsChanged();
    }
}

// ============================================================================
// SetVolumeCommand
// ============================================================================

SetVolumeCommand::SetVolumeCommand(ClipId clipId, const ClipInfo& beforeState)
    : clipId_(clipId), beforeState_(beforeState) {}

void SetVolumeCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip)
        return;

    if (afterState_.id == INVALID_CLIP_ID) {
        // First execution: clip is already in final state from drag updates.
        // Just capture it for redo.
        afterState_ = *clip;
    } else {
        // Redo: restore the after-state
        *clip = afterState_;
        clipManager.forceNotifyClipsChanged();
    }
}

void SetVolumeCommand::undo() {
    auto& clipManager = ClipManager::getInstance();
    if (auto* clip = clipManager.getClip(clipId_)) {
        *clip = beforeState_;
        clipManager.forceNotifyClipsChanged();
    }
}

// ============================================================================
// RenderClipCommand
// ============================================================================

RenderClipCommand::RenderClipCommand(ClipId clipId, TracktionEngineWrapper* engine)
    : clipId_(clipId), engine_(engine) {}

void RenderClipCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || clip->type != ClipType::Audio || !engine_) {
        DBG("RenderClipCommand: invalid clip or engine");
        return;
    }

    // Snapshot original clip for undo
    originalClipSnapshot_ = *clip;

    auto* edit = engine_->getEdit();
    auto* bridge = engine_->getAudioBridge();
    if (!edit || !bridge) {
        DBG("RenderClipCommand: no edit or bridge");
        return;
    }

    // Find the TE clip
    auto* teClip = bridge->getArrangementTeClip(clipId_);
    if (!teClip) {
        DBG("RenderClipCommand: TE clip not found");
        return;
    }

    // Determine output file path
    juce::File sourceFile(clip->audioFilePath);
    auto configFolder = Config::getInstance().getRenderFolder();
    juce::File rendersDir;
    if (!configFolder.empty()) {
        rendersDir = juce::File(configFolder);
    } else {
        auto projRendersDir = ProjectManager::getInstance().getRendersDirectory();
        rendersDir = projRendersDir != juce::File()
                         ? projRendersDir
                         : sourceFile.getParentDirectory().getChildFile("renders");
    }
    rendersDir.createDirectory();

    auto* trackInfo = TrackManager::getInstance().getTrack(clip->trackId);
    juce::String trackName = trackInfo ? trackInfo->name : "Track";
    juce::String clipName =
        clip->name.isNotEmpty() ? clip->name : sourceFile.getFileNameWithoutExtension();
    renderedFile_ = rendersDir.getChildFile(expandRenderPattern(clipName, trackName) + ".wav");

    // Stop transport and free playback context for offline rendering
    auto& transport = edit->getTransport();
    bool wasPlaying = transport.isPlaying();
    if (wasPlaying) {
        transport.stop(false, false);
    }
    te::freePlaybackContextIfNotRecording(transport);
    // Restore playback after render if it was playing
    auto restoreTransport = [wasPlaying, &transport]() {
        if (wasPlaying)
            transport.play(false);
    };

    // Find track index in getAllTracks for tracksToDo bitset
    auto* teTrack = teClip->getTrack();
    if (!teTrack) {
        DBG("RenderClipCommand: clip has no track");
        restoreTransport();
        return;
    }

    auto allTracks = te::getAllTracks(*edit);
    int trackIndex = -1;
    for (int i = 0; i < allTracks.size(); ++i) {
        if (allTracks[i] == teTrack) {
            trackIndex = i;
            break;
        }
    }

    if (trackIndex < 0) {
        DBG("RenderClipCommand: track not found in edit");
        restoreTransport();
        return;
    }

    // Build Renderer::Parameters
    te::Renderer::Parameters params(*edit);
    params.destFile = renderedFile_;

    auto& formatManager = engine_->getEngine()->getAudioFileFormatManager();
    params.audioFormat = formatManager.getWavFormat();
    params.bitDepth = Config::getInstance().getRenderBitDepth();
    params.sampleRateForAudio = Config::getInstance().getRenderSampleRate();
    params.blockSizeForAudio = 512;
    params.usePlugins = false;
    params.useMasterPlugins = false;
    params.checkNodesForAudio = false;

    // Set time range to clip's timeline range
    params.time = te::TimeRange(te::TimePosition::fromSeconds(clip->startTime),
                                te::TimePosition::fromSeconds(clip->startTime + clip->length));

    // Set track and clip filters
    juce::BigInteger trackBits;
    trackBits.setBit(trackIndex);
    params.tracksToDo = trackBits;
    params.allowedClips.add(teClip);

    // Run render on background thread with progress UI
    RenderProgressWindow progressWindow("Rendering Clip...", params);
    bool userCancelled = !progressWindow.runThread();

    if (userCancelled || !progressWindow.wasSuccessful()) {
        if (!userCancelled)
            DBG("RenderClipCommand: render failed, no output file");
        if (renderedFile_.existsAsFile())
            renderedFile_.deleteFile();
        restoreTransport();
        return;
    }

    // Delete original clip and create new clean clip at same position
    double startTime = clip->startTime;
    double length = clip->length;
    TrackId trackId = clip->trackId;
    juce::Colour colour = clip->colour;
    juce::String name = clip->name;

    clipManager.deleteClip(clipId_);

    newClipId_ =
        clipManager.createAudioClip(trackId, startTime, length, renderedFile_.getFullPathName());

    // Copy over visual properties to the new clip
    if (auto* newClip = clipManager.getClip(newClipId_)) {
        newClip->colour = colour;
        newClip->name = name.isNotEmpty() ? name : renderedFile_.getFileNameWithoutExtension();
        clipManager.forceNotifyClipsChanged();
    }

    restoreTransport();
    success_ = true;
}

void RenderClipCommand::undo() {
    if (!success_)
        return;

    auto& clipManager = ClipManager::getInstance();

    // Delete the replacement clip
    if (newClipId_ != INVALID_CLIP_ID) {
        clipManager.deleteClip(newClipId_);
        newClipId_ = INVALID_CLIP_ID;
    }

    // Restore original clip from snapshot
    clipManager.restoreClip(originalClipSnapshot_);

    // Delete the rendered file
    if (renderedFile_.existsAsFile()) {
        renderedFile_.deleteFile();
    }

    success_ = false;
}

// ============================================================================
// RenderTimeSelectionCommand
// ============================================================================

RenderTimeSelectionCommand::RenderTimeSelectionCommand(double startTime, double endTime,
                                                       const std::vector<TrackId>& trackIds,
                                                       TracktionEngineWrapper* engine)
    : startTime_(startTime), endTime_(endTime), trackIds_(trackIds), engine_(engine) {}

void RenderTimeSelectionCommand::execute() {
    if (!engine_ || startTime_ >= endTime_ || trackIds_.empty()) {
        DBG("RenderTimeSelectionCommand: invalid inputs");
        return;
    }

    auto* edit = engine_->getEdit();
    auto* bridge = engine_->getAudioBridge();
    if (!edit || !bridge) {
        DBG("RenderTimeSelectionCommand: no edit or bridge");
        return;
    }

    auto& clipManager = ClipManager::getInstance();

    // Stop transport and free playback context for offline rendering
    auto& transport = edit->getTransport();
    bool wasPlaying = transport.isPlaying();
    if (wasPlaying) {
        transport.stop(false, false);
    }
    te::freePlaybackContextIfNotRecording(transport);

    auto allTracks = te::getAllTracks(*edit);

    trackStates_.clear();
    newClipIds_.clear();

    int trackIndex_ = 0;
    for (auto trackId : trackIds_) {
        ++trackIndex_;
        // Get overlapping clips on this track
        auto overlappingIds = clipManager.getClipsInRange(trackId, startTime_, endTime_);
        if (overlappingIds.empty())
            continue;

        // Check all overlapping clips are audio
        bool allAudio = true;
        for (auto cid : overlappingIds) {
            auto* c = clipManager.getClip(cid);
            if (!c || c->type != ClipType::Audio) {
                allAudio = false;
                break;
            }
        }
        if (!allAudio)
            continue;

        // Snapshot all overlapping clips for undo
        RenderTrackState trackState;
        trackState.trackId = trackId;
        for (auto cid : overlappingIds) {
            auto* c = clipManager.getClip(cid);
            if (c)
                trackState.originalClips.push_back(*c);
        }

        // Determine output file path from first overlapping clip's source
        auto* firstClip = clipManager.getClip(overlappingIds[0]);
        juce::File sourceFile(firstClip->audioFilePath);
        auto configFolder = Config::getInstance().getRenderFolder();
        juce::File rendersDir;
        if (!configFolder.empty()) {
            rendersDir = juce::File(configFolder);
        } else {
            auto projRendersDir = ProjectManager::getInstance().getRendersDirectory();
            rendersDir = projRendersDir != juce::File()
                             ? projRendersDir
                             : sourceFile.getParentDirectory().getChildFile("renders");
        }
        rendersDir.createDirectory();

        auto* trackInfo = TrackManager::getInstance().getTrack(trackId);
        juce::String trackName = trackInfo ? trackInfo->name : "Track";
        auto* firstClipInfo = clipManager.getClip(overlappingIds[0]);
        juce::String clipName = firstClipInfo ? firstClipInfo->name : trackName;
        trackState.renderedFile =
            rendersDir.getChildFile(expandRenderPattern(clipName, trackName) + ".wav");

        // Resolve TE track index
        auto* teTrack = bridge->getAudioTrack(trackId);
        if (!teTrack) {
            DBG("RenderTimeSelectionCommand: TE track not found for trackId " << trackId);
            continue;
        }

        int trackIndex = -1;
        for (int i = 0; i < allTracks.size(); ++i) {
            if (allTracks[i] == teTrack) {
                trackIndex = i;
                break;
            }
        }
        if (trackIndex < 0)
            continue;

        // Build Renderer::Parameters
        te::Renderer::Parameters params(*edit);
        params.destFile = trackState.renderedFile;

        auto& formatManager = engine_->getEngine()->getAudioFileFormatManager();
        params.audioFormat = formatManager.getWavFormat();
        params.bitDepth = Config::getInstance().getRenderBitDepth();
        params.sampleRateForAudio = Config::getInstance().getRenderSampleRate();
        params.blockSizeForAudio = 512;
        params.usePlugins = false;
        params.useMasterPlugins = false;
        params.checkNodesForAudio = false;

        params.time = te::TimeRange(te::TimePosition::fromSeconds(startTime_),
                                    te::TimePosition::fromSeconds(endTime_));

        juce::BigInteger trackBits;
        trackBits.setBit(trackIndex);
        params.tracksToDo = trackBits;
        // allowedClips empty = all clips on track in range

        // Run render on background thread with progress UI
        int trackTotal = static_cast<int>(trackIds_.size());
        juce::String title = "Rendering track " + juce::String(trackIndex_) + " of " +
                             juce::String(trackTotal) + "...";
        RenderProgressWindow progressWindow(title, params);
        bool userCancelled = !progressWindow.runThread();

        if (userCancelled || !progressWindow.wasSuccessful()) {
            if (!userCancelled)
                DBG("RenderTimeSelectionCommand: render failed for track " << trackId);
            if (trackState.renderedFile.existsAsFile())
                trackState.renderedFile.deleteFile();
            if (userCancelled)
                break;
            continue;
        }

        // Delete all overlapping clips
        for (auto cid : overlappingIds) {
            clipManager.deleteClip(cid);
        }

        // Create new clean clip at selection start with selection length
        double newLength = endTime_ - startTime_;
        juce::Colour colour = trackState.originalClips[0].colour;

        trackState.newClipId = clipManager.createAudioClip(
            trackId, startTime_, newLength, trackState.renderedFile.getFullPathName());

        if (auto* newClip = clipManager.getClip(trackState.newClipId)) {
            newClip->colour = colour;
            newClip->name = trackName + " (rendered)";
        }

        newClipIds_.push_back(trackState.newClipId);
        trackStates_.push_back(std::move(trackState));
    }

    if (!trackStates_.empty()) {
        clipManager.forceNotifyClipsChanged();
        success_ = true;
    }

    if (wasPlaying)
        transport.play(false);
}

void RenderTimeSelectionCommand::undo() {
    if (!success_)
        return;

    auto& clipManager = ClipManager::getInstance();

    for (auto& trackState : trackStates_) {
        // Delete the new rendered clip
        if (trackState.newClipId != INVALID_CLIP_ID) {
            clipManager.deleteClip(trackState.newClipId);
            trackState.newClipId = INVALID_CLIP_ID;
        }

        // Restore all original clips
        for (const auto& originalClip : trackState.originalClips) {
            clipManager.restoreClip(originalClip);
        }

        // Delete rendered file
        if (trackState.renderedFile.existsAsFile()) {
            trackState.renderedFile.deleteFile();
        }
    }

    clipManager.forceNotifyClipsChanged();
    newClipIds_.clear();
    success_ = false;
}

// ============================================================================
// Helper: trim a looped clip's boundaries without splitting/deleting notes.
// For looped clips, time selection operations just adjust the container
// (startTime, length, midiOffset) — the notes repeat and don't need modification.
// Returns true if the clip was handled as a looped clip.
static bool trimLoopedClip(ClipManager& clipManager, const ClipInfo& clip, double selStart,
                           double selEnd, bool ripple, double duration,
                           std::vector<ClipId>& clipsToDelete) {
    if (!clip.loopEnabled)
        return false;

    auto* liveClip = clipManager.getClip(clip.id);
    if (!liveClip)
        return false;

    double clipEnd = clip.startTime + clip.length;
    bool startsBeforeSel = clip.startTime < selStart;
    bool endsAfterSel = clipEnd > selEnd;

    if (startsBeforeSel && endsAfterSel) {
        if (!ripple) {
            // Non-ripple: need two clips with a gap — fall through to normal split logic
            return false;
        }
        // Ripple: reduce length by duration, gap gets closed
        liveClip->length -= duration;
    } else if (startsBeforeSel) {
        // Spans left boundary: trim right edge
        liveClip->length = selStart - clip.startTime;
    } else if (endsAfterSel) {
        // Spans right boundary: trim left edge, adjust phase
        double trimAmount = selEnd - clip.startTime;
        liveClip->startTime = ripple ? selStart : selEnd;
        liveClip->length -= trimAmount;

        // Adjust midiOffset (phase) for the trimmed portion
        if (clip.type == ClipType::MIDI && clip.loopLength > 0.0) {
            double bpm = 120.0;
            if (auto* controller = magda::TimelineController::getCurrent()) {
                bpm = controller->getState().tempo.bpm;
            }
            double trimBeats = trimAmount * bpm / 60.0;
            double loopLengthBeats = clip.loopLength * bpm / 60.0;
            if (loopLengthBeats > 0.0) {
                double newPhase = std::fmod(clip.midiOffset + trimBeats, loopLengthBeats);
                if (newPhase < 0.0)
                    newPhase += loopLengthBeats;
                liveClip->midiOffset = newPhase;
            }
        }
    } else {
        // Fully inside: delete
        clipsToDelete.push_back(clip.id);
    }

    return true;
}

// ============================================================================
// RippleDeleteTimeSelectionCommand
// ============================================================================

RippleDeleteTimeSelectionCommand::RippleDeleteTimeSelectionCommand(
    double startTime, double endTime, const std::vector<TrackId>& trackIds, double tempo)
    : startTime_(startTime), endTime_(endTime), trackIds_(trackIds), tempo_(tempo) {}

void RippleDeleteTimeSelectionCommand::execute() {
    auto& clipManager = ClipManager::getInstance();

    // Snapshot all arrangement clips for reliable undo
    snapshot_ = clipManager.getArrangementClips();

    double duration = endTime_ - startTime_;
    if (duration <= 0.0)
        return;

    // Helper: check if a clip's track is affected
    auto isAffectedTrack = [this](TrackId trackId) {
        if (trackIds_.empty())
            return true;
        return std::find(trackIds_.begin(), trackIds_.end(), trackId) != trackIds_.end();
    };

    // Collect clips to delete (fully inside selection)
    std::vector<ClipId> clipsToDelete;

    // Process overlapping clips on affected tracks
    // We need to work on a copy of clip IDs since we'll be modifying clips
    auto allClips = clipManager.getArrangementClips();
    for (const auto& clip : allClips) {
        if (!isAffectedTrack(clip.trackId))
            continue;

        double clipEnd = clip.startTime + clip.length;

        // No overlap
        if (clip.startTime >= endTime_ || clipEnd <= startTime_)
            continue;

        // Looped clips: just adjust boundaries, don't split/delete notes
        if (trimLoopedClip(clipManager, clip, startTime_, endTime_, true, duration, clipsToDelete))
            continue;

        bool startsBeforeSel = clip.startTime < startTime_;
        bool endsAfterSel = clipEnd > endTime_;

        if (startsBeforeSel && endsAfterSel) {
            // Clip spans both boundaries: split at startTime_, split again at endTime_,
            // delete the middle piece. This preserves both the left and right portions.
            ClipId rightId = clipManager.splitClip(clip.id, startTime_, tempo_);
            if (rightId != INVALID_CLIP_ID) {
                // Split the right portion at endTime_ to isolate the middle
                ClipId tailId = clipManager.splitClip(rightId, endTime_, tempo_);
                // Delete the middle piece (between startTime_ and endTime_)
                clipsToDelete.push_back(rightId);
                // The tail (after endTime_) needs to be shifted left by duration
                if (tailId != INVALID_CLIP_ID) {
                    auto* tailClip = clipManager.getClip(tailId);
                    if (tailClip)
                        tailClip->startTime = startTime_;  // Shift left to fill gap
                }
            }
        } else if (startsBeforeSel) {
            // Clip spans left boundary only: trim right edge to startTime_
            double newLength = startTime_ - clip.startTime;
            auto* liveClip = clipManager.getClip(clip.id);
            if (liveClip)
                liveClip->length = newLength;
        } else if (endsAfterSel) {
            // Clip spans right boundary only: split at endTime_, shift right portion left
            ClipId tailId = clipManager.splitClip(clip.id, endTime_, tempo_);
            // Delete the left portion (starts inside selection)
            clipsToDelete.push_back(clip.id);
            // Shift the tail left to fill the gap
            if (tailId != INVALID_CLIP_ID) {
                auto* tailClip = clipManager.getClip(tailId);
                if (tailClip)
                    tailClip->startTime = startTime_;
            }
        } else {
            // Fully inside selection: delete
            clipsToDelete.push_back(clip.id);
        }
    }

    // Delete fully-inside clips
    for (auto clipId : clipsToDelete) {
        clipManager.deleteClip(clipId);
    }

    // Shift all clips on affected tracks that start at or after endTime_ left by duration
    for (auto& clip : clipManager.getArrangementClips()) {
        if (!isAffectedTrack(clip.trackId))
            continue;

        // Use non-const access
        auto* liveClip = clipManager.getClip(clip.id);
        if (liveClip && liveClip->startTime >= endTime_) {
            liveClip->startTime -= duration;
        }
    }

    clipManager.forceNotifyClipsChanged();
    executed_ = true;
}

void RippleDeleteTimeSelectionCommand::undo() {
    if (!executed_)
        return;

    auto& clipManager = ClipManager::getInstance();

    // Delete all current arrangement clips
    auto currentClips = clipManager.getArrangementClips();
    for (const auto& clip : currentClips) {
        clipManager.deleteClip(clip.id);
    }

    // Restore from snapshot
    for (const auto& clip : snapshot_) {
        clipManager.restoreClip(clip);
    }

    clipManager.forceNotifyClipsChanged();
    executed_ = false;
}

// ============================================================================
// DeleteTimeSelectionCommand (no ripple)
// ============================================================================

DeleteTimeSelectionCommand::DeleteTimeSelectionCommand(double startTime, double endTime,
                                                       const std::vector<TrackId>& trackIds,
                                                       double tempo)
    : startTime_(startTime), endTime_(endTime), trackIds_(trackIds), tempo_(tempo) {}

void DeleteTimeSelectionCommand::execute() {
    auto& clipManager = ClipManager::getInstance();

    // Snapshot all arrangement clips for reliable undo
    snapshot_ = clipManager.getArrangementClips();

    double duration = endTime_ - startTime_;
    if (duration <= 0.0)
        return;

    auto isAffectedTrack = [this](TrackId trackId) {
        if (trackIds_.empty())
            return true;
        return std::find(trackIds_.begin(), trackIds_.end(), trackId) != trackIds_.end();
    };

    std::vector<ClipId> clipsToDelete;

    auto allClips = clipManager.getArrangementClips();
    for (const auto& clip : allClips) {
        if (!isAffectedTrack(clip.trackId))
            continue;

        double clipEnd = clip.startTime + clip.length;

        // No overlap
        if (clip.startTime >= endTime_ || clipEnd <= startTime_)
            continue;

        // Looped clips: just adjust boundaries, don't split/delete notes
        if (trimLoopedClip(clipManager, clip, startTime_, endTime_, false, duration, clipsToDelete))
            continue;

        bool startsBeforeSel = clip.startTime < startTime_;
        bool endsAfterSel = clipEnd > endTime_;

        if (startsBeforeSel && endsAfterSel) {
            // Clip spans both boundaries: split at startTime_, split again at endTime_,
            // delete the middle piece. Preserves both left and right portions.
            ClipId rightId = clipManager.splitClip(clip.id, startTime_, tempo_);
            if (rightId != INVALID_CLIP_ID) {
                ClipId tailId = clipManager.splitClip(rightId, endTime_, tempo_);
                clipsToDelete.push_back(rightId);
                // tailId stays at endTime_ (no shift — non-ripple)
                juce::ignoreUnused(tailId);
            }
        } else if (startsBeforeSel) {
            // Clip spans left boundary: trim right edge to startTime_
            double newLength = startTime_ - clip.startTime;
            auto* liveClip = clipManager.getClip(clip.id);
            if (liveClip)
                liveClip->length = newLength;
        } else if (endsAfterSel) {
            // Clip spans right boundary: split at endTime_, delete left portion
            ClipId tailId = clipManager.splitClip(clip.id, endTime_, tempo_);
            clipsToDelete.push_back(clip.id);
            // tailId stays at endTime_ (no shift — non-ripple)
            juce::ignoreUnused(tailId);
        } else {
            // Fully inside selection: delete
            clipsToDelete.push_back(clip.id);
        }
    }

    for (auto clipId : clipsToDelete) {
        clipManager.deleteClip(clipId);
    }

    // No ripple shift — clips after selection stay where they are

    clipManager.forceNotifyClipsChanged();
    executed_ = true;
}

void DeleteTimeSelectionCommand::undo() {
    if (!executed_)
        return;

    auto& clipManager = ClipManager::getInstance();

    auto currentClips = clipManager.getArrangementClips();
    for (const auto& clip : currentClips) {
        clipManager.deleteClip(clip.id);
    }

    for (const auto& clip : snapshot_) {
        clipManager.restoreClip(clip);
    }

    clipManager.forceNotifyClipsChanged();
    executed_ = false;
}

// ============================================================================
// BounceInPlaceCommand
// ============================================================================

BounceInPlaceCommand::BounceInPlaceCommand(ClipId clipId, TracktionEngineWrapper* engine)
    : clipId_(clipId), engine_(engine) {}

void BounceInPlaceCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || clip->type != ClipType::MIDI || !engine_) {
        DBG("BounceInPlaceCommand: invalid clip (must be MIDI) or engine");
        return;
    }

    // Must be on a track with an instrument
    auto* trackInfo = TrackManager::getInstance().getTrack(clip->trackId);
    if (!trackInfo || !trackInfo->hasInstrument()) {
        DBG("BounceInPlaceCommand: clip must be on a track with an instrument");
        return;
    }

    // Snapshot original clip for undo
    originalClipSnapshot_ = *clip;

    auto* edit = engine_->getEdit();
    auto* bridge = engine_->getAudioBridge();
    if (!edit || !bridge) {
        DBG("BounceInPlaceCommand: no edit or bridge");
        return;
    }

    // Find the TE clip
    auto* teClip = bridge->getArrangementTeClip(clipId_);
    if (!teClip) {
        DBG("BounceInPlaceCommand: TE clip not found");
        return;
    }

    // Determine output file path
    auto configFolder = Config::getInstance().getRenderFolder();
    juce::File bouncesDir;
    if (!configFolder.empty()) {
        bouncesDir = juce::File(configFolder);
    } else {
        auto projBouncesDir = ProjectManager::getInstance().getBouncesDirectory();
        bouncesDir = projBouncesDir != juce::File()
                         ? projBouncesDir
                         : edit->editFileRetriever().getParentDirectory().getChildFile("bounces");
    }
    bouncesDir.createDirectory();

    {
        auto* trackInfo = TrackManager::getInstance().getTrack(clip->trackId);
        juce::String trackName = trackInfo ? trackInfo->name : "Track";
        juce::String clipName = clip->name.isNotEmpty() ? clip->name : "clip";
        renderedFile_ = bouncesDir.getChildFile(expandBouncePattern(clipName, trackName) + ".wav");
    }

    // Stop transport and free playback context
    auto& transport = edit->getTransport();
    bool wasPlaying = transport.isPlaying();
    if (wasPlaying) {
        transport.stop(false, false);
    }
    te::freePlaybackContextIfNotRecording(transport);

    auto restoreTransport = [wasPlaying, &transport]() {
        if (wasPlaying)
            transport.play(false);
    };

    // Find TE track
    auto* teTrack = teClip->getTrack();
    if (!teTrack) {
        DBG("BounceInPlaceCommand: clip has no track");
        restoreTransport();
        return;
    }

    // Bypass FX plugins (everything that isn't the instrument wrapper rack)
    auto& rackManager = bridge->getPluginManager().getInstrumentRackManager();
    struct PluginState {
        te::Plugin* plugin;
        bool wasEnabled;
    };
    std::vector<PluginState> savedStates;

    for (auto plugin : teTrack->pluginList) {
        if (!rackManager.isWrapperRack(plugin)) {
            savedStates.push_back({plugin, plugin->isEnabled()});
            plugin->setEnabled(false);
        }
    }

    // Find track index for tracksToDo bitset
    auto allTracks = te::getAllTracks(*edit);
    int trackIndex = -1;
    for (int i = 0; i < allTracks.size(); ++i) {
        if (allTracks[i] == teTrack) {
            trackIndex = i;
            break;
        }
    }

    if (trackIndex < 0) {
        DBG("BounceInPlaceCommand: track not found in edit");
        // Restore bypassed plugins
        for (auto& state : savedStates) {
            state.plugin->setEnabled(state.wasEnabled);
        }
        restoreTransport();
        return;
    }

    // Build Renderer::Parameters
    te::Renderer::Parameters params(*edit);
    params.destFile = renderedFile_;
    auto& formatManager = engine_->getEngine()->getAudioFileFormatManager();
    params.audioFormat = formatManager.getWavFormat();
    params.bitDepth = Config::getInstance().getBounceBitDepth();
    params.sampleRateForAudio = Config::getInstance().getRenderSampleRate();
    params.blockSizeForAudio = 512;
    params.usePlugins = true;  // Synth is active, FX are bypassed
    params.useMasterPlugins = false;
    params.checkNodesForAudio = false;  // MIDI→synth generates audio

    // Time range = clip timeline range + tail allowance
    double endAllowance = 2.0;
    params.time =
        te::TimeRange(te::TimePosition::fromSeconds(clip->startTime),
                      te::TimePosition::fromSeconds(clip->startTime + clip->length + endAllowance));

    juce::BigInteger trackBits;
    trackBits.setBit(trackIndex);
    params.tracksToDo = trackBits;
    params.allowedClips.add(teClip);

    // Render
    RenderProgressWindow progressWindow("Bouncing In Place...", params);
    bool userCancelled = !progressWindow.runThread();

    // Restore FX plugins regardless of render outcome
    for (auto& state : savedStates) {
        state.plugin->setEnabled(state.wasEnabled);
    }

    if (userCancelled || !progressWindow.wasSuccessful()) {
        if (!userCancelled)
            DBG("BounceInPlaceCommand: render failed");
        if (renderedFile_.existsAsFile())
            renderedFile_.deleteFile();
        restoreTransport();
        return;
    }

    // Replace MIDI clip with audio clip using the original snapshot
    // (clip pointer may be invalidated by render/transport operations)
    double startTime = originalClipSnapshot_.startTime;
    double length = originalClipSnapshot_.length;
    TrackId trackId = originalClipSnapshot_.trackId;
    juce::Colour colour = originalClipSnapshot_.colour;
    juce::String name = originalClipSnapshot_.name;

    clipManager.deleteClip(clipId_);

    newClipId_ =
        clipManager.createAudioClip(trackId, startTime, length, renderedFile_.getFullPathName());

    if (auto* newClip = clipManager.getClip(newClipId_)) {
        newClip->colour = colour;
        newClip->name = name.isNotEmpty() ? name : renderedFile_.getFileNameWithoutExtension();
        clipManager.forceNotifyClipsChanged();
    }

    restoreTransport();
    success_ = true;
}

void BounceInPlaceCommand::undo() {
    if (!success_)
        return;

    auto& clipManager = ClipManager::getInstance();

    // Delete the replacement audio clip
    if (newClipId_ != INVALID_CLIP_ID) {
        clipManager.deleteClip(newClipId_);
        newClipId_ = INVALID_CLIP_ID;
    }

    // Restore original MIDI clip
    clipManager.restoreClip(originalClipSnapshot_);

    // Delete the rendered file
    if (renderedFile_.existsAsFile()) {
        renderedFile_.deleteFile();
    }

    success_ = false;
}

// ============================================================================
// BounceToNewTrackCommand
// ============================================================================

BounceToNewTrackCommand::BounceToNewTrackCommand(ClipId clipId, TracktionEngineWrapper* engine)
    : clipId_(clipId), engine_(engine) {}

void BounceToNewTrackCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || !engine_) {
        DBG("BounceToNewTrackCommand: invalid clip or engine");
        return;
    }

    auto* edit = engine_->getEdit();
    auto* bridge = engine_->getAudioBridge();
    if (!edit || !bridge) {
        DBG("BounceToNewTrackCommand: no edit or bridge");
        return;
    }

    // Find the TE clip
    auto* teClip = bridge->getArrangementTeClip(clipId_);
    if (!teClip) {
        DBG("BounceToNewTrackCommand: TE clip not found");
        return;
    }

    // Determine output file path
    auto configFolder = Config::getInstance().getRenderFolder();
    juce::File bouncesDir;
    if (!configFolder.empty()) {
        bouncesDir = juce::File(configFolder);
    } else {
        auto projBouncesDir = ProjectManager::getInstance().getBouncesDirectory();
        bouncesDir = projBouncesDir != juce::File()
                         ? projBouncesDir
                         : edit->editFileRetriever().getParentDirectory().getChildFile("bounces");
    }
    bouncesDir.createDirectory();

    {
        auto* trackInfo = TrackManager::getInstance().getTrack(clip->trackId);
        juce::String trackName = trackInfo ? trackInfo->name : "Track";
        juce::String clipName = clip->name.isNotEmpty() ? clip->name : "clip";
        renderedFile_ = bouncesDir.getChildFile(expandBouncePattern(clipName, trackName) + ".wav");
    }

    // Stop transport and free playback context
    auto& transport = edit->getTransport();
    bool wasPlaying = transport.isPlaying();
    if (wasPlaying) {
        transport.stop(false, false);
    }
    te::freePlaybackContextIfNotRecording(transport);

    auto restoreTransport = [wasPlaying, &transport]() {
        if (wasPlaying)
            transport.play(false);
    };

    // Find TE track
    auto* teTrack = teClip->getTrack();
    if (!teTrack) {
        DBG("BounceToNewTrackCommand: clip has no track");
        restoreTransport();
        return;
    }

    // Find track index
    auto allTracks = te::getAllTracks(*edit);
    int trackIndex = -1;
    for (int i = 0; i < allTracks.size(); ++i) {
        if (allTracks[i] == teTrack) {
            trackIndex = i;
            break;
        }
    }

    if (trackIndex < 0) {
        DBG("BounceToNewTrackCommand: track not found in edit");
        restoreTransport();
        return;
    }

    // Build Renderer::Parameters (full chain)
    te::Renderer::Parameters params(*edit);
    params.destFile = renderedFile_;
    auto& formatManager = engine_->getEngine()->getAudioFileFormatManager();
    params.audioFormat = formatManager.getWavFormat();
    params.bitDepth = Config::getInstance().getBounceBitDepth();
    params.sampleRateForAudio = Config::getInstance().getRenderSampleRate();
    params.blockSizeForAudio = 512;
    params.usePlugins = true;  // Full signal chain
    params.useMasterPlugins = false;
    params.checkNodesForAudio = false;

    double endAllowance = 2.0;
    params.time =
        te::TimeRange(te::TimePosition::fromSeconds(clip->startTime),
                      te::TimePosition::fromSeconds(clip->startTime + clip->length + endAllowance));

    juce::BigInteger trackBits;
    trackBits.setBit(trackIndex);
    params.tracksToDo = trackBits;
    params.allowedClips.add(teClip);

    // Render
    RenderProgressWindow progressWindow("Bouncing To New Track...", params);
    bool userCancelled = !progressWindow.runThread();

    if (userCancelled || !progressWindow.wasSuccessful()) {
        if (!userCancelled)
            DBG("BounceToNewTrackCommand: render failed");
        if (renderedFile_.existsAsFile())
            renderedFile_.deleteFile();
        restoreTransport();
        return;
    }

    // Save clip properties before createTrack/createAudioClip, which trigger
    // listener callbacks that may invalidate the clip pointer
    const auto clipName = clip->name;
    const auto clipColour = clip->colour;
    const auto clipStartTime = clip->startTime;
    const auto clipLength = clip->length;
    const auto clipTrackId = clip->trackId;

    // Create new audio track after the source track
    auto& trackManager = TrackManager::getInstance();
    juce::String trackName = clipName.isNotEmpty() ? clipName + " (bounced)" : "Bounced";
    newTrackId_ = trackManager.createTrack(trackName, TrackType::Audio);

    // Move new track to position after source track
    int sourceIndex = trackManager.getTrackIndex(clipTrackId);
    if (sourceIndex >= 0) {
        trackManager.moveTrack(newTrackId_, sourceIndex + 1);
    }

    // Create audio clip on new track
    newClipId_ = clipManager.createAudioClip(newTrackId_, clipStartTime, clipLength,
                                             renderedFile_.getFullPathName());

    if (auto* newClip = clipManager.getClip(newClipId_)) {
        newClip->colour = clipColour;
        newClip->name =
            clipName.isNotEmpty() ? clipName : renderedFile_.getFileNameWithoutExtension();
        clipManager.forceNotifyClipsChanged();
    }

    restoreTransport();
    success_ = true;
}

void BounceToNewTrackCommand::undo() {
    if (!success_)
        return;

    auto& clipManager = ClipManager::getInstance();

    // Delete the new audio clip
    if (newClipId_ != INVALID_CLIP_ID) {
        clipManager.deleteClip(newClipId_);
        newClipId_ = INVALID_CLIP_ID;
    }

    // Delete the new track
    if (newTrackId_ != INVALID_TRACK_ID) {
        TrackManager::getInstance().deleteTrack(newTrackId_);
        newTrackId_ = INVALID_TRACK_ID;
    }

    // Delete the rendered file
    if (renderedFile_.existsAsFile()) {
        renderedFile_.deleteFile();
    }

    success_ = false;
}

// ============================================================================
// FlattenMidiClipCommand
// ============================================================================

FlattenMidiClipCommand::FlattenMidiClipCommand(ClipId clipId) : clipId_(clipId) {}

void FlattenMidiClipCommand::execute() {
    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip || clip->type != ClipType::MIDI)
        return;

    beforeSnapshot_ = *clip;
    ClipOperations::flattenMidiClip(*clip);
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = true;
}

void FlattenMidiClipCommand::undo() {
    if (!executed_)
        return;

    auto& clipManager = ClipManager::getInstance();
    auto* clip = clipManager.getClip(clipId_);
    if (!clip)
        return;

    *clip = beforeSnapshot_;
    clipManager.forceNotifyClipPropertyChanged(clipId_);
    executed_ = false;
}

// ============================================================================
// RecordSessionToArrangementCommand
// ============================================================================

RecordSessionToArrangementCommand::RecordSessionToArrangementCommand(
    const std::vector<ClipInfo>& preRecordSnapshot)
    : preRecordSnapshot_(preRecordSnapshot) {}

void RecordSessionToArrangementCommand::execute() {
    auto& clipManager = ClipManager::getInstance();

    if (!snapshotCaptured_) {
        // First execution: the arrangement already contains the recorded clips.
        // Just capture the current state for redo.
        postRecordSnapshot_ = clipManager.getArrangementClips();
        snapshotCaptured_ = true;
    } else {
        // Redo: restore post-recording state
        auto currentClips = clipManager.getArrangementClips();
        for (const auto& clip : currentClips) {
            clipManager.deleteClip(clip.id);
        }
        for (const auto& clip : postRecordSnapshot_) {
            clipManager.restoreClip(clip);
        }
        clipManager.forceNotifyClipsChanged();
    }
}

void RecordSessionToArrangementCommand::undo() {
    auto& clipManager = ClipManager::getInstance();

    // Restore arrangement to pre-recording state
    auto currentClips = clipManager.getArrangementClips();
    for (const auto& clip : currentClips) {
        clipManager.deleteClip(clip.id);
    }
    for (const auto& clip : preRecordSnapshot_) {
        clipManager.restoreClip(clip);
    }
    clipManager.forceNotifyClipsChanged();
}

// ============================================================================
// Slice Utilities
// ============================================================================

void sliceClipAtTimes(ClipId clipId, const std::vector<double>& splitTimes, double tempo) {
    if (splitTimes.empty())
        return;

    auto& undoManager = UndoManager::getInstance();
    undoManager.beginCompoundOperation("Slice Clip");

    ClipId currentClipId = clipId;

    for (double splitTime : splitTimes) {
        auto cmd = std::make_unique<SplitClipCommand>(currentClipId, splitTime, tempo);
        auto* cmdPtr = cmd.get();
        undoManager.executeCommand(std::move(cmd));
        currentClipId = cmdPtr->getRightClipId();
        if (currentClipId == INVALID_CLIP_ID)
            break;
    }

    undoManager.endCompoundOperation();
}

void sliceClipAtWarpMarkers(ClipId clipId, double tempo, AudioBridge* bridge) {
    if (!bridge)
        return;

    auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->type != ClipType::Audio)
        return;

    auto markers = bridge->getWarpMarkers(clipId);
    if (markers.size() <= 2)
        return;  // Only boundary markers

    // Disable warp before splitting — splitClip uses a linear formula
    // (tempo/sourceBPM or speedRatio) to compute source offsets, but warp
    // markers define a non-linear mapping.  With warp off the linear formula
    // is correct, so we convert marker sourceTime values to the linear
    // timeline domain.
    clip->warpEnabled = false;
    bridge->disableWarp(clipId);

    double clipStart = clip->startTime;
    double clipEnd = clip->startTime + clip->length;
    double clipOffset = clip->offset;

    std::vector<double> splitTimes;
    splitTimes.reserve(markers.size());

    // Skip first and last markers (boundary markers at 0 and file end).
    // Convert each marker's sourceTime to a linear timeline position using
    // the inverse of splitClip's offset formula.
    for (size_t i = 1; i + 1 < markers.size(); ++i) {
        double sourceDelta = markers[i].sourceTime - clipOffset;
        double splitTime;
        if (clip->autoTempo && clip->sourceBPM > 0.0 && tempo > 0.0) {
            splitTime = clipStart + sourceDelta * clip->sourceBPM / tempo;
        } else {
            splitTime = clipStart + sourceDelta / clip->speedRatio;
        }
        if (splitTime > clipStart && splitTime < clipEnd) {
            splitTimes.push_back(splitTime);
        }
    }

    std::sort(splitTimes.begin(), splitTimes.end());
    splitTimes.erase(std::unique(splitTimes.begin(), splitTimes.end()), splitTimes.end());

    sliceClipAtTimes(clipId, splitTimes, tempo);
}

void sliceClipAtGrid(ClipId clipId, double gridInterval, double tempo, AudioBridge* bridge) {
    if (gridInterval <= 0.0)
        return;

    auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip)
        return;

    // Disable warp before splitting if enabled
    if (clip->warpEnabled) {
        clip->warpEnabled = false;
        if (bridge)
            bridge->disableWarp(clipId);
    }

    double clipStart = clip->startTime;
    double clipEnd = clip->startTime + clip->length;

    double startK = std::ceil(clipStart / gridInterval);
    double iterStart = startK * gridInterval;

    std::vector<double> splitTimes;
    for (double t = iterStart; t < clipEnd; t += gridInterval) {
        if (t > clipStart && t < clipEnd)
            splitTimes.push_back(t);
    }

    sliceClipAtTimes(clipId, splitTimes, tempo);
}

namespace {

struct SliceRegion {
    double sourceStart;
    double sourceEnd;
    double timelinePos;
};

/**
 * Core helper: given pre-computed slice regions, create a DrumGrid
 * track, load each region to a pad, and write a MIDI clip.
 */
void buildDrumGridFromSlices(const std::vector<SliceRegion>& slices, const ClipInfo& clip,
                             const juce::File& audioFile, double tempo, AudioBridge* bridge) {
    if (slices.empty())
        return;

    int numSlices = static_cast<int>(slices.size());
    if (numSlices > daw::audio::DrumGridPlugin::maxPads)
        numSlices = daw::audio::DrumGridPlugin::maxPads;

    // Create Instrument track with DrumGridPlugin
    auto& trackManager = TrackManager::getInstance();
    juce::String clipName =
        clip.name.isNotEmpty() ? clip.name : audioFile.getFileNameWithoutExtension();
    TrackId newTrackId = trackManager.createTrack("Drum Grid - " + clipName, TrackType::Instrument);
    if (newTrackId == INVALID_TRACK_ID)
        return;

    DeviceInfo dgDevice;
    dgDevice.name = "Drum Grid";
    dgDevice.pluginId = "drumgrid";
    dgDevice.format = PluginFormat::Internal;
    dgDevice.isInstrument = true;
    trackManager.addDeviceToTrack(newTrackId, dgDevice);

    // Find the DrumGridPlugin that was just created
    auto* audioEngine = trackManager.getAudioEngine();
    if (!audioEngine)
        return;
    auto* teTrack = bridge->getAudioTrack(newTrackId);
    if (!teTrack)
        return;

    daw::audio::DrumGridPlugin* drumGrid = nullptr;
    for (auto* plugin : teTrack->pluginList) {
        drumGrid = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin);
        if (drumGrid)
            break;
        if (auto* rackInstance = dynamic_cast<te::RackInstance*>(plugin)) {
            if (rackInstance->type != nullptr) {
                for (auto* innerPlugin : rackInstance->type->getPlugins()) {
                    drumGrid = dynamic_cast<daw::audio::DrumGridPlugin*>(innerPlugin);
                    if (drumGrid)
                        break;
                }
            }
        }
        if (drumGrid)
            break;
    }

    if (!drumGrid) {
        DBG("buildDrumGridFromSlices: DrumGridPlugin not found on new track");
        return;
    }

    // Load samples to pads and set region boundaries
    for (int i = 0; i < numSlices; ++i) {
        const auto& slice = slices[static_cast<size_t>(i)];
        drumGrid->loadSampleToPad(i, audioFile);

        auto* chain = drumGrid->getChainByIndexMutable(i);
        if (chain && !chain->plugins.empty()) {
            auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(chain->plugins[0].get());
            if (sampler) {
                auto startSec = static_cast<float>(slice.sourceStart);
                auto endSec = static_cast<float>(slice.sourceEnd);
                sampler->sampleStartParam->setParameter(startSec, juce::dontSendNotification);
                sampler->sampleStartValue = startSec;
                sampler->sampleEndParam->setParameter(endSec, juce::dontSendNotification);
                sampler->sampleEndValue = endSec;
            }
        }
    }

    // Create MIDI clip with notes triggering each pad
    auto& clipManager = ClipManager::getInstance();
    double clipStart = clip.startTime;
    double clipEnd = clip.startTime + clip.length;
    ClipId midiClipId = clipManager.createMidiClip(newTrackId, clipStart, clip.length);
    if (midiClipId == INVALID_CLIP_ID)
        return;

    double beatsPerSecond = tempo / 60.0;

    for (int i = 0; i < numSlices; ++i) {
        const auto& slice = slices[static_cast<size_t>(i)];
        double noteStartTime = slice.timelinePos - clipStart;
        double noteStartBeat = noteStartTime * beatsPerSecond;

        double nextTimeline =
            (i + 1 < numSlices) ? slices[static_cast<size_t>(i + 1)].timelinePos : clipEnd;
        double noteDuration = nextTimeline - slice.timelinePos;
        double noteLengthBeats = noteDuration * beatsPerSecond;

        if (noteStartBeat < 0.0)
            noteStartBeat = 0.0;
        if (noteLengthBeats <= 0.0)
            noteLengthBeats = 0.01;

        MidiNote note;
        note.noteNumber = daw::audio::DrumGridPlugin::baseNote + i;
        note.velocity = 100;
        note.startBeat = noteStartBeat;
        note.lengthBeats = noteLengthBeats;
        clipManager.addMidiNote(midiClipId, note);
    }

    clipManager.forceNotifyClipsChanged();
}

}  // namespace

void sliceWarpMarkersToDrumGrid(ClipId clipId, double tempo, AudioBridge* bridge) {
    if (!bridge)
        return;

    auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->type != ClipType::Audio || clip->audioFilePath.isEmpty())
        return;

    auto markers = bridge->getWarpMarkers(clipId);
    if (markers.size() <= 2)
        return;

    juce::File audioFile(clip->audioFilePath);
    if (!audioFile.existsAsFile())
        return;

    double clipStart = clip->startTime;
    double clipEnd = clip->startTime + clip->length;
    double clipOffset = clip->offset;

    // Build sorted interior marker source times
    std::vector<double> interiorSourceTimes;
    interiorSourceTimes.reserve(markers.size());
    for (size_t i = 1; i + 1 < markers.size(); ++i) {
        interiorSourceTimes.push_back(markers[i].sourceTime);
    }
    std::sort(interiorSourceTimes.begin(), interiorSourceTimes.end());

    // Build region boundaries: [first boundary, interior..., last boundary]
    double regionStart = markers.front().sourceTime;
    double regionEnd = markers.back().sourceTime;

    std::vector<double> boundaries;
    boundaries.push_back(regionStart);
    for (double t : interiorSourceTimes) {
        if (t > regionStart && t < regionEnd)
            boundaries.push_back(t);
    }
    boundaries.push_back(regionEnd);

    auto sourceToTimeline = [&](double sourceTime) -> double {
        double sourceDelta = sourceTime - clipOffset;
        if (clip->autoTempo && clip->sourceBPM > 0.0 && tempo > 0.0)
            return clipStart + sourceDelta * clip->sourceBPM / tempo;
        else
            return clipStart + sourceDelta / clip->speedRatio;
    };

    std::vector<SliceRegion> slices;
    for (size_t i = 0; i + 1 < boundaries.size(); ++i) {
        double tlPos = sourceToTimeline(boundaries[i]);
        if (tlPos >= clipEnd)
            break;
        double tlEnd = sourceToTimeline(boundaries[i + 1]);
        if (tlEnd <= clipStart)
            continue;
        slices.push_back({boundaries[i], boundaries[i + 1], tlPos});
    }

    buildDrumGridFromSlices(slices, *clip, audioFile, tempo, bridge);
}

void sliceAtGridToDrumGrid(ClipId clipId, double gridInterval, double tempo, AudioBridge* bridge) {
    if (!bridge || gridInterval <= 0.0)
        return;

    auto* clip = ClipManager::getInstance().getClip(clipId);
    if (!clip || clip->type != ClipType::Audio || clip->audioFilePath.isEmpty())
        return;

    juce::File audioFile(clip->audioFilePath);
    if (!audioFile.existsAsFile())
        return;

    double clipStart = clip->startTime;
    double clipEnd = clip->startTime + clip->length;
    double clipOffset = clip->offset;

    // Convert timeline grid lines to source-file boundaries
    auto timelineToSource = [&](double timelinePos) -> double {
        double delta = timelinePos - clipStart;
        if (clip->autoTempo && clip->sourceBPM > 0.0 && tempo > 0.0)
            return clipOffset + delta * tempo / clip->sourceBPM;
        else
            return clipOffset + delta * clip->speedRatio;
    };

    // Build timeline grid positions within the clip
    double startK = std::ceil(clipStart / gridInterval);
    double iterStart = startK * gridInterval;

    std::vector<double> gridTimes;
    gridTimes.push_back(clipStart);  // Always start at clip start
    for (double t = iterStart; t < clipEnd; t += gridInterval) {
        if (t > clipStart && t < clipEnd)
            gridTimes.push_back(t);
    }
    gridTimes.push_back(clipEnd);  // Always end at clip end

    // Convert to slice regions
    std::vector<SliceRegion> slices;
    for (size_t i = 0; i + 1 < gridTimes.size(); ++i) {
        double srcStart = timelineToSource(gridTimes[i]);
        double srcEnd = timelineToSource(gridTimes[i + 1]);
        slices.push_back({srcStart, srcEnd, gridTimes[i]});
    }

    buildDrumGridFromSlices(slices, *clip, audioFile, tempo, bridge);
}

}  // namespace magda
