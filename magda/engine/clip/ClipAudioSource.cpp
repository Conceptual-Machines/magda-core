#include "clip/ClipAudioSource.hpp"

#include <algorithm>
#include <optional>
#include <tuple>
#include <vector>

#include "clip/SessionPlayback.hpp"

namespace magda::engine {

namespace {

/// Whether a span reaches into the block at all. Half-open at both ends, so a
/// clip ending exactly on a block boundary contributes nothing to the block
/// that starts there.
bool reachesInto(const SnapshotSpan& span, const BlockInfo& block) {
    return span.startSeconds < block.endSeconds && span.endSeconds > block.startSeconds;
}

}  // namespace

ClipAudioSource::ClipAudioSource(TrackId trackId, ClipSnapshotFeed& clips, ClipStreamFeed& streams)
    : trackId_(trackId), clips_(clips), streams_(streams) {}

ClipAudioSource::ClipAudioSource(TrackId trackId, ClipSnapshotFeed& clips, ClipStreamFeed& streams,
                                 LaunchHandleFeed& handles)
    : trackId_(trackId), clips_(clips), streams_(streams), handles_(&handles) {}

void ClipAudioSource::prepare(const RenderContext& context) {
    // Longer than a block, because a clip playing faster than its file consumes
    // more reading than it renders and both live in here (ClipStretcher.hpp).
    scratch_.setSize(context.numChannels, stretchScratchSamples(context.maxBlockSize), false, true,
                     false);
    scratch_.clear();

    for (auto& voice : voices_)
        voice.prepare(context);
}

ClipVoice* ClipAudioSource::voiceFor(ClipId clipId, EventId eventId) {
    for (auto& voice : voices_)
        if (voice.playing(clipId, eventId))
            return &voice;

    for (auto& voice : voices_)
        if (voice.clipId() == INVALID_CLIP_ID)
            return &voice;

    return nullptr;
}

void ClipAudioSource::gather(const std::vector<AudioClipPlayback>& clips, const BlockInfo& block,
                             int outOffset, const Streams& streams,
                             std::array<Sounding, kMaxVoicesPerTrack>& sounding,
                             int& soundingCount) {
    for (const auto& clip : clips) {
        // Sorted by where they start (ClipSnapshot.hpp), so once one begins at
        // or after the end of this block, so does everything behind it. Without
        // the break a track pays for its whole tail on every callback, all
        // session, and the cost grows with the length of the arrangement rather
        // than with what is playing.
        if (clip.span.startSeconds >= block.endSeconds)
            break;

        if (!reachesInto(clip.span, block))
            continue;

        for (const auto& event : clip.events) {
            if (!reachesInto(event.span, block))
                continue;

            const auto* found =
                std::find_if(streams.first, streams.last, [&](const ClipStreamTable::Entry& entry) {
                    return entry.clipId == clip.clipId && entry.eventId == event.eventId;
                });

            if (found == streams.last || soundingCount == kMaxVoicesPerTrack) {
                // No reader standing by, or no voice left to play it through.
                // Both happen and they are different failures: a track may hold
                // more readers than a callback has voices (kMaxReadersPerTrack
                // is the larger, because a reader has to exist before its clip
                // is due), so a block can be handed more entries than it can
                // render. Either way the clip does not sound and the count is
                // what says so.
                starved_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            sounding[static_cast<std::size_t>(soundingCount++)] = Sounding{
                &clip, &event,   found->stream.get(), found->stretcher.get(), found->preRollSamples,
                block, outOffset};
        }
    }
}

void ClipAudioSource::gatherSession(const TrackClipPlayback& track, const BlockInfo& block,
                                    const Streams& streams,
                                    std::array<Sounding, kMaxVoicesPerTrack>& sounding,
                                    int& soundingCount) {
    const LaunchHandleFeed::Reader handles(*handles_);
    if (!handles)
        return;

    // The status is the launcher's, worked out before anything rendered
    // (SessionLauncher.hpp). Reading it rather than advancing the handle is
    // what lets this source and the MIDI one see the same block.
    forEachSlot(
        *handles.get(), track, [&](const SessionSlotPlayback& slot, const SplitStatus& status) {
            const auto play = [&](const BeatRange& range, const std::optional<double>& playStart,
                                  const std::optional<double>& playStartSeconds, int offset,
                                  int count) {
                if (count <= 0 || !playStart || !playStartSeconds)
                    return;

                gather(slot.audio,
                       materialSubBlock(block, range, *playStart, *playStartSeconds, count), offset,
                       streams, sounding, soundingCount);
            };

            // Where the launch or the stop landed. What is on the far side of
            // it is simply not rendered: the ramp across that boundary, and the
            // one between a track's session and its arrangement, are #2302's.
            const auto split = splitSample(block, status);

            if (status.playing1)
                play(status.range1, status.playStartTime1, status.playStartSeconds1, 0, split);

            if (status.isSplit && status.playing2)
                play(status.range2, status.playStartTime2, status.playStartSeconds2, split,
                     block.numSamples - split);
        });
}

void ClipAudioSource::render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) {
    out.clear();

    const ClipStreamFeed::Reader streams(streams_);
    Streams table;
    if (streams)
        std::tie(table.first, table.last) = streams->rangeFor(trackId_);

    // Every stream this track has, sounding or not, once per block. The stream
    // a cue is most useful to is one nobody is reading from: a clip that has
    // not started would otherwise not hear about the position it was pointed at
    // until the material was already due (#2016).
    for (const auto* entry = table.first; entry != table.last; ++entry)
        entry->stream->applyPendingCue();

    const auto silence = [this] {
        for (auto& voice : voices_)
            voice.release();
    };

    // Held to what the caller actually provided as well as to what the block
    // claims. The session hands its voices sub-blocks of this one, so a length
    // the buffer does not have is not a short block, it is somebody else's
    // memory.
    auto lane = block;
    lane.numSamples = std::min(block.numSamples, static_cast<int>(out.getNumSamples()));

    if (!block.playing || lane.numSamples <= 0)
        return silence();

    const ClipSnapshotFeed::Reader snapshot(clips_);
    if (!snapshot)
        return silence();

    const auto* track = snapshot->find(trackId_);
    if (track == nullptr)
        return silence();

    // What sounds, and through what. Gathered before anything is rendered so
    // the voices that are not in it can be let go first: a voice still holding
    // a clip that stopped last block would otherwise keep a slot a clip
    // starting this one needs.
    std::array<Sounding, kMaxVoicesPerTrack> sounding;
    auto soundingCount = 0;

    if (handles_ == nullptr)
        gather(track->audio, lane, 0, table, sounding, soundingCount);
    else
        gatherSession(*track, lane, table, sounding, soundingCount);

    for (auto& voice : voices_) {
        if (voice.clipId() == INVALID_CLIP_ID)
            continue;

        const auto stillSounding =
            std::any_of(sounding.begin(), sounding.begin() + soundingCount, [&](const Sounding& s) {
                return voice.playing(s.clip->clipId, s.event->eventId);
            });

        if (!stillSounding)
            voice.release();
    }

    // The whole of it, not this block's length: the voice renders into the front
    // and reads into what is behind, and where the second part starts is fixed
    // by the largest block rather than by this one.
    auto scratch = juce::dsp::AudioBlock<float>(scratch_);

    for (auto index = 0; index < soundingCount; ++index) {
        const auto& entry = sounding[static_cast<std::size_t>(index)];

        auto* voice = voiceFor(entry.clip->clipId, entry.event->eventId);
        if (voice == nullptr) {
            // Unreachable while the gather above is capped at the voice count,
            // and counted rather than asserted because the two numbers being
            // the same is a property of this file rather than of the type.
            starved_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        voice->render(*entry.clip, *entry.event, entry.block, *entry.stream, entry.stretcher,
                      entry.preRoll, scratch,
                      out.getSubBlock(static_cast<std::size_t>(entry.outOffset),
                                      static_cast<std::size_t>(entry.block.numSamples)));
    }
}

}  // namespace magda::engine
