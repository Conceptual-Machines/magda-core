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
    // Whether anything is still sounding when the block ends, gathered in the
    // one walk that renders: a track with a slot still going has not stepped,
    // whatever its other slots did.
    auto stillSounding = false;

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

        // Where this slot stopped sounding, for the ramp that takes the step
        // out of it. A slot still sounding at the end of the block leaves no
        // step, and one that stopped in an earlier block has none left here.
        if (status.beforeEvent.playing() && !status.playingAtEnd())
            sessionSilentFrom_ = std::max(sessionSilentFrom_.value_or(0), split);

        stillSounding = stillSounding || status.playingAtEnd();
    };

    forEachSlot(*handles.get(), track, renderSlot);

    // The ramp is bound to the same grain as the signal it corrects, which is
    // the track: the op is per track because a track sounds one session clip at
    // a time, so a sum that did not step needs no correction.
    if (stillSounding)
        sessionSilentFrom_.reset();
}

void ClipAudioSource::render(const BlockInfo& block, juce::dsp::AudioBlock<float> out) {
    // Cleared for the block rather than by whatever gathers, because every path
    // through the render can return before the gather does: a stale edge would
    // de-click a second time, blocks after the stop it belongs to.
    sessionSilentFrom_.reset();

    renderMaterial(block, out);

    switch (section_) {
        case Section::Arrangement:
            // Only a source that shares its track with a session has a section
            // to lose it to. One built without the feed is the whole track.
            if (handles_ != nullptr)
                applySectionHold(out);
            break;

        case Section::Session:
            if (sessionSilentFrom_) {
                sessionStop_.begin(out, *sessionSilentFrom_, kSectionDeClickSamples);
            } else {
                sessionStop_.advance(out);

                // What sounded, remembered for a stop that lands on the first
                // sample of some later block.
                sessionStop_.push(out);
            }
            break;
    }
}

void ClipAudioSource::applySectionHold(juce::dsp::AudioBlock<float> out) {
    const LaunchHandleFeed::Reader handles(*handles_);
    const auto hold = handles ? sectionHold(*handles.get(), trackId_) : SectionHold{};

    if (!hold.session) {
        // The track is the arrangement's. Coming back to it lands mid-material,
        // which is the same step a voice starting mid-file leaves and comes out
        // the same way.
        if (wasHeld_)
            handBack_.begin(out, kSectionDeClickSamples);
        else
            handBack_.advance(out);

        // A hand-over taken back inside its own ramp finishes into the material
        // rather than being cut off, which would be the click it exists to
        // remove.
        handOver_.advance(out);
        handOver_.push(out);

        wasHeld_ = false;
        return;
    }

    // The session has the track. What the arrangement rendered still advanced
    // every voice, stream and stretcher, which is what makes handing the track
    // back land where the timeline says rather than where the arrangement was
    // when it lost it. Only the output is dropped.
    const auto numSamples = static_cast<int>(out.getNumSamples());
    const auto from = wasHeld_ ? 0 : std::clamp(hold.takenAt.value, 0, numSamples);

    out.getSubBlock(static_cast<std::size_t>(from)).clear();

    if (wasHeld_)
        handOver_.advance(out);
    else
        handOver_.begin(out, from, kSectionDeClickSamples);

    handBack_.reset();
    wasHeld_ = true;
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
