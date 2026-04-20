#include "ControllerRouter.hpp"

#include "../core/aliases/AliasRegistry.hpp"
#include "../core/aliases/ChainContext.hpp"
#include "../core/aliases/ResolverRegistry.hpp"
#include "../core/aliases/TargetResolver.hpp"
#include "../core/controllers/BindingTransform.hpp"

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
                                                    << "' enabled=" << (c.enabled ? "yes" : "no"));
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
    // Filter by controller port before dispatching. Matching accepts either the
    // stored identifier or the display name (see magda::midi::matches).
    bool isController =
        ControllerRegistry::getInstance().isControllerInputPort(deviceId, deviceName);
    if (msg.isController() || msg.isNoteOnOrOff() || msg.isPitchWheel()) {
        DBG("ControllerRouter: raw midi from '"
            << deviceId << "' (name='" << deviceName
            << "') isController=" << (isController ? "yes" : "no") << " " << msg.getDescription());
    }
    if (isController)
        onMidiFromControllerPort(deviceId, deviceName, msg);
}

// ============================================================================
// MIDI processing (called from MIDI thread)
// ============================================================================

void ControllerRouter::onMidiFromControllerPort(const juce::String& portId,
                                                const juce::String& portName,
                                                const juce::MidiMessage& msg) {
    // Only handle messages from registered controller ports
    if (!ControllerRegistry::getInstance().isControllerInputPort(portId, portName))
        return;

    // Find the controller for this port (accepts either identifier or name).
    auto controllerOpt = ControllerRegistry::getInstance().findByInputPort(portId);
    if (!controllerOpt.has_value())
        controllerOpt = ControllerRegistry::getInstance().findByInputPort(portName);
    if (!controllerOpt.has_value() || !controllerOpt->enabled)
        return;

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

    // Look up bindings (lock-free snapshot read)
    auto bindings =
        BindingRegistry::getInstance().findForSource(controllerOpt->id, msgType, channel, number);

    DBG("ControllerRouter: port='" << portId << "' controller='" << controllerOpt->name
                                   << "' ch=" << channel << " num=" << number << " raw=" << rawValue
                                   << " -> " << bindings.size() << " binding(s) matched");

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

    DBG("ControllerRouter: WRITE value=" << finalValue << " to " << resolved.sourceLabel);
    paramWriter_->write(resolved, finalValue);

    // Emit feedback
    if (feedbackSink_) {
        feedbackSink_->send(FeedbackEvent{bindingId, finalValue});
    }
}

}  // namespace magda
