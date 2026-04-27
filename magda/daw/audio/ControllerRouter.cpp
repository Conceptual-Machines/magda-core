#include "ControllerRouter.hpp"

#include <algorithm>

#include "../core/SelectionManager.hpp"
#include "../core/aliases/AliasRegistry.hpp"
#include "../core/aliases/ChainContext.hpp"
#include "../core/aliases/ResolverRegistry.hpp"
#include "../core/aliases/TargetResolver.hpp"
#include "../core/controllers/BindingTransform.hpp"
#include "MidiDeviceMatch.hpp"

namespace magda {

// ============================================================================
// Singleton
// ============================================================================

ControllerRouter& ControllerRouter::getInstance() {
    static ControllerRouter instance;
    return instance;
}

// ============================================================================
// Lifecycle
// ============================================================================

void ControllerRouter::reconfigure() {
    if (!configured_) {
        ControllerRegistry::getInstance().addListener(this);
        BindingRegistry::getInstance().addListener(this);
        configured_ = true;
    }
    DBG("ControllerRouter: reconfigure — "
        << ControllerRegistry::getInstance().all().size() << " controller(s), "
        << BindingRegistry::getInstance().bindings(BindingScope::Global).size()
        << " global binding(s), "
        << BindingRegistry::getInstance().bindings(BindingScope::Project).size()
        << " project binding(s)");
    for (const auto& c : ControllerRegistry::getInstance().all())
        DBG("ControllerRouter:   controller name='" << c.name << "' inputPort='" << c.inputPort
                                                    << "'");
}

void ControllerRouter::setMidiBridge(MidiBridge* bridge) {
    if (midiBridge_ == bridge)
        return;
    if (midiBridge_)
        midiBridge_->removeRawMidiListener(this);
    midiBridge_ = bridge;
    if (midiBridge_)
        midiBridge_->addRawMidiListener(this);
    DBG("ControllerRouter: MidiBridge " << (bridge ? "attached" : "detached"));
}

void ControllerRouter::shutdown() {
    setMidiBridge(nullptr);  // unsubscribe from MidiBridge
    if (configured_) {
        ControllerRegistry::getInstance().removeListener(this);
        BindingRegistry::getInstance().removeListener(this);
        configured_ = false;
    }
}

// ============================================================================
// Dependency injection
// ============================================================================

void ControllerRouter::setParamWriter(std::unique_ptr<ControllerParamWriter> writer) {
    paramWriter_ = std::move(writer);
}

void ControllerRouter::setFeedbackSink(std::unique_ptr<ControllerFeedbackSink> sink) {
    feedbackSink_ = std::move(sink);
}

// ============================================================================
// Registry listener callbacks
// ============================================================================

void ControllerRouter::controllerRegistryChanged() {
    // Future: could update port filter set here
}

void ControllerRouter::bindingRegistryChanged(BindingScope /*scope*/) {
    // Future: could clear per-binding runtime state for removed bindings
}

// ============================================================================
// MIDI Learn
// ============================================================================

void ControllerRouter::beginLearnSession(LearnSessionConfig cfg, LearnCallback onCaptured) {
    juce::ScopedLock sl(learnLock_);
    // Discard any existing session (active or completed but not yet cleaned up)
    learn_ = std::make_unique<LearnState>();
    learn_->cfg = cfg;
    learn_->cb = std::move(onCaptured);
    learn_->armedAtMs = juce::Time::currentTimeMillis();
    learn_->active.store(true, std::memory_order_release);
    DBG("ControllerRouter: learn session armed");
}

void ControllerRouter::cancelLearnSession() {
    juce::ScopedLock sl(learnLock_);
    if (learn_) {
        learn_->active.store(false, std::memory_order_release);
        learn_.reset();
        DBG("ControllerRouter: learn session cancelled");
    }
}

bool ControllerRouter::isLearning() const {
    juce::ScopedLock sl(learnLock_);
    return learn_ && learn_->active.load(std::memory_order_acquire);
}

// ============================================================================
// Injection entry point (test + Phase D bridge)
// ============================================================================

void ControllerRouter::injectMessageForTest(const juce::String& portId,
                                            const juce::MidiMessage& msg,
                                            const juce::String& portName) {
    onMidiFromControllerPort(portId, portName, msg);
}

// ============================================================================
// RawMidiListener implementation
// ============================================================================

void ControllerRouter::onRawMidi(const juce::String& deviceId, const juce::String& deviceName,
                                 const juce::MidiMessage& msg) {
    // Called on the MIDI callback thread.
    // Forward every MIDI message; dispatch decides whether a Learn session is
    // armed or a binding matches. ControllerRegistry is consulted only for
    // scripted-surface bindings; MIDI Learn bindings attach directly to a port.
    onMidiFromControllerPort(deviceId, deviceName, msg);
}

// ============================================================================
// MIDI processing (called from MIDI thread)
// ============================================================================

void ControllerRouter::onMidiFromControllerPort(const juce::String& portId,
                                                const juce::String& portName,
                                                const juce::MidiMessage& msg) {
    // ---- MIDI Learn intercept (checked before normal routing) ----
    {
        juce::ScopedLock sl(learnLock_);
        if (learn_ && learn_->active.load(std::memory_order_acquire)) {
            // Only capture qualifying message types
            const bool isNoteOn = msg.isNoteOn();
            const bool isNoteOff = msg.isNoteOff();
            const bool isCC = msg.isController();
            const bool isPW = msg.isPitchWheel();

            if (isNoteOff) {
                // Suppress Note-off within debounce window of the last Note-on
                juce::int64 nowMs = juce::Time::currentTimeMillis();
                if ((nowMs - learn_->lastNoteOnMs) < learn_->cfg.captureDebounceMs) {
                    return;  // drop this Note-off; stay armed
                }
                // Note-off outside debounce — let normal routing handle it
            } else if (isCC || isNoteOn || isPW) {
                // Build capture
                LearnCapture capture;
                capture.portId = portId;
                capture.portName = portName;
                capture.channel = msg.getChannel();

                // Try to find the controller; use a default ControllerId if unknown
                auto ctrlOpt = ControllerRegistry::getInstance().findByInputPort(portId);
                if (!ctrlOpt.has_value())
                    ctrlOpt = ControllerRegistry::getInstance().findByInputPort(portName);
                if (ctrlOpt.has_value())
                    capture.controllerId = ctrlOpt->id;

                if (isCC) {
                    capture.msgType = BindingMsgType::CC;
                    capture.number = msg.getControllerNumber();
                    capture.rawValue = msg.getControllerValue();
                } else if (isNoteOn) {
                    capture.msgType = BindingMsgType::Note;
                    capture.number = msg.getNoteNumber();
                    capture.rawValue = msg.getVelocity();
                    learn_->lastNoteOnMs = juce::Time::currentTimeMillis();
                } else {
                    capture.msgType = BindingMsgType::PitchBend;
                    capture.number = 0;
                    capture.rawValue = msg.getPitchWheelValue();
                }

                // Mark session as done and fire callback on message thread
                learn_->active.store(false, std::memory_order_release);
                LearnCallback cb = std::move(learn_->cb);
                learn_.reset();

                DBG("ControllerRouter: learn captured " << msg.getDescription());

                // Deliver on message thread (or synchronously if already there)
                auto* mm = juce::MessageManager::getInstanceWithoutCreating();
                if (mm == nullptr || mm->isThisTheMessageThread()) {
                    cb(capture);
                } else {
                    juce::MessageManager::callAsync(
                        [cb = std::move(cb), capture]() mutable { cb(capture); });
                }
                return;  // do NOT fall through to normal routing
            }
            // Non-qualifying messages (program change, etc.) — stay armed, fall through
        }
    }

    // Determine message type and raw value
    BindingMsgType msgType;
    int channel = msg.getChannel();  // 1..16
    int number = 0;
    int rawValue = 0;
    int rawMax = 127;

    if (msg.isController()) {
        msgType = BindingMsgType::CC;
        number = msg.getControllerNumber();
        rawValue = msg.getControllerValue();
    } else if (msg.isNoteOn() || msg.isNoteOff()) {
        msgType = BindingMsgType::Note;
        number = msg.getNoteNumber();
        rawValue = msg.isNoteOn() ? msg.getVelocity() : 0;
    } else if (msg.isPitchWheel()) {
        msgType = BindingMsgType::PitchBend;
        number = 0;
        rawValue = msg.getPitchWheelValue();
        rawMax = 16383;
    } else {
        return;  // other message types not handled
    }

    auto& bindingReg = BindingRegistry::getInstance();

    // Port-addressed bindings: MIDI Learn gestures attach directly to a live
    // port (display name or identifier) with no ControllerRegistry dependency.
    auto bindings = bindingReg.findForPort(portId, portName, msgType, channel, number);

    // Controller-addressed bindings: walk every registered controller whose
    // port matches and accumulate bindings keyed to its ControllerId. A single
    // hardware port can host multiple registry rows (e.g. an AI-generated
    // profile alongside the bundled one) and each row carries its own UUID,
    // so the bindings of every matching row need to fire — picking just the
    // "first match" silently drops the others. Each binding ID is only
    // dispatched once even if multiple lookups return it.
    auto& controllerReg = ControllerRegistry::getInstance();
    for (const auto& c : controllerReg.all()) {
        if (!magda::midi::matches(c.inputPort, portId, portName))
            continue;
        auto controllerBindings = bindingReg.findForSource(c.id, msgType, channel, number);
        for (const auto& cb : controllerBindings) {
            const bool alreadyIncluded =
                std::any_of(bindings.begin(), bindings.end(),
                            [&cb](const Binding& b) { return b.id == cb.id; });
            if (!alreadyIncluded)
                bindings.push_back(cb);
        }
    }

    // Preference rule: an explicit user-mapped binding (MIDI Learn) shadows
    // any focused-device-macro resolver binding (automap profile) matched by
    // the same MIDI event. Only the explicit target fires so the Learn override
    // wins over the profile default. Every StaticTarget (PluginParam,
    // DeviceMacro, ModParam) counts as explicit because Learn is the only
    // gesture that produces them; AliasRef is also explicit since
    // MidiLearnCoordinator prefers an alias whenever a canonical alias exists
    // for the param. ResolverRef bindings are profile-driven and do not count
    // as explicit.
    const bool hasExplicitOverride =
        std::any_of(bindings.begin(), bindings.end(), [](const Binding& b) {
            return std::holds_alternative<StaticTarget>(b.target) ||
                   std::holds_alternative<AliasRef>(b.target);
        });
    if (hasExplicitOverride) {
        bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                      [](const Binding& b) {
                                          if (auto* rr = std::get_if<ResolverRef>(&b.target))
                                              return rr->kind == "focused.macro";
                                          return false;
                                      }),
                       bindings.end());
    }

    for (const auto& binding : bindings) {
        // Always schedule with raw value; executeWrite handles all mode logic
        // on the message thread (where per-binding state lives).
        scheduleWrite(binding.id, rawValue, rawMax, binding);
    }
}

// ============================================================================
// Schedule + execute writes
// ============================================================================

void ControllerRouter::scheduleWrite(const BindingId& bindingId, int rawValue, int rawMax,
                                     const Binding& binding) {
    juce::String key = bindingId.toDashedString();

    // Get or create the PendingWrite slot
    auto pwIt = pendingWrites_.find(key);
    if (pwIt == pendingWrites_.end()) {
        pendingWrites_[key] = std::make_shared<PendingWrite>();
        pwIt = pendingWrites_.find(key);
    }

    auto& pw = pwIt->second;

    // Pack rawValue and rawMax into a float pair via atomic int pair.
    // We store rawValue in pendingRaw and rawMax in pendingRawMax.
    pw->pendingRaw.store(rawValue, std::memory_order_relaxed);
    pw->pendingRawMax.store(rawMax, std::memory_order_relaxed);

    // If a callAsync is already in flight, just update the value and return
    bool expected = false;
    if (!pw->inFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;

    auto pwShared = pwIt->second;
    Binding bindingCopy = binding;

    // If we're already on the message thread (or no message manager -- test context),
    // execute synchronously to avoid deadlock and allow tests to run without a
    // running message loop.
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr || mm->isThisTheMessageThread()) {
        pwShared->inFlight.store(false, std::memory_order_release);
        executeWrite(bindingCopy.id, rawValue, rawMax, bindingCopy);
        return;
    }

    juce::MessageManager::callAsync([this, pwShared, bindingCopy]() {
        int raw = pwShared->pendingRaw.load(std::memory_order_relaxed);
        int rawMax2 = pwShared->pendingRawMax.load(std::memory_order_relaxed);
        pwShared->inFlight.store(false, std::memory_order_release);
        executeWrite(bindingCopy.id, raw, rawMax2, bindingCopy);
    });
}

void ControllerRouter::executeWrite(const BindingId& bindingId, int rawValue, int rawMax,
                                    const Binding& binding) {
    // This runs on the message thread -- runtimeState_ access is safe here.
    juce::String key = bindingId.toDashedString();
    auto& state = runtimeState_[key];

    float finalValue;

    if (binding.mode == BindingMode::Toggle) {
        float toggled = applyToggle(rawValue, state.toggleState);
        float curved = applyCurve(binding.range.curve, toggled);
        finalValue = applyRange(binding.range, curved);
    } else {
        TransformInput input{rawValue, rawMax};
        auto out = applyMode(binding.mode, input);

        if (out.isAbsolute) {
            float curved = applyCurve(binding.range.curve, out.value);
            finalValue = applyRange(binding.range, curved);
        } else {
            // Relative: accumulate on message thread
            state.accumulator = juce::jlimit(0.0f, 1.0f, state.accumulator + out.value);
            float curved = applyCurve(binding.range.curve, state.accumulator);
            finalValue = applyRange(binding.range, curved);
        }
    }

    // Resolve target and write
    if (!paramWriter_) {
        DBG("ControllerRouter: no paramWriter_ set — dropping write for binding "
            << bindingId.toDashedString());
        return;
    }

    DefaultChainContext ctx;
    TargetResolver resolver{AliasRegistry::getInstance(), ResolverRegistry::getInstance(), ctx};
    auto resolved = resolver.resolve(binding.target);

    if (!resolved.ok()) {
        DBG("ControllerRouter: target resolve FAILED (" << resolved.sourceLabel << ") for binding "
                                                        << bindingId.toDashedString());
        return;
    }

    // Contextual firing: if the resolved target is pinned to a specific track,
    // the binding only fires while that track is the selected track. Targets
    // without a track pin (master, @focused.*, @selected.*) fire regardless.
    const TrackId targetTrackId = resolved.devicePath.trackId;
    if (targetTrackId != INVALID_TRACK_ID) {
        const TrackId selected = SelectionManager::getInstance().getSelectedTrack();
        if (selected != targetTrackId) {
            DBG("ControllerRouter: gated — binding target is on track "
                << targetTrackId << " but selected is " << selected);
            return;
        }
    }

    DBG("ControllerRouter: WRITE value=" << finalValue << " to " << resolved.sourceLabel);
    paramWriter_->write(resolved, finalValue);

    // Emit feedback
    if (feedbackSink_) {
        feedbackSink_->send(FeedbackEvent{bindingId, finalValue});
    }
}

}  // namespace magda
