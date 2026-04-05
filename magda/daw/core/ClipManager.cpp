#include "ClipManager.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "../project/ProjectManager.hpp"
#include "ClipOperations.hpp"
#include "Config.hpp"
#include "TrackManager.hpp"
#include "audio/AudioThumbnailManager.hpp"

namespace magda {

ClipManager& ClipManager::getInstance() {
    static ClipManager instance;
    return instance;
}

// ============================================================================
// Clip Creation
// ============================================================================

ClipId ClipManager::createAudioClip(TrackId trackId, double startTime, double length,
                                    const juce::String& audioFilePath, ClipView view,
                                    double projectBPM) {
    ClipInfo clip;
    clip.id = nextClipId_++;
    clip.trackId = trackId;
    clip.type = ClipType::Audio;
    clip.view = view;
    if (audioFilePath.isNotEmpty()) {
        clip.name = juce::File(audioFilePath).getFileNameWithoutExtension();
    } else {
        clip.name = generateClipName(ClipType::Audio);
    }
    if (Config::getInstance().getClipColourMode() == 0) {
        // Inherit from parent track
        const auto* track = TrackManager::getInstance().getTrack(trackId);
        clip.colour = track ? track->colour : juce::Colour(Config::getDefaultColour(0));
    } else {
        clip.colour = juce::Colour(Config::getDefaultColour(static_cast<int>(clips_.size())));
    }
    clip.startTime = startTime;
    clip.length = length;
    clip.audioFilePath = audioFilePath;
    clip.offset = 0.0;
    clip.speedRatio = 1.0;

    // Set loopStart to offset (0), loopLength to the clip's source extent
    clip.loopStart = 0.0;
    clip.setLoopLengthFromTimeline(length);

    if (view == ClipView::Arrangement) {
        // Set beat position for tempo-independent placement
        double bpm = projectBPM > 0.0 ? projectBPM
                                      : ProjectManager::getInstance().getCurrentProjectInfo().tempo;
        if (bpm > 0.0) {
            clip.startBeats = startTime * bpm / 60.0;
            // Don't set lengthBeats for non-autoTempo audio clips — time is
            // authoritative and beats should be derived at display time so the
            // clip width updates correctly when BPM changes.
            clip.lengthBeats = 0.0;
        }
        clips_[clip.id] = clip;
        resolveOverlaps(clip.id);
    } else {
        // Session clips loop by default and follow project tempo
        clip.loopEnabled = true;
        clip.autoTempo = true;
        double bpm = projectBPM > 0.0 ? projectBPM
                                      : ProjectManager::getInstance().getCurrentProjectInfo().tempo;
        if (bpm > 0.0) {
            clip.startBeats = (startTime * bpm) / 60.0;
            clip.lengthBeats = (length * bpm) / 60.0;
            clip.loopLengthBeats = clip.lengthBeats;
        }
        clip.length = length;
        clips_[clip.id] = clip;
    }

    notifyClipsChanged();

    return clip.id;
}

ClipId ClipManager::createMidiClip(TrackId trackId, double startTime, double length,
                                   ClipView view) {
    ClipInfo clip;
    clip.id = nextClipId_++;
    clip.trackId = trackId;
    clip.type = ClipType::MIDI;
    clip.view = view;
    clip.name = generateClipName(ClipType::MIDI);
    if (Config::getInstance().getClipColourMode() == 0) {
        const auto* track = TrackManager::getInstance().getTrack(trackId);
        clip.colour = track ? track->colour : juce::Colour(Config::getDefaultColour(0));
    } else {
        clip.colour = juce::Colour(Config::getDefaultColour(static_cast<int>(clips_.size())));
    }
    clip.startTime = startTime;
    clip.length = length;

    // MIDI clips: beats are authoritative, derive from seconds using project tempo
    double tempo = ProjectManager::getInstance().getCurrentProjectInfo().tempo;
    if (tempo > 0.0) {
        clip.startBeats = (startTime * tempo) / 60.0;
        clip.lengthBeats = (length * tempo) / 60.0;
    }

    if (view == ClipView::Arrangement) {
        clips_[clip.id] = clip;
        resolveOverlaps(clip.id);
    } else {
        // Session clips loop by default
        clip.loopEnabled = true;
        clip.loopLengthBeats = clip.lengthBeats;
        clip.loopLength = length;
        clips_[clip.id] = clip;
    }

    notifyClipsChanged();

    return clip.id;
}

void ClipManager::deleteClip(ClipId clipId) {
    auto it = clips_.find(clipId);
    if (it == clips_.end())
        return;

    if (selectedClipId_ == clipId) {
        selectedClipId_ = INVALID_CLIP_ID;
        notifyClipSelectionChanged(INVALID_CLIP_ID);
    }
    if (lastTriggeredSessionClipId_ == clipId) {
        lastTriggeredSessionClipId_ = INVALID_CLIP_ID;
    }

    clips_.erase(it);
    notifyClipsChanged();
}

void ClipManager::restoreClip(const ClipInfo& clipInfo) {
    if (clips_.count(clipInfo.id))
        return;

    clips_[clipInfo.id] = clipInfo;

    // Ensure nextClipId_ is beyond any restored clip IDs
    if (clipInfo.id >= nextClipId_) {
        nextClipId_ = clipInfo.id + 1;
    }

    notifyClipsChanged();
}

void ClipManager::forceNotifyClipsChanged() {
    notifyClipsChanged();
}

void ClipManager::forceNotifyClipPropertyChanged(ClipId clipId) {
    notifyClipPropertyChanged(clipId);
}

void ClipManager::forceNotifyMultipleClipPropertiesChanged(const std::vector<ClipId>& clipIds) {
    if (clipIds.empty())
        return;
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipPropertiesChanged(clipIds);
        }
    }
}

ClipId ClipManager::duplicateClip(ClipId clipId) {
    const auto* original = getClip(clipId);
    if (!original) {
        return INVALID_CLIP_ID;
    }

    ClipInfo newClip = *original;
    newClip.id = nextClipId_++;
    newClip.name = original->name + " Copy";

    if (newClip.view == ClipView::Arrangement) {
        // Offset the duplicate to the right on the timeline
        newClip.startTime = original->startTime + original->length;
        if (newClip.type == ClipType::MIDI) {
            newClip.startBeats = original->startBeats + original->lengthBeats;
        }
    } else {
        // Session clips always loop
        newClip.startTime = 0.0;
        newClip.loopEnabled = true;
    }
    clips_[newClip.id] = newClip;

    notifyClipsChanged();

    return newClip.id;
}

ClipId ClipManager::duplicateClipAt(ClipId clipId, double startTime, TrackId trackId,
                                    double tempo) {
    const auto* original = getClip(clipId);
    if (!original) {
        return INVALID_CLIP_ID;
    }

    ClipInfo newClip = *original;
    newClip.id = nextClipId_++;
    newClip.name = original->name + " Copy";

    // Use specified track or keep same track
    if (trackId != INVALID_TRACK_ID) {
        newClip.trackId = trackId;
    }

    if (newClip.view == ClipView::Arrangement) {
        newClip.startTime = startTime;
        if (tempo > 0.0)
            newClip.startBeats = startTime * tempo / 60.0;
        clips_[newClip.id] = newClip;
        resolveOverlaps(newClip.id);
    } else {
        // Session clips always loop
        newClip.startTime = 0.0;
        newClip.loopEnabled = true;
        clips_[newClip.id] = newClip;
    }

    notifyClipsChanged();

    return newClip.id;
}

void ClipManager::resetLoopedClipLength(ClipInfo& clip) {
    if (!clip.loopEnabled)
        return;

    if (clip.isBeatsAuthoritative() && clip.loopLengthBeats > 0.0) {
        clip.lengthBeats = clip.loopLengthBeats;
    } else if (clip.loopLength > 0.0) {
        clip.length = clip.sourceToTimeline(clip.loopLength);
    }
    clip.loopEnabled = false;
}

// ============================================================================
// Clip Manipulation
// ============================================================================

void ClipManager::moveClip(ClipId clipId, double newStartTime, double tempo) {
    if (auto* clip = getClip(clipId)) {
        ClipOperations::moveContainer(*clip, newStartTime);
        if (tempo > 0.0)
            clip->startBeats = clip->startTime * tempo / 60.0;
        // Notes maintain their relative position within the clip (startBeat unchanged)
        // so they move with the clip on the timeline
        resolveOverlaps(clipId);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::moveClipToTrack(ClipId clipId, TrackId newTrackId) {
    if (auto* clip = getClip(clipId)) {
        if (clip->trackId != newTrackId) {
            clip->trackId = newTrackId;
            resolveOverlaps(clipId);
            notifyClipsChanged();  // Track assignment change affects layout
        }
    }
}

void ClipManager::resizeClip(ClipId clipId, double newLength, bool fromStart, double tempo) {
    if (auto* clip = getClip(clipId)) {
        if (fromStart) {
            ClipOperations::resizeContainerFromLeft(*clip, newLength, tempo);
            // Non-loop mode: keep loopStart synced to offset
            if (!clip->loopEnabled && clip->type == ClipType::Audio) {
                clip->loopStart = clip->offset;
            }
        } else {
            ClipOperations::resizeContainerFromRight(*clip, newLength, tempo);

            // In non-loop mode, clip length defines the source region — keep loopLength in sync
            if (!clip->loopEnabled && clip->type == ClipType::Audio) {
                clip->loopLength = clip->timelineToSource(clip->length);
                if (clip->autoTempo && clip->sourceBPM > 0.0) {
                    clip->loopLengthBeats = clip->loopLength * clip->sourceBPM / 60.0;
                }
            }
        }
        resolveOverlaps(clipId);
        notifyClipPropertyChanged(clipId);
    }
}

ClipId ClipManager::splitClip(ClipId clipId, double splitTime, double tempo) {
    auto* clip = getClip(clipId);
    if (!clip) {
        return INVALID_CLIP_ID;
    }

    // Validate split position is within clip
    if (splitTime <= clip->startTime || splitTime >= clip->getEndTime()) {
        return INVALID_CLIP_ID;
    }

    // Calculate lengths
    double leftLength = splitTime - clip->startTime;
    double rightLength = clip->getEndTime() - splitTime;

    // Create right half as new clip
    ClipInfo rightClip = *clip;
    rightClip.id = nextClipId_++;
    rightClip.name = clip->name + " R";
    rightClip.startTime = splitTime;
    rightClip.length = rightLength;

    // Adjust offset for right clip (TE-aligned: offset is start position in source)
    if (rightClip.type == ClipType::Audio) {
        // In autoTempo/warp mode, speedRatio is 1.0 but actual stretch is projectBPM/sourceBPM.
        // Use the tempo ratio to convert timeline seconds to source seconds.
        if (clip->isBeatsAuthoritative() && clip->sourceBPM > 0.0 && tempo > 0.0) {
            double deltaBeats = leftLength * tempo / 60.0;
            rightClip.offsetBeats += deltaBeats;
            rightClip.offset = rightClip.offsetBeats * 60.0 / clip->sourceBPM;
        } else {
            rightClip.offset += leftLength * clip->speedRatio;
        }
    }

    // Handle MIDI clip splitting
    if (rightClip.type == ClipType::MIDI && !rightClip.midiNotes.empty()) {
        if (clip->loopEnabled && clip->loopLengthBeats > 0.0) {
            // Looped MIDI: both halves keep the same notes.
            // If the split falls mid-loop, adjust the right clip's midiOffset
            // so it starts playing from the correct phase within the loop.
            // If the split lands on a loop boundary, midiOffset stays unchanged.
            const double beatsPerSecond = tempo / 60.0;
            double splitBeat = leftLength * beatsPerSecond;
            double loopLen = clip->loopLengthBeats;
            double phase = std::fmod(splitBeat, loopLen);
            // Treat near-zero and near-loopLen as boundary (floating-point tolerance)
            constexpr double kEpsilon = 0.0001;
            bool onBoundary = phase < kEpsilon || (loopLen - phase) < kEpsilon;
            if (!onBoundary) {
                rightClip.midiOffset = std::fmod(clip->midiOffset + phase, loopLen);
            }
        } else {
            // Non-looped MIDI: partition notes by split position
            const double beatsPerSecond = tempo / 60.0;
            double splitBeat = leftLength * beatsPerSecond;

            std::vector<MidiNote> leftNotes;
            std::vector<MidiNote> rightNotes;

            for (const auto& note : clip->midiNotes) {
                if (note.startBeat < splitBeat) {
                    leftNotes.push_back(note);
                } else {
                    MidiNote adjustedNote = note;
                    adjustedNote.startBeat -= splitBeat;
                    rightNotes.push_back(adjustedNote);
                }
            }

            clip->midiNotes = leftNotes;
            rightClip.midiNotes = rightNotes;
        }
    }

    // Resize original clip to be left half
    clip->length = leftLength;
    clip->name = clip->name + " L";

    // Update beat fields for both halves
    if (tempo > 0.0)
        rightClip.startBeats = splitTime * tempo / 60.0;
    if (clip->isBeatsAuthoritative() && tempo > 0.0) {
        // Left clip: lengthBeats changes, startBeats stays the same
        clip->lengthBeats = leftLength * tempo / 60.0;
        rightClip.lengthBeats = rightLength * tempo / 60.0;
    }

    // Sync loop region after split
    if (clip->loopEnabled) {
        // Looped clip: if the loop region is longer than the new clip length,
        // truncate it so each half only loops over its own portion.
        double beatsPerSecond = tempo > 0.0 ? tempo / 60.0 : 2.0;

        // Left clip
        double leftLenBeats = leftLength * beatsPerSecond;
        if (clip->loopLengthBeats > leftLenBeats) {
            clip->loopLengthBeats = leftLenBeats;
            if (clip->type == ClipType::Audio) {
                double srcBpm = clip->sourceBPM > 0.0 ? clip->sourceBPM : tempo;
                clip->loopLength = clip->loopLengthBeats / srcBpm * 60.0;
            }
        }

        // Right clip
        double rightLenBeats = rightLength * beatsPerSecond;
        if (rightClip.loopLengthBeats > rightLenBeats) {
            rightClip.loopLengthBeats = rightLenBeats;
            if (rightClip.type == ClipType::Audio) {
                double srcBpm = rightClip.sourceBPM > 0.0 ? rightClip.sourceBPM : tempo;
                if (rightClip.autoTempo && rightClip.sourceBPM > 0.0) {
                    rightClip.loopStartBeats = rightClip.offsetBeats;
                    rightClip.loopStart = rightClip.loopStartBeats * 60.0 / rightClip.sourceBPM;
                } else {
                    rightClip.loopStart = rightClip.offset;
                    rightClip.loopStartBeats = rightClip.loopStart * srcBpm / 60.0;
                }
                rightClip.loopLength = rightClip.loopLengthBeats / srcBpm * 60.0;
            }
        }
    } else if (clip->type == ClipType::Audio) {
        // Non-looped audio: sync loopStart/loopLength to actual source extent
        // Left clip: loopStart stays at original value, loopLength shrinks
        clip->loopLength = clip->timelineToSource(clip->length);
        if (clip->isBeatsAuthoritative() && tempo > 0.0) {
            clip->loopLengthBeats =
                clip->loopLength * (clip->sourceBPM > 0.0 ? clip->sourceBPM : tempo) / 60.0;
        }

        // Right clip: loopStart must match offset so TE's loop range covers the
        // correct source region (otherwise offset falls outside the loop range,
        // causing TE to wrap and produce doubled transients at the split point)
        if (rightClip.autoTempo && rightClip.sourceBPM > 0.0) {
            rightClip.loopStartBeats = rightClip.offsetBeats;
            rightClip.loopStart = rightClip.loopStartBeats * 60.0 / rightClip.sourceBPM;
        } else {
            rightClip.loopStart = rightClip.offset;
        }
        rightClip.loopLength = rightClip.timelineToSource(rightClip.length);
        if (rightClip.isBeatsAuthoritative() && tempo > 0.0) {
            double srcBpm = rightClip.sourceBPM > 0.0 ? rightClip.sourceBPM : tempo;
            if (!rightClip.autoTempo)
                rightClip.loopStartBeats = rightClip.loopStart * srcBpm / 60.0;
            rightClip.loopLengthBeats = rightClip.loopLength * srcBpm / 60.0;
        }
    }

    // Time-stretched clips (autoTempo/warp): add small anti-click fades at the
    // split boundary.  The stretcher's overlapping analysis windows bleed audio
    // from beyond the boundary, which sounds like a doubled transient.  A short
    // fade masks this startup/shutdown artifact without being audible.
    if (clip->type == ClipType::Audio && clip->isBeatsAuthoritative()) {
        constexpr double kSplitFadeSeconds = 0.005;  // 5 ms
        clip->fadeOut = kSplitFadeSeconds;
        rightClip.fadeIn = kSplitFadeSeconds;
    }

    // Add right clip to the clip pool
    clips_[rightClip.id] = rightClip;

    notifyClipsChanged();

    return rightClip.id;
}

void ClipManager::trimClip(ClipId clipId, double newStartTime, double newLength, double tempo) {
    if (auto* clip = getClip(clipId)) {
        clip->startTime = newStartTime;
        clip->length = newLength;
        if (tempo > 0.0)
            clip->startBeats = newStartTime * tempo / 60.0;
        if (clip->isBeatsAuthoritative() && tempo > 0.0) {
            clip->lengthBeats = newLength * tempo / 60.0;
        }
        notifyClipPropertyChanged(clipId);
    }
}

// ============================================================================
// Clip Properties
// ============================================================================

void ClipManager::setClipName(ClipId clipId, const juce::String& name) {
    if (auto* clip = getClip(clipId)) {
        clip->name = name;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipColour(ClipId clipId, juce::Colour colour) {
    if (auto* clip = getClip(clipId)) {
        clip->colour = colour;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipLoopEnabled(ClipId clipId, bool enabled, double projectBPM) {
    if (auto* clip = getClip(clipId)) {
        clip->loopEnabled = enabled;

        // When enabling loop on MIDI clips, capture current length as loop region
        // Populate both beat and seconds fields so all existing code paths work
        if (enabled && clip->type == ClipType::MIDI) {
            double bpm = juce::jmax(1.0, projectBPM);
            if (clip->loopLengthBeats <= 0.0) {
                clip->loopLengthBeats =
                    clip->lengthBeats > 0.0 ? clip->lengthBeats : clip->length * bpm / 60.0;
            }
            clip->loopLength = clip->loopLengthBeats * 60.0 / bpm;
        }

        // When enabling loop on audio clips, transfer offset → loopStart
        // The user's current offset becomes the loop start point (phase resets to 0)
        if (enabled && clip->type == ClipType::Audio && clip->audioFilePath.isNotEmpty()) {
            clip->loopStart = clip->offset;

            // Ensure loopLength is set (preserves source extent in loop mode)
            if (clip->loopLength <= 0.0) {
                clip->setLoopLengthFromTimeline(clip->length);
            }

            sanitizeAudioClip(*clip);
        }

        // When disabling loop on MIDI clips, reset midiOffset — the looped
        // phase value has no meaning in non-looped mode.
        if (!enabled && clip->type == ClipType::MIDI) {
            clip->midiOffset = 0.0;
        }

        // When disabling loop on audio clips, sync loopStart and clamp length to actual file
        // content
        if (!enabled && clip->type == ClipType::Audio && clip->audioFilePath.isNotEmpty()) {
            clip->loopStart = clip->offset;
            auto* thumbnail =
                AudioThumbnailManager::getInstance().getThumbnail(clip->audioFilePath);
            if (thumbnail) {
                clip->clampLengthToSource(thumbnail->getTotalLength());
            }
        }

        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipMidiOffset(ClipId clipId, double offsetBeats) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type != ClipType::MIDI) {
            return;
        }
        clip->midiOffset = juce::jmax(0.0, offsetBeats);
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipLaunchMode(ClipId clipId, LaunchMode mode) {
    if (auto* clip = getClip(clipId)) {
        clip->launchMode = mode;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipLaunchQuantize(ClipId clipId, LaunchQuantize quantize) {
    if (auto* clip = getClip(clipId)) {
        clip->launchQuantize = quantize;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipWarpEnabled(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && clip->warpEnabled != enabled) {
            clip->warpEnabled = enabled;
            if (enabled)
                clip->analogPitch = false;  // Analog pitch is incompatible with warp
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAutoTempo(ClipId clipId, bool enabled, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            ClipOperations::setAutoTempo(*clip, enabled, bpm);

            // Ensure time-stretching is enabled when beat mode is on
            if (enabled && clip->timeStretchMode == 0)
                clip->timeStretchMode = 4;  // soundtouchBetter

            double newLength = (enabled && clip->lengthBeats > 0.0 && bpm > 0.0)
                                   ? (clip->lengthBeats * 60.0 / bpm)
                                   : clip->length;
            resizeClip(clipId, newLength, false, bpm);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setOffset(ClipId clipId, double offset) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI) {
            // MIDI phase lives in midiOffset (beats) — caller passes beats directly
            clip->midiOffset = juce::jmax(0.0, offset);
        } else {
            clip->offset = juce::jmax(0.0, offset);
            if (clip->autoTempo && clip->sourceBPM > 0.0)
                clip->offsetBeats = clip->offset * clip->sourceBPM / 60.0;
            sanitizeAudioClip(*clip);
        }
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setLoopPhase(ClipId clipId, double phase) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && clip->loopEnabled) {
            clip->offset = clip->loopStart + phase;
            if (clip->autoTempo && clip->sourceBPM > 0.0)
                clip->offsetBeats = clip->offset * clip->sourceBPM / 60.0;
            sanitizeAudioClip(*clip);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setLoopStart(ClipId clipId, double loopStart, double bpm) {
    if (auto* clip = getClip(clipId)) {
        clip->loopStart = juce::jmax(0.0, loopStart);
        if (clip->type == ClipType::Audio) {
            if (clip->autoTempo) {
                // Use sourceBPM for beat conversion — loopStartBeats is in source-beat domain
                double convBpm = (clip->sourceBPM > 0.0) ? clip->sourceBPM : bpm;
                clip->loopStartBeats = (clip->loopStart * convBpm) / 60.0;
            }
            sanitizeAudioClip(*clip);
        }
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setLoopLength(ClipId clipId, double loopLength, double bpm) {
    if (auto* clip = getClip(clipId)) {
        clip->loopLength = juce::jmax(0.0, loopLength);
        if (clip->type == ClipType::MIDI) {
            // MIDI: keep loopLengthBeats in sync using project BPM
            clip->loopLengthBeats = (clip->loopLength * juce::jmax(1.0, bpm)) / 60.0;
        } else if (clip->type == ClipType::Audio) {
            if (clip->autoTempo) {
                // Use sourceBPM for beat conversion — loopLengthBeats is in source-beat domain
                double convBpm = (clip->sourceBPM > 0.0) ? clip->sourceBPM : bpm;
                clip->loopLengthBeats = (clip->loopLength * convBpm) / 60.0;
            }
            sanitizeAudioClip(*clip);
        }
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setLoopStartAndLength(ClipId clipId, double loopStart, double loopLength,
                                        double bpm) {
    if (auto* clip = getClip(clipId)) {
        clip->loopStart = juce::jmax(0.0, loopStart);
        clip->loopLength = juce::jmax(0.0, loopLength);

        if (clip->type == ClipType::Audio) {
            if (clip->autoTempo) {
                double convBpm = (clip->sourceBPM > 0.0) ? clip->sourceBPM : bpm;
                clip->loopStartBeats = (clip->loopStart * convBpm) / 60.0;
                clip->loopLengthBeats = (clip->loopLength * convBpm) / 60.0;
            }
            sanitizeAudioClip(*clip);
        } else if (clip->type == ClipType::MIDI) {
            clip->loopLengthBeats = (clip->loopLength * juce::jmax(1.0, bpm)) / 60.0;
        }

        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setLengthBeats(ClipId clipId, double newBeats, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio && clip->autoTempo && bpm > 0.0) {
            double minBeats = ClipInfo::MIN_CLIP_LENGTH * bpm / 60.0;
            newBeats = juce::jmax(minBeats, newBeats);

            // Source audio length stays constant — we change sourceBPM to stretch
            double sourceSeconds =
                clip->loopLength > 0.0 ? clip->loopLength : clip->getSourceLength();
            if (sourceSeconds <= 0.0)
                return;

            // Compute new sourceBPM so TE stretches the same audio to fill newBeats
            // Formula: sourceBPM = beats * 60 / sourceSeconds
            double oldSourceBPM = clip->sourceBPM;
            clip->sourceBPM = newBeats * 60.0 / sourceSeconds;

            // Scale sourceNumBeats proportionally
            if (oldSourceBPM > 0.0 && clip->sourceNumBeats > 0.0) {
                clip->sourceNumBeats *= clip->sourceBPM / oldSourceBPM;
            }

            // Update beat values — scale lengthBeats proportionally for sub-loop case
            double oldLoopLengthBeats = clip->loopLengthBeats;
            clip->loopLengthBeats = newBeats;
            if (oldLoopLengthBeats > 0.0) {
                clip->lengthBeats = clip->lengthBeats * newBeats / oldLoopLengthBeats;
            } else {
                clip->lengthBeats = newBeats;
            }

            // Update loopStartBeats to match new sourceBPM domain
            clip->loopStartBeats = clip->loopStart * clip->sourceBPM / 60.0;

            // Update clip timeline duration from new beat length
            clip->length = clip->lengthBeats * 60.0 / bpm;

            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setSpeedRatio(ClipId clipId, double speedRatio) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            double oldSourceExtent = clip->timelineToSource(clip->length);
            clip->speedRatio = juce::jlimit(ClipOperations::MIN_SPEED_RATIO,
                                            ClipOperations::MAX_SPEED_RATIO, speedRatio);
            double newSourceExtent = clip->timelineToSource(clip->length);

            // Keep loopLength in sync when the loop covers the full source extent
            // (non-looped clips, or looped clips where the loop wasn't user-shortened)
            if (!clip->loopEnabled || std::abs(clip->loopLength - oldSourceExtent) < 0.001) {
                clip->loopLength = newSourceExtent;
            }
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setTimeStretchMode(ClipId clipId, int mode) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->timeStretchMode = mode;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Pitch
// ============================================================================

void ClipManager::setAutoPitch(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->autoPitch = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAnalogPitch(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->analogPitch = enabled;
            if (enabled && !clip->autoTempo && !clip->warpEnabled) {
                // Recompute speedRatio from current pitchChange
                double pitchFactor = std::pow(2.0, clip->pitchChange / 12.0);
                double sourceContent = clip->length * clip->speedRatio;
                clip->speedRatio = pitchFactor;
                clip->length = juce::jmax(ClipInfo::MIN_CLIP_LENGTH, sourceContent / pitchFactor);
            }
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAutoPitchMode(ClipId clipId, int mode) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->autoPitchMode = juce::jlimit(0, 2, mode);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setPitchChange(ClipId clipId, float semitones) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            float old = clip->pitchChange;
            clip->pitchChange = juce::jlimit(-48.0f, 48.0f, semitones);

            if (clip->isAnalogPitchActive()) {
                double oldFactor = std::pow(2.0, old / 12.0);
                double newFactor = std::pow(2.0, clip->pitchChange / 12.0);
                clip->length =
                    juce::jmax(ClipInfo::MIN_CLIP_LENGTH, clip->length * (oldFactor / newFactor));
                clip->speedRatio = newFactor;
            }

            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setTranspose(ClipId clipId, int semitones) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->transpose = juce::jlimit(-24, 24, semitones);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Beat Detection
// ============================================================================

void ClipManager::setAutoDetectBeats(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->autoDetectBeats = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setBeatSensitivity(ClipId clipId, float sensitivity) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->beatSensitivity = juce::jlimit(0.0f, 1.0f, sensitivity);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Playback
// ============================================================================

void ClipManager::setIsReversed(ClipId clipId, bool reversed) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->isReversed = reversed;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Groove/Shuffle/Swing
// ============================================================================

void ClipManager::setGrooveTemplate(ClipId clipId, const juce::String& templateName) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI) {
            clip->grooveTemplate = templateName;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setGrooveStrength(ClipId clipId, float strength) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI) {
            clip->grooveStrength = juce::jlimit(0.0f, 1.0f, strength);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Per-Clip Mix
// ============================================================================

void ClipManager::setClipVolumeDB(ClipId clipId, float dB) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->volumeDB = juce::jlimit(-100.0f, 0.0f, dB);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setClipGainDB(ClipId clipId, float dB) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->gainDB = juce::jlimit(0.0f, 24.0f, dB);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setClipPan(ClipId clipId, float pan) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->pan = juce::jlimit(-1.0f, 1.0f, pan);
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Fades
// ============================================================================

void ClipManager::setFadeIn(ClipId clipId, double seconds) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->fadeIn = juce::jmax(0.0, seconds);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOut(ClipId clipId, double seconds) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->fadeOut = juce::jmax(0.0, seconds);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeInType(ClipId clipId, int type) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->fadeInType = juce::jlimit(1, 4, type);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOutType(ClipId clipId, int type) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->fadeOutType = juce::jlimit(1, 4, type);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeInBehaviour(ClipId clipId, int behaviour) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->fadeInBehaviour = juce::jlimit(0, 1, behaviour);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setFadeOutBehaviour(ClipId clipId, int behaviour) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->fadeOutBehaviour = juce::jlimit(0, 1, behaviour);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setAutoCrossfade(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->autoCrossfade = enabled;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Channels
// ============================================================================

void ClipManager::setLeftChannelActive(ClipId clipId, bool active) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->leftChannelActive = active;
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::setRightChannelActive(ClipId clipId, bool active) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            clip->rightChannelActive = active;
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Per-Clip Grid Settings
// ============================================================================

void ClipManager::setClipGridSettings(ClipId clipId, bool autoGrid, int numerator,
                                      int denominator) {
    if (auto* clip = getClip(clipId)) {
        clip->gridAutoGrid = autoGrid;
        clip->gridNumerator = numerator;
        clip->gridDenominator = denominator;
        notifyClipPropertyChanged(clipId);
    }
}

void ClipManager::setClipSnapEnabled(ClipId clipId, bool enabled) {
    if (auto* clip = getClip(clipId)) {
        clip->gridSnapEnabled = enabled;
        notifyClipPropertyChanged(clipId);
    }
}

// ============================================================================
// Content-Level Operations (Editor Operations)
// ============================================================================

void ClipManager::trimAudioLeft(ClipId clipId, double trimAmount, double fileDuration) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            ClipOperations::trimAudioFromLeft(*clip, trimAmount, fileDuration);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::trimAudioRight(ClipId clipId, double trimAmount, double fileDuration) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            ClipOperations::trimAudioFromRight(*clip, trimAmount, fileDuration);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::stretchAudioLeft(ClipId clipId, double newLength, double oldLength,
                                   double originalSpeedRatio, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            ClipOperations::stretchAudioFromLeft(*clip, newLength, oldLength, originalSpeedRatio);
            if (bpm > 0.0)
                clip->startBeats = clip->startTime * bpm / 60.0;
            if (clip->isBeatsAuthoritative() && bpm > 0.0) {
                clip->lengthBeats = clip->length * bpm / 60.0;
            }
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::stretchAudioRight(ClipId clipId, double newLength, double oldLength,
                                    double originalSpeedRatio, double bpm) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::Audio) {
            ClipOperations::stretchAudioFromRight(*clip, newLength, oldLength, originalSpeedRatio);
            if (clip->isBeatsAuthoritative() && bpm > 0.0) {
                clip->lengthBeats = clip->length * bpm / 60.0;
            }
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::addMidiNote(ClipId clipId, const MidiNote& note) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI) {
            clip->midiNotes.push_back(note);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::removeMidiNote(ClipId clipId, int noteIndex) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI && noteIndex >= 0 &&
            noteIndex < static_cast<int>(clip->midiNotes.size())) {
            clip->midiNotes.erase(clip->midiNotes.begin() + noteIndex);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::clearMidiNotes(ClipId clipId) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI) {
            clip->midiNotes.clear();
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::addChordAnnotation(ClipId clipId, const ClipInfo::ChordAnnotation& annotation) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI) {
            clip->chordAnnotations.push_back(annotation);
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::removeChordAnnotation(ClipId clipId, size_t index) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI && index < clip->chordAnnotations.size()) {
            clip->chordAnnotations.erase(clip->chordAnnotations.begin() +
                                         static_cast<ptrdiff_t>(index));
            notifyClipPropertyChanged(clipId);
        }
    }
}

void ClipManager::clearChordAnnotations(ClipId clipId) {
    if (auto* clip = getClip(clipId)) {
        if (clip->type == ClipType::MIDI) {
            clip->chordAnnotations.clear();
            notifyClipPropertyChanged(clipId);
        }
    }
}

// ============================================================================
// Access
// ============================================================================

ClipInfo* ClipManager::getClip(ClipId clipId) {
    auto it = clips_.find(clipId);
    return (it != clips_.end()) ? &it->second : nullptr;
}

const ClipInfo* ClipManager::getClip(ClipId clipId) const {
    auto it = clips_.find(clipId);
    return (it != clips_.end()) ? &it->second : nullptr;
}

std::vector<ClipInfo> ClipManager::getArrangementClips() const {
    std::vector<ClipInfo> result;
    result.reserve(clips_.size());
    for (const auto& [id, clip] : clips_) {
        if (clip.view == ClipView::Arrangement)
            result.push_back(clip);
    }
    return result;
}

std::vector<ClipInfo> ClipManager::getSessionClips() const {
    std::vector<ClipInfo> result;
    result.reserve(clips_.size());
    for (const auto& [id, clip] : clips_) {
        if (clip.view == ClipView::Session)
            result.push_back(clip);
    }
    return result;
}

std::vector<ClipInfo> ClipManager::getClips() const {
    std::vector<ClipInfo> result;
    result.reserve(clips_.size());
    for (const auto& [id, clip] : clips_)
        result.push_back(clip);
    return result;
}

std::vector<ClipId> ClipManager::getClipsOnTrack(TrackId trackId) const {
    std::vector<ClipId> result;
    for (const auto& [id, clip] : clips_) {
        if (clip.trackId == trackId)
            result.push_back(clip.id);
    }
    std::sort(result.begin(), result.end(), [this](ClipId a, ClipId b) {
        const auto* clipA = getClip(a);
        const auto* clipB = getClip(b);
        return clipA && clipB && clipA->startTime < clipB->startTime;
    });
    return result;
}

std::vector<ClipId> ClipManager::getClipsOnTrack(TrackId trackId, ClipView view) const {
    std::vector<ClipId> result;
    for (const auto& [id, clip] : clips_) {
        if (clip.trackId == trackId && clip.view == view)
            result.push_back(clip.id);
    }
    if (view == ClipView::Arrangement) {
        std::sort(result.begin(), result.end(), [this](ClipId a, ClipId b) {
            const auto* clipA = getClip(a);
            const auto* clipB = getClip(b);
            return clipA && clipB && clipA->startTime < clipB->startTime;
        });
    }
    return result;
}

ClipId ClipManager::getClipAtPosition(TrackId trackId, double time) const {
    for (const auto& [id, clip] : clips_) {
        if (clip.view == ClipView::Arrangement && clip.trackId == trackId &&
            clip.containsTime(time)) {
            return clip.id;
        }
    }
    return INVALID_CLIP_ID;
}

std::vector<ClipId> ClipManager::getClipsInRange(TrackId trackId, double startTime,
                                                 double endTime) const {
    std::vector<ClipId> result;
    for (const auto& [id, clip] : clips_) {
        if (clip.view == ClipView::Arrangement && clip.trackId == trackId &&
            clip.overlaps(startTime, endTime)) {
            result.push_back(clip.id);
        }
    }
    return result;
}

// ============================================================================
// Selection
// ============================================================================

void ClipManager::setSelectedClip(ClipId clipId) {
    if (selectedClipId_ != clipId) {
        selectedClipId_ = clipId;
        notifyClipSelectionChanged(clipId);
    }
}

void ClipManager::clearClipSelection() {
    selectedClipId_ = INVALID_CLIP_ID;
    // Always notify so listeners can clear stale visual state
    // (e.g. ClipComponents still showing selected after multi-clip deselection)
    notifyClipSelectionChanged(INVALID_CLIP_ID);
}

// ============================================================================
// Session View (Clip Launcher)
// ============================================================================

ClipId ClipManager::getClipInSlot(TrackId trackId, int sceneIndex) const {
    for (const auto& [id, clip] : clips_) {
        if (clip.view == ClipView::Session && clip.trackId == trackId &&
            clip.sceneIndex == sceneIndex) {
            return clip.id;
        }
    }
    return INVALID_CLIP_ID;
}

void ClipManager::setClipSceneIndex(ClipId clipId, int sceneIndex) {
    if (auto* clip = getClip(clipId)) {
        clip->sceneIndex = sceneIndex;
        notifyClipsChanged();  // Structural change: old slot must also refresh
    }
}

void ClipManager::triggerClip(ClipId clipId) {
    if (auto* clip = getClip(clipId)) {
        // Remember the last triggered session clip so transport Record can
        // re-trigger it. Don't touch selectedClipId_ — that's for UI selection.
        if (clip->view == ClipView::Session) {
            lastTriggeredSessionClipId_ = clipId;
        }

        // Emit a play request — the scheduler handles toggle logic,
        // same-track exclusion, and all state management.
        notifyClipPlaybackRequested(clipId, ClipPlaybackRequest::Play);
    }
}

void ClipManager::stopClip(ClipId clipId) {
    if (getClip(clipId)) {
        notifyClipPlaybackRequested(clipId, ClipPlaybackRequest::Stop);
    }
}

void ClipManager::stopAllClips() {
    for (const auto& [id, clip] : clips_) {
        if (clip.view == ClipView::Session)
            notifyClipPlaybackRequested(clip.id, ClipPlaybackRequest::Stop);
    }
}

// ============================================================================
// Listener Management
// ============================================================================

void ClipManager::addListener(ClipManagerListener* listener) {
    if (listener && std::find(listeners_.begin(), listeners_.end(), listener) == listeners_.end()) {
        listeners_.push_back(listener);
    }
}

void ClipManager::removeListener(ClipManagerListener* listener) {
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

// ============================================================================
// Project Management
// ============================================================================

void ClipManager::clearAllClips() {
    clips_.clear();
    selectedClipId_ = INVALID_CLIP_ID;
    nextClipId_ = 1;
    notifyClipsChanged();
}

void ClipManager::createTestClips() {
    // Create random test clips on existing tracks for development
    auto& trackManager = TrackManager::getInstance();
    const auto& tracks = trackManager.getTracks();

    if (tracks.empty()) {
        return;
    }

    // Random number generator
    juce::Random random;

    for (const auto& track : tracks) {
        // Create 1-4 clips per track
        int numClips = random.nextInt({1, 4});
        double currentTime = random.nextFloat() * 2.0;  // Start within first 2 seconds

        for (int i = 0; i < numClips; ++i) {
            // Random clip length between 1 and 8 seconds
            double length = 1.0 + random.nextFloat() * 7.0;

            // Create MIDI clip in arrangement view (works on all track types for testing)
            createMidiClip(track.id, currentTime, length, ClipView::Arrangement);

            // Gap between clips (0 to 2 seconds)
            currentTime += length + random.nextFloat() * 2.0;
        }
    }
}

// ============================================================================
// Overlap Resolution
// ============================================================================

void ClipManager::resolveOverlaps(ClipId dominantClipId) {
    const auto* dominant = getClip(dominantClipId);
    if (!dominant || dominant->view != ClipView::Arrangement) {
        return;
    }

    double dStart = dominant->startTime;
    double dEnd = dominant->getEndTime();
    TrackId trackId = dominant->trackId;

    // Collect IDs to delete and clips to resize (avoid iterator invalidation)
    std::vector<ClipId> toDelete;

    struct ResizeOp {
        ClipId id;
        double newLength;
        bool fromLeft;  // true = trim left edge (move start forward)
    };
    std::vector<ResizeOp> toResize;

    for (const auto& [cid, clip] : clips_) {
        if (clip.view != ClipView::Arrangement || clip.id == dominantClipId ||
            clip.trackId != trackId) {
            continue;
        }

        double cStart = clip.startTime;
        double cEnd = clip.getEndTime();

        // Check for overlap
        if (cStart >= dEnd || cEnd <= dStart) {
            continue;  // No overlap
        }

        if (cStart >= dStart && cEnd <= dEnd) {
            // Case 1: C fully covered by D → delete
            toDelete.push_back(clip.id);
        } else if (cStart < dStart && cEnd > dStart && cEnd <= dEnd) {
            // Case 2: C overlaps from left → trim right edge to dStart
            toResize.push_back({clip.id, dStart - cStart, false});
        } else if (cStart >= dStart && cStart < dEnd && cEnd > dEnd) {
            // Case 3: C overlaps from right → trim left edge to dEnd
            toResize.push_back({clip.id, cEnd - dEnd, true});
        } else if (cStart < dStart && cEnd > dEnd) {
            // Case 4: C fully contains D → trim right edge to dStart (keep left portion)
            toResize.push_back({clip.id, dStart - cStart, false});
        }
    }

    // Apply deletions
    for (auto id : toDelete) {
        deleteClip(id);
    }

    // Apply resizes
    for (const auto& op : toResize) {
        if (auto* clip = getClip(op.id)) {
            if (op.fromLeft) {
                ClipOperations::resizeContainerFromLeft(*clip, op.newLength);
            } else {
                ClipOperations::resizeContainerFromRight(*clip, op.newLength);
            }
            notifyClipPropertyChanged(op.id);
        }
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

void ClipManager::notifyClipsChanged() {
    // Make a copy because listeners may be removed during iteration
    // (e.g., ClipComponent destroyed when TrackContentPanel rebuilds)
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipsChanged();
        }
    }
}

void ClipManager::notifyClipPropertyChanged(ClipId clipId) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipPropertyChanged(clipId);
        }
    }
}

void ClipManager::notifyClipSelectionChanged(ClipId clipId) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipSelectionChanged(clipId);
        }
    }
}

void ClipManager::notifyClipPlaybackStateChanged(ClipId clipId) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipPlaybackStateChanged(clipId);
        }
    }
}

void ClipManager::notifyClipPlaybackRequested(ClipId clipId, ClipPlaybackRequest request) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipPlaybackRequested(clipId, request);
        }
    }
}

void ClipManager::notifyClipDragPreview(ClipId clipId, double previewStartTime,
                                        double previewLength) {
    auto listenersCopy = listeners_;
    for (auto* listener : listenersCopy) {
        if (std::find(listeners_.begin(), listeners_.end(), listener) != listeners_.end()) {
            listener->clipDragPreview(clipId, previewStartTime, previewLength);
        }
    }
}

juce::String ClipManager::generateClipName(ClipType type) const {
    int count = 1;
    for (const auto& [id, clip] : clips_) {
        if (clip.type == type)
            count++;
    }

    if (type == ClipType::Audio) {
        return "Audio " + juce::String(count);
    } else {
        return "MIDI " + juce::String(count);
    }
}

void ClipManager::sanitizeAudioClip(ClipInfo& clip) {
    if (clip.type != ClipType::Audio || clip.audioFilePath.isEmpty())
        return;

    auto* thumbnail = AudioThumbnailManager::getInstance().getThumbnail(clip.audioFilePath);
    if (!thumbnail)
        return;

    double fileDuration = thumbnail->getTotalLength();
    if (fileDuration <= 0.0)
        return;

    // Clamp loopStart to file bounds
    clip.loopStart = juce::jlimit(0.0, fileDuration, clip.loopStart);

    // Clamp loopLength so loop region doesn't exceed file
    double availableFromLoop = fileDuration - clip.loopStart;
    if (clip.loopLength > availableFromLoop) {
        double oldLoopLength = clip.loopLength;
        clip.loopLength = juce::jmax(0.0, availableFromLoop);
        if (clip.autoTempo && oldLoopLength > 0.0) {
            clip.loopLengthBeats *= clip.loopLength / oldLoopLength;
        }
    }

    // Clamp offset to file bounds
    clip.offset = juce::jlimit(0.0, fileDuration, clip.offset);

    // Non-loop mode: keep loopStart synced to offset and clamp clip length
    if (!clip.loopEnabled) {
        clip.loopStart = clip.offset;
        clip.clampLengthToSource(fileDuration);
    }
}

// ============================================================================
// Clipboard Operations
// ============================================================================

void ClipManager::copyToClipboard(const std::unordered_set<ClipId>& clipIds) {
    clipboard_.clear();

    if (clipIds.empty()) {
        return;
    }

    // Find the earliest start time to use as reference
    clipboardReferenceTime_ = std::numeric_limits<double>::max();
    for (auto clipId : clipIds) {
        const auto* clip = getClip(clipId);
        if (clip) {
            clipboardReferenceTime_ = std::min(clipboardReferenceTime_, clip->startTime);
        }
    }

    // Copy clips maintaining relative positions
    for (auto clipId : clipIds) {
        const auto* clip = getClip(clipId);
        if (clip) {
            clipboard_.push_back(*clip);
        }
    }

    DBG("CLIPBOARD: Copied " << clipboard_.size() << " clip(s)");
}

void ClipManager::copyTimeRangeToClipboard(double startTime, double endTime,
                                           const std::vector<TrackId>& trackIds, double tempoBPM) {
    clipboard_.clear();
    clipboardReferenceTime_ = startTime;

    if (startTime >= endTime)
        return;

    for (const auto& [id, clip] : clips_) {
        if (clip.view != ClipView::Arrangement)
            continue;
        // Filter by track if trackIds is non-empty
        if (!trackIds.empty()) {
            if (std::find(trackIds.begin(), trackIds.end(), clip.trackId) == trackIds.end())
                continue;
        }

        // Check overlap
        double clipEnd = clip.startTime + clip.length;
        if (clip.startTime >= endTime || clipEnd <= startTime)
            continue;

        double overlapStart = std::max(clip.startTime, startTime);
        double overlapEnd = std::min(clipEnd, endTime);

        ClipInfo trimmed = clip;
        trimmed.length = overlapEnd - overlapStart;
        trimmed.startTime = overlapStart;

        if (clip.type == ClipType::Audio) {
            // Adjust offset for the trimmed start position
            double trimFromLeft = overlapStart - clip.startTime;
            if (clip.isBeatsAuthoritative() && clip.sourceBPM > 0.0 && tempoBPM > 0.0) {
                // autoTempo: work in beats, derive seconds
                double deltaBeats = trimFromLeft * tempoBPM / 60.0;
                trimmed.offsetBeats = clip.offsetBeats + deltaBeats;
                trimmed.offset = trimmed.offsetBeats * 60.0 / clip.sourceBPM;
            } else {
                trimmed.offset = clip.offset + trimFromLeft * clip.speedRatio;
            }
            // Sync loop fields for non-looped clips
            if (!trimmed.loopEnabled) {
                if (trimmed.autoTempo && trimmed.sourceBPM > 0.0) {
                    trimmed.loopStartBeats = trimmed.offsetBeats;
                    trimmed.loopStart = trimmed.loopStartBeats * 60.0 / trimmed.sourceBPM;
                } else {
                    trimmed.loopStart = trimmed.offset;
                }
                trimmed.loopLength = trimmed.timelineToSource(trimmed.length);
            }
        } else if (clip.type == ClipType::MIDI && !clip.midiNotes.empty()) {
            // Filter MIDI notes to those within the overlap range
            double bps = tempoBPM / 60.0;
            // Notes are in beats relative to clip start. Convert overlap bounds to beats.
            double overlapStartBeat = (overlapStart - clip.startTime) * bps;
            double overlapEndBeat = (overlapEnd - clip.startTime) * bps;

            std::vector<MidiNote> filteredNotes;
            for (const auto& note : clip.midiNotes) {
                if (note.startBeat >= overlapStartBeat && note.startBeat < overlapEndBeat) {
                    MidiNote adjusted = note;
                    adjusted.startBeat -= overlapStartBeat;
                    filteredNotes.push_back(adjusted);
                }
            }
            trimmed.midiNotes = filteredNotes;
        }

        clipboard_.push_back(trimmed);
    }
}

std::vector<ClipId> ClipManager::pasteFromClipboard(double pasteTime, TrackId targetTrackId,
                                                    ClipView targetView, int targetSceneIndex) {
    std::vector<ClipId> newClips;

    if (clipboard_.empty()) {
        return newClips;
    }

    // Calculate offset from reference time to paste time
    double timeOffset = pasteTime - clipboardReferenceTime_;

    // Track which scene slots have been used during this paste (for multi-clip session paste)
    std::unordered_map<TrackId, int> trackSceneMap;

    for (const auto& clipData : clipboard_) {
        // Calculate new start time maintaining relative position
        double newStartTime = clipData.startTime + timeOffset;

        // Determine target track
        TrackId newTrackId = (targetTrackId != INVALID_TRACK_ID) ? targetTrackId : clipData.trackId;

        // Create new clip based on type, using targetView instead of clipData.view
        ClipId newClipId = INVALID_CLIP_ID;
        if (clipData.type == ClipType::Audio) {
            if (clipData.audioFilePath.isNotEmpty()) {
                newClipId = createAudioClip(newTrackId, newStartTime, clipData.length,
                                            clipData.audioFilePath, targetView);
            }
        } else {
            // For MIDI clips, create empty then copy notes
            newClipId = createMidiClip(newTrackId, newStartTime, clipData.length, targetView);
        }

        if (newClipId != INVALID_CLIP_ID) {
            // Copy properties
            auto* newClip = getClip(newClipId);
            if (newClip) {
                newClip->name = clipData.name + " (copy)";
                newClip->colour = clipData.colour;
                newClip->loopEnabled = clipData.loopEnabled;

                // Copy MIDI data
                if (clipData.type == ClipType::MIDI) {
                    newClip->midiNotes = clipData.midiNotes;
                    newClip->midiOffset = clipData.midiOffset;
                    newClip->midiCCData = clipData.midiCCData;
                    newClip->midiPitchBendData = clipData.midiPitchBendData;
                }

                // Copy audio properties — but NOT when pasting arrangement→session,
                // because createAudioClip already set correct session defaults
                // (autoTempo, beat values, offset=0, loopStart=0).
                bool crossViewToSession =
                    (targetView == ClipView::Session && clipData.view == ClipView::Arrangement);

                if (clipData.type == ClipType::Audio && !crossViewToSession) {
                    newClip->offset = clipData.offset;
                    newClip->loopStart = clipData.loopStart;
                    newClip->loopLength = clipData.loopLength;
                }

                // Audio playback — preserve session defaults for cross-view paste
                if (!crossViewToSession) {
                    newClip->autoTempo = clipData.autoTempo;
                    newClip->loopStartBeats = clipData.loopStartBeats;
                    newClip->loopLengthBeats = clipData.loopLengthBeats;
                    newClip->lengthBeats = clipData.lengthBeats;
                    newClip->startBeats = clipData.startBeats;
                }
                if (!crossViewToSession) {
                    newClip->warpEnabled = clipData.warpEnabled;
                    newClip->timeStretchMode = clipData.timeStretchMode;
                }

                // Source file metadata (always copy — these describe the file itself)
                if (clipData.sourceBPM > 0.0)
                    newClip->sourceBPM = clipData.sourceBPM;
                if (clipData.sourceNumBeats > 0.0)
                    newClip->sourceNumBeats = clipData.sourceNumBeats;

                // Pitch
                newClip->autoPitch = clipData.autoPitch;
                newClip->analogPitch = clipData.analogPitch;
                newClip->pitchChange = clipData.pitchChange;
                newClip->transpose = clipData.transpose;

                // Mix
                newClip->volumeDB = clipData.volumeDB;
                newClip->gainDB = clipData.gainDB;
                newClip->pan = clipData.pan;

                // Playback
                newClip->isReversed = clipData.isReversed;
                if (!crossViewToSession)
                    newClip->speedRatio = clipData.speedRatio;

                // Channels
                newClip->leftChannelActive = clipData.leftChannelActive;
                newClip->rightChannelActive = clipData.rightChannelActive;

                // Grid settings
                newClip->gridAutoGrid = clipData.gridAutoGrid;
                newClip->gridNumerator = clipData.gridNumerator;
                newClip->gridDenominator = clipData.gridDenominator;
                newClip->gridSnapEnabled = clipData.gridSnapEnabled;

                // Cross-view translation: pasting into session view
                if (targetView == ClipView::Session && targetSceneIndex >= 0) {
                    // Find next empty slot for this track
                    if (trackSceneMap.find(newTrackId) == trackSceneMap.end()) {
                        trackSceneMap[newTrackId] = targetSceneIndex;
                    }
                    int sceneForThisClip = trackSceneMap[newTrackId];
                    while (getClipInSlot(newTrackId, sceneForThisClip) != INVALID_CLIP_ID) {
                        sceneForThisClip++;
                    }
                    newClip->sceneIndex = sceneForThisClip;
                    trackSceneMap[newTrackId] = sceneForThisClip + 1;
                    newClip->loopEnabled = true;
                    newClip->launchMode = clipData.launchMode;
                    newClip->launchQuantize = clipData.launchQuantize;

                    if (!crossViewToSession) {
                        // Reset extended loops to base loop length for
                        // session→session pastes
                        if (clipData.loopEnabled && clipData.loopLengthBeats > 0.0 &&
                            clipData.lengthBeats > clipData.loopLengthBeats) {
                            newClip->lengthBeats = clipData.loopLengthBeats;
                            newClip->loopLengthBeats = clipData.loopLengthBeats;
                            // Derive time-length from beat ratio
                            if (clipData.lengthBeats > 0.0) {
                                double ratio = clipData.loopLengthBeats / clipData.lengthBeats;
                                newClip->length = clipData.length * ratio;
                                newClip->loopLength = newClip->length;
                            }
                        } else if (clipData.loopEnabled && clipData.loopLength > 0.0 &&
                                   clipData.length >
                                       clipData.sourceToTimeline(clipData.loopLength)) {
                            newClip->length = clipData.sourceToTimeline(clipData.loopLength);
                            newClip->loopLength = clipData.loopLength;
                        }
                    }
                }

                forceNotifyClipPropertyChanged(newClipId);
            }

            // resolveOverlaps already called by createAudioClip/createMidiClip
            newClips.push_back(newClipId);
        }
    }

    return newClips;
}

void ClipManager::cutToClipboard(const std::unordered_set<ClipId>& clipIds) {
    // Copy to clipboard
    copyToClipboard(clipIds);

    // Delete original clips
    for (auto clipId : clipIds) {
        deleteClip(clipId);
    }
}

bool ClipManager::hasClipsInClipboard() const {
    return !clipboard_.empty();
}

void ClipManager::clearClipboard() {
    clipboard_.clear();
    clipboardReferenceTime_ = 0.0;
}

// ============================================================================
// Note Clipboard Operations
// ============================================================================

void ClipManager::copyNotesToClipboard(ClipId clipId, const std::vector<size_t>& noteIndices) {
    noteClipboard_.clear();
    noteClipboardMinBeat_ = 0.0;

    const auto* clip = getClip(clipId);
    if (!clip || clip->type != ClipType::MIDI || noteIndices.empty()) {
        return;
    }

    // Copy selected notes
    double minBeat = std::numeric_limits<double>::max();
    for (size_t idx : noteIndices) {
        if (idx < clip->midiNotes.size()) {
            noteClipboard_.push_back(clip->midiNotes[idx]);
            minBeat = std::min(minBeat, clip->midiNotes[idx].startBeat);
        }
    }

    if (noteClipboard_.empty()) {
        return;
    }

    // Store original earliest beat and normalise
    noteClipboardMinBeat_ = minBeat;
    for (auto& note : noteClipboard_) {
        note.startBeat -= minBeat;
    }
}

bool ClipManager::hasNotesInClipboard() const {
    return !noteClipboard_.empty();
}

const std::vector<MidiNote>& ClipManager::getNoteClipboard() const {
    return noteClipboard_;
}

double ClipManager::getNoteClipboardMinBeat() const {
    return noteClipboardMinBeat_;
}

void ClipManager::setNoteClipboard(std::vector<MidiNote> notes) {
    noteClipboard_ = std::move(notes);
    noteClipboardMinBeat_ = 0.0;
    if (!noteClipboard_.empty()) {
        double minBeat = noteClipboard_.front().startBeat;
        for (const auto& n : noteClipboard_)
            minBeat = std::min(minBeat, n.startBeat);
        noteClipboardMinBeat_ = minBeat;
    }
}

}  // namespace magda
