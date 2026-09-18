#include "SessionArrangementCapture.hpp"

#include <juce_core/juce_core.h>

#include <ranges>
#include <tuple>
#include <utility>

#include "../../core/ClipManager.hpp"
#include "../../core/TrackManager.hpp"
#include "../../project/serialization/ProjectSerializer.hpp"
#include "clip/ClipSnapshotCompiler.hpp"
#include "exec/EngineSession.hpp"
#include "launch/SessionCapture.hpp"

namespace magda::daw::engine_host {

SessionArrangementCapture::SessionArrangementCapture() = default;
SessionArrangementCapture::~SessionArrangementCapture() = default;

bool SessionArrangementCapture::SourceKey::operator<(const SourceKey& other) const {
    return std::tie(slot, incarnation, source.clipId, source.revision) <
           std::tie(other.slot, other.incarnation, other.source.clipId, other.source.revision);
}

void SessionArrangementCapture::attach(engine::EngineSession& session) {
    reset();
    session_ = &session;
    capture_ = std::make_unique<engine::SessionCapture>(session.slotRuns());
}

void SessionArrangementCapture::prepare(std::vector<engine::ClipLane>& lanes, double sourceTempo) {
    std::set<ClipId> present;
    for (const auto& clip : ClipManager::getInstance().getSessionClips()) {
        present.insert(clip.id);
        const auto fingerprint =
            (juce::JSON::toString(ProjectSerializer::serializeClipInfo(clip), true, 17) + "@" +
             juce::String(sourceTempo, 17))
                .toStdString();
        const auto previous = fingerprints_.find(clip.id);
        if (previous == fingerprints_.end()) {
            fingerprints_[clip.id] = fingerprint;
            revisions_[clip.id] = ++nextRevision_;
        } else if (previous->second != fingerprint) {
            previous->second = fingerprint;
            revisions_[clip.id] = ++nextRevision_;
        }
        revisionTempos_[clip.id] = sourceTempo;

        const auto lane = std::ranges::find(lanes, clip.trackId, &engine::ClipLane::trackId);
        if (lane != lanes.end())
            lane->captureRevisions[clip.id] = revisions_.at(clip.id);
    }

    const auto deleted = [&present](const auto& entry) { return !present.contains(entry.first); };
    std::erase_if(fingerprints_, deleted);
    std::erase_if(revisions_, deleted);
    std::erase_if(revisionTempos_, deleted);
}

bool SessionArrangementCapture::published() {
    if (session_ == nullptr || capture_ == nullptr)
        return false;

    current_.clear();
    auto& clips = ClipManager::getInstance();
    for (const auto& clip : clips.getSessionClips()) {
        const engine::SlotKey slot{clip.trackId, clip.sceneIndex};
        const auto target = session_->slotRunTarget(slot);
        if (!target)
            continue;

        const engine::CaptureSource source{clip.id, revisions_.at(clip.id)};
        const SourceKey key{slot, target->incarnation, source};
        current_.insert(key);
        sources_.try_emplace(key,
                             SourceSnapshot{.clip = clip, .tempo = revisionTempos_.at(clip.id)});
    }

    return update();
}

bool SessionArrangementCapture::hasMaterial() const {
    return !current_.empty();
}

bool SessionArrangementCapture::arm() {
    if (capture_ == nullptr || !hasMaterial())
        return false;

    capture_->armFromCurrent();
    reportOverflows();
    return true;
}

bool SessionArrangementCapture::update(bool createClips) {
    if (session_ == nullptr || capture_ == nullptr)
        return false;

    for (const auto& retired : session_->takeRetiredRuns())
        capture_->apply(retired);
    capture_->update();
    reportOverflows();
    return collect(createClips);
}

bool SessionArrangementCapture::disarm(bool createClips) {
    if (session_ == nullptr || capture_ == nullptr)
        return false;

    for (const auto& retired : session_->takeRetiredRuns())
        capture_->apply(retired);
    capture_->disarm();
    reportOverflows();
    return collect(createClips);
}

void SessionArrangementCapture::reset() {
    capture_.reset();
    session_ = nullptr;
    sources_.clear();
    current_.clear();
    fingerprints_.clear();
    revisions_.clear();
    revisionTempos_.clear();
    nextRevision_ = 0;
    reportedOverflows_ = 0;
}

bool SessionArrangementCapture::armed() const {
    return capture_ != nullptr && capture_->armed();
}

bool SessionArrangementCapture::collect(bool createClips) {
    bool created = false;
    for (const auto& run : capture_->collect()) {
        if (!createClips)
            continue;

        const auto source = sources_.find({run.key, run.incarnation, run.source});
        if (source == sources_.end()) {
            juce::Logger::writeToLog("[engine] Session capture lost source for track " +
                                     juce::String(run.key.trackId) + ", scene " +
                                     juce::String(run.key.sceneIndex));
            continue;
        }
        if (TrackManager::getInstance().getTrack(run.key.trackId) == nullptr)
            continue;

        created |= ClipManager::getInstance().createCapturedSessionClip(
                       source->second.clip, run.startBeat, run.lengthBeats, run.offsetBeats,
                       ClipOverlapPolicy::ResolveOverlaps, source->second.tempo) != INVALID_CLIP_ID;
    }
    pruneSources();
    return created;
}

void SessionArrangementCapture::pruneSources() {
    auto retained = current_;
    for (const auto& active : capture_->activeSources())
        retained.insert({active.key, active.incarnation, active.source});

    std::erase_if(sources_,
                  [&retained](const auto& entry) { return !retained.contains(entry.first); });
}

void SessionArrangementCapture::reportOverflows() {
    if (session_ == nullptr)
        return;

    const auto overflows = session_->slotRuns().overflows();
    if (overflows == reportedOverflows_)
        return;

    juce::Logger::writeToLog("[engine] Session capture lost " +
                             juce::String(overflows - reportedOverflows_) +
                             " run events to queue overflow");
    reportedOverflows_ = overflows;
}

}  // namespace magda::daw::engine_host
