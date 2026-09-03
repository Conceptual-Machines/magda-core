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
    return span.seconds.start < block.seconds.end && span.seconds.end > block.seconds.start;
}

}  // namespace

ClipAudioSource::ClipAudioSource(TrackId trackId, ClipSnapshotFeed& clips, ClipStreamFeed& streams)
    : trackId_(trackId), clips_(clips), streams_(streams) {}

ClipAudioSource::ClipAudioSource(TrackId trackId, ClipSnapshotFeed& clips, ClipStreamFeed& streams,
                                 LaunchHandleFeed& handles, Section section)
    : trackId_(trackId), clips_(clips), streams_(streams), handles_(&handles), section_(section) {}

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

    // A voice still carrying a release ramp holds no entry and yet is still
    // sounding, so claiming it would cut the tail it is in the middle of.
    for (auto& voice : voices_)
        if (voice.clipId() == INVALID_CLIP_ID && !voice.fading())
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
        if (clip.span.seconds.start >= block.seconds.end)
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
    // (SessionLauncher.hpp), so this source and the MIDI one see the same block.
    const auto renderSlot = [&](const SessionSlotPlayback& slot, const SplitStatus& status) {
        const auto play = [&](const BlockPiece& piece, int offset, int count) {
            if (count <= 0 || !piece.origin)
                return;

            const auto material =
                materialSubBlock(block, piece.range, *piece.origin, offset, count);
            gather(slot.audio, material, offset, streams, sounding, soundingCount);
        };

        const auto split = splitSample(block, status).value;

        play(status.beforeEvent, 0, split);

        if (status.afterEvent)
            play(*status.afterEvent, split, block.numSamples - split);

        // Where this slot stopped sounding, noted per clip rather than per
        // track: the ramp is the voice's own (ClipVoice::releaseInto), so
        // whether some other slot goes on sounding through the same samples
        // does not come into it. The handle's answer rather than the shape of
        // the split, so a stop on the block's first sample is the same edge as
        // one half way through it (LaunchHandle::silencedAt).
        const auto silenced = status.silencedAt();
        if (!silenced)
            return;

        for (const auto& clip : slot.audio)
            for (const auto& event : clip.events) {
                if (stoppingCount_ >= kMaxVoicesPerTrack)
                    return;

                stopping_[static_cast<std::size_t>(stoppingCount_++)] =
                    Stopping{clip.clipId, event.eventId, silenced->value};
            }
    };

    forEachSlot(*handles.get(), track, renderSlot);
}

void ClipAudioSource::render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) {
    // Cleared for the block rather than by whatever gathers, because every path
    // through the render can return before the gather does: a stale edge would
    // release a voice a second time, blocks after the stop it belongs to.
    stoppingCount_ = 0;

    renderMaterial(block, out);

    // Only a source that shares its track with a session has a section to lose
    // it to. One built without the feed is the whole track.
    if (section_ == Section::Arrangement && handles_ != nullptr)
        applySectionHold(out);
}

void ClipAudioSource::applySectionHold(juce::dsp::AudioBlock<float> out) {
    const auto numSamples = static_cast<int>(out.getNumSamples());

    const LaunchHandleFeed::Reader handles(*handles_);
    const auto hold = handles ? sectionHold(*handles.get(), trackId_, numSamples) : SectionHold{};

    const auto until = std::clamp(hold.until.value, 0, numSamples);

    // What it rendered and gets to keep, before anything corrects it, which is
    // the order StopDeClick::push asks for.
    if (until > 0)
        handOver_.push(out.getSubBlock(0, static_cast<std::size_t>(until)));

    // The session's share of the block is dropped. What the arrangement
    // rendered into it still advanced every voice, stream and stretcher, which
    // is what makes taking the track back land where the timeline says rather
    // than where the arrangement was when it lost it. Only the output goes.
    if (until < numSamples)
        out.getSubBlock(static_cast<std::size_t>(until)).clear();

    // Taking the track back lands mid-material, which is the same step a voice
    // starting mid-file leaves and comes out the same way.
    if (hold.gained)
        handBack_.begin(out, kSectionDeClickSamples);
    else
        handBack_.advance(out);

    // Losing it leaves the other step. A ramp still running from an earlier
    // block finishes into whatever is here now rather than being cut off, which
    // would be the click it exists to remove.
    if (hold.lost)
        handOver_.begin(out, until, kSectionDeClickSamples);
    else
        handOver_.advance(out);
}

void ClipAudioSource::renderMaterial(const BlockInfo& block, juce::dsp::AudioBlock<float> out) {
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

    // Held to what the caller provided as well as to what the block claims: the
    // session hands its voices sub-blocks of this one.
    auto lane = block;
    lane.numSamples = std::min(block.numSamples, static_cast<int>(out.getNumSamples()));

    if (!block.playing || lane.numSamples <= 0)
        return silence();

    const ClipSnapshotFeed::Reader snapshot(clips_);
    if (!snapshot)
        return silence();

    // Compiled against a tempo map that has since changed: every second in it
    // is wrong by however much the map moved (ClipSnapshot.hpp).
    //
    // Counted, and then played anyway. Not because playing it is right, but
    // because the alternatives are worse in front of an audience: silence is a
    // hole in the middle of a set, and the stale spans usually stop overlapping
    // the block anyway, so refusing them mostly turns an accidental gap into a
    // deliberate one. Re-deriving the seconds from the beats, which do survive
    // a tempo edit, would be compiling on the audio thread, which is the one
    // thing a snapshot exists to have already done.
    //
    // The count is the point. Zero is the only right answer and reaching it is
    // the publish's job: the map and the snapshot compiled for it are meant to
    // swap together, and this says when they did not (#2337).
    //
    // Coarser than the question it stands in for, and knowingly. The
    // fingerprint is the whole map, while an arrangement clip's seconds depend
    // on the map at its own placement and a session slot's depend on it only
    // over beats zero to its length, because a slot compiles at the origin
    // (ClipSnapshotCompiler.cpp). A tempo change at bar 200 moves neither a
    // slot nor a clip before it, and still changes the fingerprint. Which is
    // another reason this counts rather than acts.
    if (block.tempo != nullptr && snapshot->tempoFingerprint != block.tempo->fingerprint())
        staleSnapshots_.fetch_add(1, std::memory_order_relaxed);

    const auto* track = snapshot->find(trackId_);
    if (track == nullptr)
        return silence();

    // What sounds, and through what. Gathered before anything is rendered so
    // the voices that are not in it can be let go first: a voice still holding
    // a clip that stopped last block would otherwise keep a slot a clip
    // starting this one needs.
    std::array<Sounding, kMaxVoicesPerTrack> sounding;
    auto soundingCount = 0;

    if (section_ == Section::Session)
        gatherSession(*track, lane, table, sounding, soundingCount);
    else
        gather(track->audio, lane, 0, table, sounding, soundingCount);

    // Pointers rather than the array's own iterators, which are a class type on
    // MSVC and a pointer on libc++: deducing `const auto*` from one compiles on
    // exactly one of the two.
    const auto stopping = [&](const ClipVoice& voice) -> const Stopping* {
        const auto* first = stopping_.data();
        const auto* last = first + stoppingCount_;

        const auto* found = std::find_if(
            first, last, [&](const Stopping& s) { return voice.playing(s.clipId, s.eventId); });

        return found != last ? found : nullptr;
    };

    for (auto& voice : voices_) {
        // A release ramp from an earlier block, which sounds whether or not
        // anything replaced the voice that left it.
        voice.carryTail(out);

        if (voice.clipId() == INVALID_CLIP_ID)
            continue;

        const auto stillSounding =
            std::any_of(sounding.begin(), sounding.begin() + soundingCount, [&](const Sounding& s) {
                return voice.playing(s.clip->clipId, s.event->eventId);
            });

        // Let go before anything renders, so a voice holding a clip that
        // stopped last block does not keep a slot a clip starting this one
        // needs. Not one the launcher says is stopping in this block: that one
        // is released below, once it has rendered whatever it still had to.
        if (!stillSounding && stopping(voice) == nullptr)
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

    // After rendering, because a slot that stops half way through a block still
    // sounds the first half of it, and the ramp carries on from what that half
    // ended at. A slot stopped on the block's first sample rendered nothing
    // here and carries on from the block before, which is the same thing said
    // about a different sample (StopDeClick::push).
    for (auto& voice : voices_) {
        if (voice.clipId() == INVALID_CLIP_ID)
            continue;

        if (const auto* stopped = stopping(voice))
            voice.releaseInto(out, stopped->offset, kSectionDeClickSamples);
    }
}

}  // namespace magda::engine
