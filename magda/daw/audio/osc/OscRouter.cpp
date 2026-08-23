#include "osc/OscRouter.hpp"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <bit>
#include <cmath>

namespace magda::osc {

namespace {

/**
 * @brief What the message had to say, which is not the same as what it carried.
 *
 * Three outcomes, and the difference between the last two is load-bearing.
 * `Absent` is a bare message, which for a toggle *is* the request — flip
 * whatever the state is. `Unusable` is a message that carried something we
 * cannot read. Collapsing the two would let a surface speaking an unknown
 * dialect flip a mute by sending a word where a number belongs.
 */
enum class ArgumentState : std::uint8_t { Absent, Unusable, Present };

struct MessageArgument {
    ArgumentState state = ArgumentState::Absent;
    float value = 0.0f;
};

/**
 * @brief The message's first argument, if it is a number we can act on.
 *
 * Only the first is considered. Surfaces that send several — an XY pad, a
 * multi-touch fader bank — address each value separately in this namespace, so
 * a second argument means the sender is speaking a dialect we do not have a
 * mapping for, and guessing which of its numbers we wanted would be worse than
 * taking the one it put first.
 *
 * Non-finite values are rejected rather than passed on. A float32 argument is
 * free to be NaN or an infinity, `jlimit` propagates NaN rather than clamping
 * it, and this is an unauthenticated UDP port — so without this check a single
 * stray packet writes NaN into a fader, a pan, a macro, or the playhead.
 */
MessageArgument firstNumericArgument(const OscMessageView& message) {
    if (message.isEmpty())
        return {ArgumentState::Absent, 0.0f};

    const auto& argument = message[0];
    float value = 0.0f;
    if (argument.isFloat32())
        value = argument.getFloat32();
    else if (argument.isInt32())
        value = static_cast<float>(argument.getInt32());
    else
        return {ArgumentState::Unusable, 0.0f};

    if (!std::isfinite(value))
        return {ArgumentState::Unusable, 0.0f};
    return {ArgumentState::Present, value};
}

/// The value a binding takes from a message, if that argument is one we can
/// read. Bound sources are always continuous positions, so the argument
/// conventions of the fixed namespace — triggers, flips — do not apply here.
std::optional<float> boundArgument(const OscMessageView& message, int argIndex) {
    if (argIndex < 0 || argIndex >= message.size())
        return std::nullopt;

    const auto& argument = message[argIndex];
    float value = 0.0f;
    if (argument.isFloat32())
        value = argument.getFloat32();
    else if (argument.isInt32())
        value = static_cast<float>(argument.getInt32());
    else
        return std::nullopt;

    if (!std::isfinite(value))
        return std::nullopt;
    return juce::jlimit(0.0f, 1.0f, value);
}

}  // namespace

// ============================================================================
// OscBindingRoutes
// ============================================================================

std::shared_ptr<OscBindingRoutes> OscBindingRoutes::build(const std::vector<Binding>& bindings) {
    auto routes = std::make_shared<OscBindingRoutes>();

    for (const auto& binding : bindings) {
        if (!binding.source.isOsc() || !binding.isValid())
            continue;
        routes->entries.push_back(
            Entry{binding.source.oscAddress, binding.source.oscArgIndex, binding});
    }

    std::sort(routes->entries.begin(), routes->entries.end(),
              [](const Entry& a, const Entry& b) { return a.address < b.address; });

    const auto count = routes->entries.size();
    routes->values = std::make_unique<std::atomic<float>[]>(count);
    routes->peers = std::make_unique<std::atomic<OscPeerId>[]>(count);
    routes->dirtyWords = (count + 63) / 64;
    routes->dirty = std::make_unique<std::atomic<std::uint64_t>[]>(routes->dirtyWords);
    for (std::size_t i = 0; i < routes->dirtyWords; ++i)
        routes->dirty[i].store(0, std::memory_order_relaxed);

    return routes;
}

std::pair<std::size_t, std::size_t> OscBindingRoutes::findRange(juce::StringRef address) const {
    const juce::String wanted(address);
    auto first = std::lower_bound(
        entries.begin(), entries.end(), wanted,
        [](const Entry& entry, const juce::String& value) { return entry.address < value; });

    auto last = first;
    while (last != entries.end() && last->address == wanted)
        ++last;

    return {static_cast<std::size_t>(first - entries.begin()),
            static_cast<std::size_t>(last - entries.begin())};
}

// ============================================================================
// Argument extraction
// ============================================================================

std::optional<float> oscValueFor(const OscCommand& command, const OscMessageView& message) {
    const auto argument = firstNumericArgument(message);

    if (argument.state == ArgumentState::Unusable)
        return std::nullopt;

    switch (argKindFor(command.kind)) {
        case OscArgKind::Trigger:
            // A momentary button sends 1 on press and 0 on release. Acting on
            // the release would make holding Play stop the transport when the
            // finger came off.
            if (argument.state == ArgumentState::Present && argument.value == 0.0f)
                return std::nullopt;
            return 1.0f;

        case OscArgKind::Toggle:
            if (argument.state == ArgumentState::Absent)
                return kOscToggleRequest;
            return argument.value != 0.0f ? 1.0f : 0.0f;

        case OscArgKind::Normalized:
            if (argument.state != ArgumentState::Present)
                return std::nullopt;
            return juce::jlimit(0.0f, 1.0f, argument.value);

        case OscArgKind::Delta:
            // A distance, and a zero one is a no-op rather than a release
            // edge: nothing here is momentary. Sent bare it says nothing about
            // how far, so there is nothing to do.
            if (argument.state != ArgumentState::Present)
                return std::nullopt;
            if (argument.value == 0.0f || !std::isfinite(argument.value))
                return std::nullopt;
            return argument.value;

        case OscArgKind::Bpm:
        case OscArgKind::Beats:
            // Ranges belong to the model, which clamps against the project's
            // real limits; a tempo of 0 is not this layer's to reinterpret.
            if (argument.state != ArgumentState::Present)
                return std::nullopt;
            return argument.value;
    }
    return std::nullopt;
}

// ============================================================================
// OscRouter
// ============================================================================

OscRouter::OscRouter(std::unique_ptr<OscCommandSink> sink)
    : sink_(std::move(sink)), scheduler_([this]() { postDrain(); }) {
    jassert(sink_ != nullptr);
}

OscRouter::~OscRouter() {
    alive_->store(false, std::memory_order_release);
}

bool OscRouter::handleMessage(const OscMessageView& message, OscPeerId peer, Dispatch dispatch) {
    // The fixed namespace first, and it is not negotiable: `/magda/…` is a
    // documented contract, so a binding cannot take one of those addresses
    // over and leave a stock template mysteriously half-working. It also comes
    // before learn, so a control already carrying a meaning cannot be captured
    // as if it were free.
    if (const auto fixed = handleFixedNamespace(message, peer, dispatch); fixed.has_value())
        return *fixed;
    if (handleLearn(message, dispatch))
        return true;
    return handleBindings(message, peer, dispatch);
}

bool OscRouter::handleMessage(const juce::OSCMessage& message, OscPeerId peer, Dispatch dispatch) {
    // Named rather than inlined: the view reads the address through a
    // StringRef, and `toString` returns a temporary.
    const auto address = message.getAddressPattern().toString();
    return handleMessage(OscMessageView::fromOscMessage(address, message), peer, dispatch);
}

bool OscRouter::handleLearn(const OscMessageView& message, Dispatch dispatch) {
    // Keep the common path lock-free. Once armed, take the lock before touching
    // `learn_`: begin/cancel may replace that pointer from the message thread.
    if (!learning_.load(std::memory_order_acquire))
        return false;

    if (dispatch == Dispatch::Preflight) {
        // Would capture, without consuming the session. Reading `learning_`
        // without the lock is a race either way; the two outcomes are a peer
        // admitted that need not have been, and a learn gesture that arms
        // between the two passes waiting for the next message. Both are fine,
        // and neither can capture twice.
        for (int i = 0; i < message.size(); ++i)
            if (boundArgument(message, i))
                return true;
        return false;
    }

    OscLearnCapture capture;
    LearnCallback callback;
    {
        const juce::ScopedLock lock(learnLock_);
        if (!learning_.load(std::memory_order_relaxed) || learn_ == nullptr)
            return false;

        // The one string this path does build. A learn capture outlives the
        // datagram it came from — it travels to the message thread and becomes
        // a binding — so the address has to be copied rather than viewed.
        capture.address = juce::String(message.address());

        // The first argument we can read is the one the control is sending. A
        // surface that sends several down one address — an XY pad — is captured
        // on its first axis, and the other can be learned by moving it alone.
        bool found = false;
        for (int i = 0; i < message.size(); ++i) {
            if (const auto value = boundArgument(message, i)) {
                capture.argIndex = i;
                capture.value = *value;
                found = true;
                break;
            }
        }
        if (!found)
            return false;  // stay armed: this said nothing a binding could use

        learning_.store(false, std::memory_order_release);
        callback = std::move(learn_->callback);
        learn_.reset();
    }
    if (!callback)
        return false;

    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    if (manager == nullptr || manager->isThisTheMessageThread())
        callback(capture);
    else
        juce::MessageManager::callAsync(
            [callback = std::move(callback), capture]() mutable { callback(capture); });

    // Consumed, not routed: the gesture that teaches a control must not also
    // move whatever that control is bound to today.
    return true;
}

void OscRouter::beginLearnSession(LearnCallback onCaptured) {
    const juce::ScopedLock lock(learnLock_);
    learn_ = std::make_unique<LearnState>();
    learn_->callback = std::move(onCaptured);
    learning_.store(true, std::memory_order_release);
}

void OscRouter::cancelLearnSession() {
    const juce::ScopedLock lock(learnLock_);
    learning_.store(false, std::memory_order_release);
    learn_.reset();
}

bool OscRouter::isLearning() const {
    return learning_.load(std::memory_order_acquire);
}

std::optional<bool> OscRouter::handleFixedNamespace(const OscMessageView& message, OscPeerId peer,
                                                    Dispatch dispatch) {
    const auto command = parseOscAddress(message.address());
    if (!command.has_value())
        return std::nullopt;

    const auto value = oscValueFor(*command, message);
    if (!value.has_value())
        // The payload is unusable, but the address is still reserved. Returning
        // a present false prevents it from leaking into learn or bindings.
        return false;

    // Everything above is a question about the message; everything below acts on
    // it. The line between them is what `Preflight` means, and the ordered ring
    // is why it has to be here rather than one call up: a coalescing slot
    // forgives being written twice, a toggle pushed twice cancels itself and a
    // delta pushed twice moves twice as far.
    if (dispatch == Dispatch::Preflight)
        return true;

    accepted_.fetch_add(1, std::memory_order_relaxed);
    submit(*command, *value, peer);
    return true;
}

bool OscRouter::handleBindings(const OscMessageView& message, OscPeerId peer, Dispatch dispatch) {
    // A reference-count bump, not an allocation, and it pins the snapshot for
    // as long as we are reading and writing through it — which is what makes
    // an index and its value belong to the same list.
    auto routes = std::atomic_load(&bindingRoutes_);
    if (routes == nullptr || routes->entries.empty())
        return false;

    const auto [first, last] = routes->findRange(message.address());
    if (first == last)
        return false;

    bool accepted = false;
    for (std::size_t i = first; i < last; ++i) {
        const auto value = boundArgument(message, routes->entries[i].argIndex);
        if (!value.has_value())
            continue;  // this binding wanted an argument the message did not carry

        if (dispatch == Dispatch::Preflight)
            return true;

        // Same publish order as the fixed namespace: value and peer, then the
        // bit that advertises them.
        routes->values[i].store(*value, std::memory_order_relaxed);
        routes->peers[i].store(peer, std::memory_order_relaxed);
        routes->dirty[i / 64].fetch_or(1ULL << (i % 64), std::memory_order_release);
        accepted = true;
    }

    if (!accepted)
        return false;

    accepted_.fetch_add(1, std::memory_order_relaxed);
    scheduleDrain();
    return true;
}

void OscRouter::updateBindings(const std::vector<Binding>& bindings) {
    std::atomic_store(&bindingRoutes_, OscBindingRoutes::build(bindings));
}

void OscRouter::submit(const OscCommand& command, float value, OscPeerId peer) {
    const auto argKind = argKindFor(command.kind);
    if (argKind == OscArgKind::Trigger || argKind == OscArgKind::Toggle ||
        argKind == OscArgKind::Delta) {
        // An edge, not a value. Two flips of one toggle have to stay two, and
        // play and stop have to resolve by arrival rather than by slot number.
        //
        // A relative seek is the same shape for a different reason: it says
        // how far to move rather than where to be, so two presses of a rewind
        // button are two bars and coalescing them into one slot would silently
        // make them one. It also has to land relative to the position a locate
        // in the same bundle just set, which arrival order gives it and a slot
        // walk does not.
        pushOrdered(command, value, peer);
        scheduleDrain();
        return;
    }

    const int slot = oscSlotIndex(command);
    jassert(slot >= 0 && slot < kOscSlotCount);

    // Value and peer first, then the bit that advertises them: a drain that
    // observes the bit is guaranteed to observe at least this value, and a
    // store it misses leaves the bit set for the next one.
    values_[static_cast<size_t>(slot)].store(value, std::memory_order_relaxed);
    valuePeers_[static_cast<size_t>(slot)].store(peer, std::memory_order_relaxed);
    dirty_[static_cast<size_t>(slot / 64)].fetch_or(1ULL << (slot % 64), std::memory_order_release);
    scheduleDrain();
}

bool OscRouter::pushOrdered(const OscCommand& command, float value, OscPeerId peer) {
    const auto write = orderedWrite_.load(std::memory_order_relaxed);
    const auto read = orderedRead_.load(std::memory_order_acquire);
    if (write - read >= kOrderedCapacity) {
        // Dropping the newest keeps what is already queued in order. Nothing a
        // person can press fills this; a sender that does is flooding, and the
        // counter is how that becomes visible rather than silent.
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    ordered_[write & (kOrderedCapacity - 1)] = OrderedCommand{command, value, peer};
    // Publishes the entry along with the index that advertises it.
    orderedWrite_.store(write + 1, std::memory_order_release);
    return true;
}

void OscRouter::scheduleDrain() {
    bool expected = false;
    if (!drainScheduled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;  // a drain is already posted and will pick this up

    scheduler_();
}

void OscRouter::postDrain() {
    // Headless hosts and any caller already on the message thread apply inline:
    // there is no loop to post to, and hopping would defer the write forever.
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    if (manager == nullptr || manager->isThisTheMessageThread()) {
        drainPending();
        return;
    }

    juce::MessageManager::callAsync([this, alive = alive_]() {
        if (alive->load(std::memory_order_acquire))
            drainPending();
    });
}

void OscRouter::drainPending() {
    // Released before the walk, not after. A value published while we are
    // partway through the table has to be able to schedule the next drain —
    // if it could not, it would sit unapplied until the surface moved again,
    // which for a fader the user has just let go of means the wrong value
    // staying on screen.
    drainScheduled_.store(false, std::memory_order_release);

    for (int word = 0; word < kDirtyWords; ++word) {
        auto bits = dirty_[static_cast<size_t>(word)].exchange(0, std::memory_order_acquire);
        while (bits != 0) {
            const int slot = (word * 64) + std::countr_zero(bits);
            bits &= bits - 1;
            const auto value = values_[static_cast<size_t>(slot)].load(std::memory_order_relaxed);
            const auto peer =
                valuePeers_[static_cast<size_t>(slot)].load(std::memory_order_relaxed);
            sink_->apply(oscCommandForSlot(slot), value);
            if (feedbackTap_)
                feedbackTap_(peer, slot, value);
        }
    }

    drainBindings();

    // Values first, then edges. A cue that locates and rolls in one bundle
    // should locate before the transport acts on it, rather than rolling from
    // the old position and jumping.
    drainOrdered();
}

void OscRouter::drainBindings() {
    if (bindingSink_ == nullptr)
        return;

    // Read the snapshot once. A value published into a snapshot that has since
    // been replaced goes with it — see OscBindingRoutes for why that is the
    // behaviour we want rather than a leak to plug.
    auto routes = std::atomic_load(&bindingRoutes_);
    if (routes == nullptr)
        return;

    for (std::size_t word = 0; word < routes->dirtyWords; ++word) {
        auto bits = routes->dirty[word].exchange(0, std::memory_order_acquire);
        while (bits != 0) {
            const auto index = (word * 64) + static_cast<std::size_t>(std::countr_zero(bits));
            bits &= bits - 1;
            const auto& entry = routes->entries[index];
            const auto value = routes->values[index].load(std::memory_order_relaxed);
            const auto peer = routes->peers[index].load(std::memory_order_relaxed);
            bindingSink_->apply(entry.binding, value);
            if (bindingFeedbackTap_)
                bindingFeedbackTap_(peer, entry.address, value);
        }
    }
}

void OscRouter::drainOrdered() {
    // Bounded per drain so a sender cannot hold the message thread inside this
    // loop indefinitely by pushing as fast as it is consumed.
    for (std::uint32_t applied = 0; applied < kOrderedCapacity; ++applied) {
        const auto read = orderedRead_.load(std::memory_order_relaxed);
        if (read == orderedWrite_.load(std::memory_order_acquire))
            return;

        const auto entry = ordered_[read & (kOrderedCapacity - 1)];
        orderedRead_.store(read + 1, std::memory_order_release);
        sink_->apply(entry.command, entry.value);

        // A flip request names no state, so the surface that sent it does not
        // know what it produced and has to hear the answer. A delta has no
        // state at all. Everything else here is a button that sent the value it
        // is now showing.
        if (feedbackTap_ && entry.value != kOscToggleRequest &&
            argKindFor(entry.command.kind) != OscArgKind::Delta)
            feedbackTap_(entry.peer, oscSlotIndex(entry.command), entry.value);
    }

    // Hit the cap with work still queued. Whoever pushed the remainder may have
    // found the drain already scheduled, so ask for the next one here rather
    // than leaving those commands until the surface is touched again.
    if (orderedRead_.load(std::memory_order_relaxed) !=
        orderedWrite_.load(std::memory_order_acquire))
        requestFollowUpDrain();
}

void OscRouter::requestFollowUpDrain() {
    bool expected = false;
    if (!drainScheduled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;  // a drain is already posted and will pick this up

    // Deliberately not `scheduleDrain`, which would run the configured
    // scheduler — and the default one applies inline when it is already on the
    // message thread, which here it always is. That would re-enter
    // `drainPending` one stack level per batch without ever returning to the
    // event loop, so a sender that keeps the ring non-empty would hold the
    // message thread and grow the stack: precisely the failure the per-drain
    // cap exists to prevent. The cap only bounds anything if the remainder
    // waits its turn behind other events, which means going through the queue.
    //
    // Reachable only across threads, and not by much: a producer publishes its
    // write index before it calls `scheduleDrain`, so a drain that reads the
    // index inside that gap finds work pending with the scheduled flag still
    // clear. Rare is not the same as impossible, and stack safety should not
    // rest on that ordering happening to hold.
    //
    // With no message loop there is nothing to post to. A headless host drives
    // drains itself, so the remainder waits for the next explicit one rather
    // than recursing here.
    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr) {
        drainScheduled_.store(false, std::memory_order_release);
        return;
    }

    juce::MessageManager::callAsync([this, alive = alive_]() {
        if (alive->load(std::memory_order_acquire))
            drainPending();
    });
}

void OscRouter::setSink(std::unique_ptr<OscCommandSink> sink) {
    jassert(sink != nullptr);
    if (sink != nullptr)
        sink_ = std::move(sink);
}

void OscRouter::setBindingSink(std::unique_ptr<OscBindingSink> sink) {
    bindingSink_ = std::move(sink);
}

void OscRouter::setFeedbackTap(FeedbackTap tap) {
    feedbackTap_ = std::move(tap);
}

void OscRouter::setBindingFeedbackTap(BindingFeedbackTap tap) {
    bindingFeedbackTap_ = std::move(tap);
}

void OscRouter::setDrainScheduler(std::function<void()> scheduler) {
    jassert(scheduler != nullptr);
    if (scheduler != nullptr)
        scheduler_ = std::move(scheduler);
}

std::uint64_t OscRouter::acceptedMessageCount() const {
    return accepted_.load(std::memory_order_relaxed);
}

std::uint64_t OscRouter::droppedCommandCount() const {
    return dropped_.load(std::memory_order_relaxed);
}

}  // namespace magda::osc
