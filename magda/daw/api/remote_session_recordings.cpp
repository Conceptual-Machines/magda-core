#include "remote_session_recordings.hpp"

#include <algorithm>
#include <functional>

#include "../core/ClipManager.hpp"
#include "clip_api.hpp"
#include "magda_api.hpp"
#include "session_api.hpp"
#include "transport_api.hpp"

namespace magda::remote {
namespace {

std::vector<ClipId> arrangementClipIds(MagdaApi& api) {
    std::vector<ClipId> result;
    for (const auto& clip : api.clips().getArrangementClips())
        result.push_back(clip.id);
    std::ranges::sort(result);
    return result;
}

void onMessageThread(std::function<void()> callback) {
    auto* messages = juce::MessageManager::getInstanceWithoutCreating();
    if (messages != nullptr && !messages->isThisTheMessageThread()) {
        juce::MessageManager::callAsync(std::move(callback));
        return;
    }
    callback();
}

}  // namespace

RemoteSessionRecordings::RemoteSessionRecordings(MagdaApi& api,
                                                 std::shared_ptr<RemoteJobManager> jobs,
                                                 std::shared_ptr<std::atomic<Revision>> revision)
    : api_(api), jobs_(std::move(jobs)), revision_(std::move(revision)) {}

RemoteSessionRecordings::~RemoteSessionRecordings() {
    shutdown();
}

RemoteSessionRecordings::Result RemoteSessionRecordings::beginSlot(TrackId trackId, SceneId sceneId,
                                                                   int sceneIndex,
                                                                   const RequestContext& context) {
    if (shutdown_)
        return {{}, Error{ErrorCode::Cancelled, "recording service is shut down", {}}, false};
    if (std::ranges::any_of(active_, [&](const Active& item) {
            return item.kind == Kind::Slot && item.trackId == trackId &&
                   item.sceneIndex == sceneIndex;
        }))
        return {{},
                Error{ErrorCode::Conflict, "the slot already has an active recording job", {}},
                false};

    const auto capabilities = api_.session().recordingCapabilities();
    if (!capabilities.slotRecording)
        return {{},
                Error{ErrorCode::ValidationFailed,
                      "this engine does not support session slot recording",
                      {}},
                false};
    if (api_.session().getClipInSlot(trackId, sceneIndex) != INVALID_CLIP_ID)
        return {
            {}, Error{ErrorCode::Conflict, "the addressed session slot is occupied", {}}, false};

    const auto cancellable = capabilities.slotCancellation;
    const auto weak = weak_from_this();
    const auto jobId =
        jobs_->accept({.kind = "session.slotRecording",
                       .ownerClientId = context.clientId,
                       .requiredScope = Scope::Session,
                       .acceptedRevision = context.revision,
                       .revisionPolicy = JobRevisionPolicy::CheckAtStartOnly,
                       .projectBound = true,
                       .cancellable = cancellable},
                      RemoteJobManager::CancelCallback{[weak, trackId, sceneIndex, cancellable] {
                          onMessageThread([weak, trackId, sceneIndex, cancellable] {
                              if (const auto owner = weak.lock())
                                  owner->releaseSlot(trackId, sceneIndex, cancellable);
                          });
                      }});
    if (jobId.isEmpty())
        return {
            {}, Error{ErrorCode::InternalError, "slot recording job was not accepted", {}}, false};

    active_.push_back(
        {jobId, context.clientId, Kind::Slot, trackId, sceneId, sceneIndex, {}, false});
    if (!api_.session().setSlotRecordArmed(trackId, sceneIndex, true) ||
        !api_.session().beginSlotRecording(trackId, sceneIndex)) {
        if (capabilities.slotCancellation)
            api_.session().stopSlotRecording(trackId, sceneIndex, false);
        else
            api_.session().setSlotRecordArmed(trackId, sceneIndex, false);
        jobs_->fail(jobId, {ErrorCode::Conflict, "the slot could not begin recording", {}},
                    context.revision);
        active_.pop_back();
        return {{}, Error{ErrorCode::Conflict, "the slot could not begin recording", {}}, false};
    }
    api_.transport().setRecording(true);
    jobs_->markRunning(jobId);

    Error error;
    return {jobs_->get(jobId, context.clientId, context.scopes, error), std::nullopt, false};
}

RemoteSessionRecordings::Active* RemoteSessionRecordings::owned(const juce::String& jobId,
                                                                const RequestContext& context,
                                                                Error& error) {
    auto found = std::ranges::find(active_, jobId, &Active::jobId);
    if (found == active_.end() || found->ownerClientId != context.clientId) {
        error = {ErrorCode::NotFound, "recording job not found", {}};
        return nullptr;
    }
    return &*found;
}

std::optional<RemoteJobDto> RemoteSessionRecordings::job(const Active& active,
                                                         const RequestContext& context,
                                                         Error& error) {
    return jobs_->get(active.jobId, context.clientId, context.scopes, error);
}

RemoteSessionRecordings::Result RemoteSessionRecordings::stopSlot(const juce::String& jobId,
                                                                  const RequestContext& context) {
    Error error;
    auto* active = owned(jobId, context, error);
    if (active == nullptr)
        return {{}, error, false};
    if (active->kind != Kind::Slot)
        return {{}, Error{ErrorCode::Conflict, "job is not a slot recording", {}}, false};
    if (active->stopping)
        return {job(*active, context, error), std::nullopt, false};

    const auto before = api_.session().getClipInSlot(active->trackId, active->sceneIndex);
    ClipManager::BatchScope notificationBatch;
    if (!api_.session().stopSlotRecording(active->trackId, active->sceneIndex, true))
        return {
            {}, Error{ErrorCode::Conflict, "the slot recording could not be stopped", {}}, false};
    active->stopping = true;
    if (api_.session().recordingCapabilities().slotStopStopsTransport)
        for (auto& item : active_)
            item.stopping = true;
    pollSoon();
    const auto after = api_.session().getClipInSlot(active->trackId, active->sceneIndex);
    const auto changed = before == INVALID_CLIP_ID && after != INVALID_CLIP_ID;
    return {job(*active, context, error), std::nullopt, changed};
}

RemoteSessionRecordings::Result RemoteSessionRecordings::beginPerformance(
    const RequestContext& context) {
    if (shutdown_)
        return {{}, Error{ErrorCode::Cancelled, "recording service is shut down", {}}, false};
    if (std::ranges::any_of(active_,
                            [](const Active& item) { return item.kind == Kind::Performance; }))
        return {
            {}, Error{ErrorCode::Conflict, "a performance capture is already active", {}}, false};
    const auto capabilities = api_.session().recordingCapabilities();
    if (!capabilities.performanceCapture)
        return {{},
                Error{ErrorCode::ValidationFailed,
                      "this engine does not support session performance capture",
                      {}},
                false};

    const auto weak = weak_from_this();
    auto id = std::make_shared<juce::String>();
    const auto jobId = jobs_->accept({.kind = "session.performanceCapture",
                                      .ownerClientId = context.clientId,
                                      .requiredScope = Scope::Session,
                                      .acceptedRevision = context.revision,
                                      .revisionPolicy = JobRevisionPolicy::CheckAtStartOnly,
                                      .projectBound = true,
                                      .cancellable = capabilities.performanceCaptureCancellation},
                                     [weak, id] {
                                         onMessageThread([weak, id] {
                                             if (const auto owner = weak.lock())
                                                 owner->releasePerformance(*id);
                                         });
                                     });
    if (jobId.isEmpty())
        return {{},
                Error{ErrorCode::InternalError, "performance capture job was not accepted", {}},
                false};

    *id = jobId;
    active_.push_back({jobId, context.clientId, Kind::Performance, INVALID_TRACK_ID,
                       INVALID_SCENE_ID, -1, arrangementClipIds(api_)});
    api_.transport().setRecording(true);
    if (!api_.transport().isRecording()) {
        jobs_->fail(jobId, {ErrorCode::Conflict, "the transport could not begin recording", {}},
                    context.revision);
        active_.pop_back();
        return {
            {}, Error{ErrorCode::Conflict, "the transport could not begin recording", {}}, false};
    }
    jobs_->markRunning(jobId);
    Error error;
    return {jobs_->get(jobId, context.clientId, context.scopes, error), std::nullopt, false};
}

RemoteSessionRecordings::Result RemoteSessionRecordings::stopPerformance(
    const juce::String& jobId, const RequestContext& context) {
    Error error;
    auto* active = owned(jobId, context, error);
    if (active == nullptr)
        return {{}, error, false};
    if (active->kind != Kind::Performance)
        return {{}, Error{ErrorCode::Conflict, "job is not a performance capture", {}}, false};
    if (active->stopping)
        return {job(*active, context, error), std::nullopt, false};

    const auto before = arrangementClipIds(api_);
    ClipManager::BatchScope notificationBatch;
    api_.transport().setRecording(false);
    active->stopping = true;
    for (auto& item : active_)
        if (item.kind == Kind::Slot)
            item.stopping = true;
    pollSoon();
    const auto after = arrangementClipIds(api_);
    return {job(*active, context, error), std::nullopt, before != after};
}

void RemoteSessionRecordings::poll() {
    if (shutdown_)
        return;
    const auto revision = revision_->load(std::memory_order_acquire);
    for (auto it = active_.begin(); it != active_.end();) {
        if (it->kind == Kind::Slot) {
            const auto clipId = api_.session().getClipInSlot(it->trackId, it->sceneIndex);
            if (!it->stopping && (api_.session().isSlotRecordArmed(it->trackId, it->sceneIndex) ||
                                  api_.session().isSlotRecording(it->trackId, it->sceneIndex))) {
                ++it;
                continue;
            }
            if (clipId == INVALID_CLIP_ID &&
                (api_.session().isSlotRecordArmed(it->trackId, it->sceneIndex) ||
                 api_.session().isSlotRecording(it->trackId, it->sceneIndex))) {
                ++it;
                continue;
            }
            jobs_->complete(it->jobId, slotResult(*it, clipId), revision);
        } else {
            if (!it->stopping && api_.transport().isRecording()) {
                ++it;
                continue;
            }
            jobs_->complete(it->jobId, performanceResult(*it), revision);
        }
        it = active_.erase(it);
    }
}

void RemoteSessionRecordings::pollSoon() {
    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        return;
    const auto weak = weak_from_this();
    juce::MessageManager::callAsync([weak] {
        if (const auto owner = weak.lock())
            owner->poll();
    });
}

void RemoteSessionRecordings::releaseSlot(TrackId trackId, int sceneIndex, bool discard) {
    api_.session().stopSlotRecording(trackId, sceneIndex, !discard);
    std::erase_if(active_, [&](const Active& item) {
        return item.kind == Kind::Slot && item.trackId == trackId && item.sceneIndex == sceneIndex;
    });
}

void RemoteSessionRecordings::releasePerformance(const juce::String& jobId) {
    api_.transport().setRecording(false);
    std::erase_if(active_, [&](const Active& item) { return item.jobId == jobId; });
}

juce::var RemoteSessionRecordings::slotResult(const Active& active, ClipId clipId) {
    auto* result = new juce::DynamicObject();
    result->setProperty("kind", "session_slot_recording");
    result->setProperty("state", "completed");
    result->setProperty("trackId", active.trackId);
    result->setProperty("sceneId", active.sceneId);
    result->setProperty("sceneIndex", active.sceneIndex);
    result->setProperty("clipId", clipId == INVALID_CLIP_ID ? juce::var() : juce::var(clipId));
    return result;
}

juce::var RemoteSessionRecordings::performanceResult(const Active& active) {
    const auto after = arrangementClipIds(api_);
    juce::Array<juce::var> created;
    for (const auto id : after)
        if (!std::ranges::binary_search(active.arrangementBefore, id))
            created.add(id);
    auto* result = new juce::DynamicObject();
    result->setProperty("kind", "session_performance_capture");
    result->setProperty("state", "completed");
    result->setProperty("clipIds", created);
    return result;
}

void RemoteSessionRecordings::shutdown() {
    if (shutdown_)
        return;
    shutdown_ = true;
    active_.clear();
}

}  // namespace magda::remote
