#include "DeviceControl.hpp"

#include <mutex>
#include <utility>

#include "EngineExternalDevice.hpp"

namespace magda::daw::audio::engine_adapter {

namespace {

/// Which device a key names, said the way a person reads it.
juce::String describeKey(magda::engine::DeviceKey key) {
    return "device " + juce::String(static_cast<int>(key.deviceId)) + " in section " +
           juce::String(static_cast<int>(key.segment));
}

/// The capture itself, once @p device's accepted edits are fenced.
void captureFrom(EngineExternalDevice& device, magda::engine::DeviceKey key,
                 const DeviceControlPlane::CaptureCallback& completed) {
    auto snapshot = device.captureState();
    if (!snapshot.has_value()) {
        completed(CaptureOutcome::failed("the plugin bound for " + describeKey(key) +
                                         " could not describe itself"));
        return;
    }

    completed(CaptureOutcome::taken(std::move(*snapshot)));
}

}  // namespace

CaptureOutcome CaptureOutcome::taken(magda::ExternalPluginSnapshot snapshot) {
    CaptureOutcome outcome;
    outcome.snapshot_ = std::move(snapshot);
    return outcome;
}

CaptureOutcome CaptureOutcome::failed(juce::String reason) {
    CaptureOutcome outcome;

    // A failure always says something. One that did not would reach a person as
    // a save that did not happen and no reason it did not, which is the same
    // thing as saying nothing at all.
    outcome.failure_ = reason.isNotEmpty()
                           ? std::move(reason)
                           : juce::String("the capture failed for no stated reason");
    return outcome;
}

const magda::ExternalPluginSnapshot& CaptureOutcome::snapshot() const {
    jassert(ok());
    return *snapshot_;
}

DeviceControlPlane::DeviceControlPlane(std::shared_ptr<ControlExecutor> executor)
    : executor_(std::move(executor)) {
    // A plane with nowhere to run its work would answer nothing and say nothing
    // about why, which is the one failure this whole file is arranged against.
    jassert(executor_ != nullptr);
}

LocalDeviceControlPlane::Waiting::Waiting() {
    submitted.reserve(kMaxOutstandingEdits);
    taken.reserve(kMaxOutstandingEdits);
    edits.reserve(kMaxOutstandingEdits);
}

LocalDeviceControlPlane::LocalDeviceControlPlane(std::shared_ptr<ControlExecutor> executor,
                                                 std::weak_ptr<const DeviceRegistry> devices)
    : DeviceControlPlane(std::move(executor)),
      devices_(std::move(devices)),
      waiting_(std::make_shared<Waiting>()) {}

LocalDeviceControlPlane::~LocalDeviceControlPlane() {
    const auto answerAll = [waiting = waiting_](ExecutionState) {
        {
            const std::scoped_lock lock(waiting->submitLock);
            std::swap(waiting->submitted, waiting->taken);
        }

        for (auto& submitted : waiting->taken)
            submitted.completed({});

        for (auto& edit : waiting->edits)
            edit.completed({});

        waiting->taken.clear();
        waiting->edits.clear();
        waiting->outstanding.store(0, std::memory_order_release);
    };

    // On the executor, where a cancelled run lands too; a stopped one accepts
    // nothing, and then this thread owes the answers.
    if (!executor()->run(answerAll))
        answerAll(ExecutionState::Cancelled);
}

void LocalDeviceControlPlane::settleParameterEdits() {
    if (waiting_->outstanding.load(std::memory_order_acquire) == 0 ||
        waiting_->settleQueued.exchange(true, std::memory_order_acq_rel))
        return;

    executor()->run([waiting = waiting_, devices = devices_](ExecutionState state) {
        waiting->settleQueued.store(false, std::memory_order_release);
        settle(*waiting, devices, state == ExecutionState::Cancelled);
    });
}

void LocalDeviceControlPlane::pump(Waiting& waiting,
                                   const std::weak_ptr<const DeviceRegistry>& devices,
                                   bool closing) {
    {
        const std::scoped_lock lock(waiting.submitLock);
        std::swap(waiting.submitted, waiting.taken);
        waiting.pumpQueued = false;
    }

    const auto registry = closing ? nullptr : devices.lock();
    const auto answer = [&waiting](const EditCallback& completed, magda::EditCompletion done) {
        waiting.outstanding.fetch_sub(1, std::memory_order_acq_rel);
        completed(done);
    };

    for (auto& submitted : waiting.taken) {
        // Before the write rather than after: the same guard the loader puts
        // between a completion and the model it would write (#2270).
        const auto device = registry != nullptr && submitted.edit.request.isStillWanted()
                                ? registry->find(submitted.key)
                                : nullptr;
        if (device == nullptr) {
            answer(submitted.completed, {});
            continue;
        }

        const auto slot = submitted.edit.slot;
        const auto sequence = device->queueParameterEdit(slot, submitted.edit.normalised);
        if (!sequence.has_value()) {
            answer(submitted.completed, {.observed = device->readParameter(slot)});
            continue;
        }

        waiting.edits.push_back({.key = submitted.key,
                                 .slot = slot,
                                 .sequence = *sequence,
                                 .request = submitted.edit.request,
                                 .completed = std::move(submitted.completed)});
    }

    waiting.taken.clear();
    settle(waiting, devices, closing);
}

void LocalDeviceControlPlane::settle(Waiting& waiting,
                                     const std::weak_ptr<const DeviceRegistry>& devices,
                                     bool closing) {
    const auto registry = closing ? nullptr : devices.lock();

    std::erase_if(waiting.edits, [&registry, &waiting](WaitingEdit& edit) {
        const auto answer = [&waiting, &edit](magda::EditCompletion done) {
            waiting.outstanding.fetch_sub(1, std::memory_order_acq_rel);
            edit.completed(done);
            return true;
        };

        const auto device = registry != nullptr ? registry->find(edit.key) : nullptr;
        if (device == nullptr || !edit.request.isStillWanted())
            return answer({});

        // Nothing renders it any more, so nothing else will apply what it holds.
        if (!device->isRendered())
            device->pumpParameterEdits();

        switch (device->parameterEditState(edit.slot, edit.sequence)) {
            case EngineExternalDevice::EditState::Pending:
                return false;
            case EngineExternalDevice::EditState::Superseded:
                return answer({.superseded = true});
            case EngineExternalDevice::EditState::Refused:
                return answer({.observed = device->readParameter(edit.slot)});
            case EngineExternalDevice::EditState::Applied:
                return answer({.delivered = true, .observed = device->readParameter(edit.slot)});
        }

        return false;
    });
}

bool LocalDeviceControlPlane::captureState(magda::engine::DeviceKey key,
                                           CaptureCallback completed) {
    if (!completed || executor() == nullptr)
        return false;

    // Everything past here runs on the executor: the lookup, the plugin's own
    // state read, and the answer. Nothing checks which thread asked, because
    // asking is allowed from any of them -- what is not allowed is two of these
    // being inside one plugin at once, and one serial executor is what makes
    // that impossible for every control operation rather than for this one.
    //
    // Nothing about the plane is captured. The work outlives the call that
    // queued it, so it carries what it needs by value: the weak registry, which
    // is the devices and their owner in one reference rather than a token
    // standing beside them.
    return executor()->run([devices = devices_, waiting = waiting_, key,
                            completed](ExecutionState state) mutable {
        if (state == ExecutionState::Cancelled) {
            // Still on the executor, which is what a cancellation is worth: the
            // caller is told where it was expecting to be told, and the devices
            // are not reached for on the way.
            completed(CaptureOutcome::failed("the control plane closed before this capture ran"));
            return;
        }

        // Held for the length of the operation rather than checked at the start
        // of it, and it is the registry itself rather than a token beside it:
        // what keeps a device alive is the thing that owns it, so locking that
        // is what makes the pointer below safe to use until this returns.
        const auto registry = devices.lock();
        if (!registry) {
            completed(CaptureOutcome::failed("the runtime that owned " + describeKey(key) +
                                             " is gone, so nothing was read"));
            return;
        }

        // The lease, held for the rest of this operation. What it is worth is
        // that the device cannot go while the capture is reading it: not when
        // the chain it sits in is edited, and not when whatever owns it lets
        // go. The registry being alive was never enough to promise that.
        const auto device = registry->find(key);
        if (device == nullptr) {
            // Named rather than reported as an empty state. A key with nothing
            // bound to it is a slot whose plugin has not arrived or has gone,
            // and a caller told "no state" would write that absence into the
            // project.
            completed(CaptureOutcome::failed("no plugin is bound for " + describeKey(key)));
            return;
        }

        // Every edit accepted before this, applied and processed, or the chunk
        // can miss one reported as delivered (#2651).
        if (!device->fenceParameterEdits()) {
            completed(CaptureOutcome::failed("the plugin bound for " + describeKey(key) +
                                             " has edits it has not processed and could not "
                                             "be run to take them"));
            return;
        }

        settle(*waiting, devices, false);
        captureFrom(*device, key, completed);
    });
}

EditorOutcome EditorOutcome::showing(bool isShowing) {
    EditorOutcome outcome;
    outcome.showing_ = isShowing;
    return outcome;
}

EditorOutcome EditorOutcome::failed(juce::String reason) {
    EditorOutcome outcome;

    // Never empty, or ok() would read a failure as an answer.
    outcome.failure_ = reason.isNotEmpty()
                           ? std::move(reason)
                           : juce::String("the editor request failed for no stated reason");
    return outcome;
}

bool LocalDeviceControlPlane::applyState(magda::engine::DeviceKey key, magda::DeviceInfo saved,
                                         CaptureCallback completed) {
    if (!completed || executor() == nullptr)
        return false;

    // Same threading and lifetime rules as captureState().
    return executor()->run([devices = devices_, waiting = waiting_, key, saved = std::move(saved),
                            completed](ExecutionState state) mutable {
        if (state == ExecutionState::Cancelled) {
            completed(CaptureOutcome::failed("the control plane closed before this apply ran"));
            return;
        }

        const auto registry = devices.lock();
        if (!registry) {
            completed(CaptureOutcome::failed("the runtime that owned " + describeKey(key) +
                                             " is gone, so nothing was applied"));
            return;
        }

        const auto device = registry->find(key);
        if (device == nullptr) {
            completed(CaptureOutcome::failed("no plugin is bound for " + describeKey(key) +
                                             ", so there was nothing to apply it to"));
            return;
        }

        // A pending edit must not replay into the patch that replaces it.
        device->discardParameterEdits();
        settle(*waiting, devices, false);

        if (device->applyState(saved) == magda::SavedStateOutcome::Failed) {
            // setStateInformation() threw partway, so the plugin holds
            // neither the old nor the new state. Reading it back would record
            // that as the state somebody asked for.
            completed(CaptureOutcome::failed("the plugin bound for " + describeKey(key) +
                                             " could not take that patch"));
            return;
        }

        // A state chunk can change parameters the written array did not
        // list, so the model is updated from the plugin, not from the write.
        auto snapshot = device->captureState();
        if (!snapshot.has_value()) {
            completed(CaptureOutcome::failed("the plugin bound for " + describeKey(key) +
                                             " took the state and then would not describe "
                                             "itself"));
            return;
        }

        completed(CaptureOutcome::taken(std::move(*snapshot)));
    });
}

magda::EditStatus LocalDeviceControlPlane::editParameter(magda::engine::DeviceKey key,
                                                         ParameterEdit edit,
                                                         EditCallback completed) {
    if (!completed || executor() == nullptr)
        return magda::EditStatus::Closing;

    // Into a bounded batch with one pump queued for it, so a drag queues one
    // executor job however many moves it makes (#2651).
    std::size_t position = 0;
    {
        const std::scoped_lock lock(waiting_->submitLock);
        if (waiting_->outstanding.load(std::memory_order_acquire) >= kMaxOutstandingEdits)
            return magda::EditStatus::Busy;

        position = waiting_->submitted.size();
        waiting_->submitted.push_back(
            {.key = key, .edit = edit, .completed = std::move(completed)});
        waiting_->outstanding.fetch_add(1, std::memory_order_acq_rel);

        if (std::exchange(waiting_->pumpQueued, true))
            return magda::EditStatus::Accepted;
    }

    // On the executor like everything else here: a write must not land inside
    // a state read, which suspends the plugin around a chunk (#2268).
    const auto queued =
        executor()->run([waiting = waiting_, devices = devices_](ExecutionState state) {
            pump(*waiting, devices, state == ExecutionState::Cancelled);
        });
    if (queued)
        return magda::EditStatus::Accepted;

    // A stopped executor: this call's own edit is taken back, and whatever was
    // submitted behind it is answered by the destructor. No pump took the batch,
    // since this call is the one that would have queued it.
    const std::scoped_lock lock(waiting_->submitLock);
    waiting_->pumpQueued = false;
    waiting_->submitted.erase(waiting_->submitted.begin() + static_cast<std::ptrdiff_t>(position));
    waiting_->outstanding.fetch_sub(1, std::memory_order_acq_rel);
    return magda::EditStatus::Closing;
}

bool LocalDeviceControlPlane::editorWindow(magda::engine::DeviceKey key, EditorAction action,
                                           EditorCallback completed) {
    if (!completed || executor() == nullptr)
        return false;

    // Same rules as captureState(): an editor being built must not overlap a
    // state read on one plugin (#2268).
    return executor()->run([devices = devices_, key, action,
                            completed](ExecutionState state) mutable {
        if (state == ExecutionState::Cancelled) {
            completed(EditorOutcome::failed("the control plane closed before this ran"));
            return;
        }

        const auto registry = devices.lock();
        if (!registry) {
            completed(
                EditorOutcome::failed("the runtime that owned " + describeKey(key) + " is gone"));
            return;
        }

        const auto device = registry->find(key);
        if (device == nullptr) {
            completed(EditorOutcome::failed("no plugin is bound for " + describeKey(key)));
            return;
        }

        switch (action) {
            case EditorAction::Show:
                device->showEditor();
                break;
            case EditorAction::Hide:
                device->hideEditor();
                break;
            case EditorAction::Toggle:
                if (device->isEditorOpen())
                    device->hideEditor();
                else
                    device->showEditor();
                break;
            case EditorAction::Query:
                break;
        }

        // What it is now, whatever was asked.
        completed(EditorOutcome::showing(device->isEditorOpen()));
    });
}

bool commitCapturedState(const AssignmentRequest& request,
                         const magda::ExternalPluginSnapshot& snapshot,
                         const MutableDeviceLookup& device) {
    // The same question the other direction asks of a plugin that has finished
    // loading, and the same answer: a snapshot is only worth writing down onto
    // the assignment it was read from. A key that is live again under a
    // different assignment is not that, and neither is a runtime that has gone.
    if (!request.isStillWanted())
        return false;

    if (!device)
        return false;

    // Resolved from the token's own key, so the thing that was validated and
    // the thing that is written are the same device by construction.
    auto* target = device(request.key);
    if (target == nullptr)
        return false;

    magda::applyCapturedPluginState(*target, snapshot);
    return true;
}

}  // namespace magda::daw::audio::engine_adapter
