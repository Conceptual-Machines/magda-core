#include "osc/OscRouter.hpp"

#include <juce_events/juce_events.h>

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
MessageArgument firstNumericArgument(const juce::OSCMessage& message) {
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

}  // namespace

// ============================================================================
// Argument extraction
// ============================================================================

std::optional<float> oscValueFor(const OscCommand& command, const juce::OSCMessage& message) {
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

bool OscRouter::handleMessage(const juce::OSCMessage& message) {
    // Named rather than inlined into the call: the parser reads through the
    // StringRef into this buffer, so it has to outlive the parse. Copying a
    // juce::String is a reference-count bump, so this costs no allocation.
    const auto address = message.getAddressPattern().toString();

    const auto command = parseOscAddress(address);
    if (!command.has_value())
        return false;

    const auto value = oscValueFor(*command, message);
    if (!value.has_value())
        return false;

    accepted_.fetch_add(1, std::memory_order_relaxed);
    submit(*command, *value);
    return true;
}

void OscRouter::submit(const OscCommand& command, float value) {
    const auto argKind = argKindFor(command.kind);
    if (argKind == OscArgKind::Trigger || argKind == OscArgKind::Toggle) {
        // An edge, not a value. Two flips of one toggle have to stay two, and
        // play and stop have to resolve by arrival rather than by slot number.
        pushOrdered(command, value);
        scheduleDrain();
        return;
    }

    const int slot = oscSlotIndex(command);
    jassert(slot >= 0 && slot < kOscSlotCount);

    // Value first, then the bit that advertises it: a drain that observes the
    // bit is guaranteed to observe at least this value, and a store it misses
    // leaves the bit set for the next one.
    values_[static_cast<size_t>(slot)].store(value, std::memory_order_relaxed);
    dirty_[static_cast<size_t>(slot / 64)].fetch_or(1ULL << (slot % 64), std::memory_order_release);
    scheduleDrain();
}

bool OscRouter::pushOrdered(const OscCommand& command, float value) {
    const auto write = orderedWrite_.load(std::memory_order_relaxed);
    const auto read = orderedRead_.load(std::memory_order_acquire);
    if (write - read >= kOrderedCapacity) {
        // Dropping the newest keeps what is already queued in order. Nothing a
        // person can press fills this; a sender that does is flooding, and the
        // counter is how that becomes visible rather than silent.
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    ordered_[write & (kOrderedCapacity - 1)] = OrderedCommand{command, value};
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
            sink_->apply(oscCommandForSlot(slot),
                         values_[static_cast<size_t>(slot)].load(std::memory_order_relaxed));
        }
    }

    // Values first, then edges. A cue that locates and rolls in one bundle
    // should locate before the transport acts on it, rather than rolling from
    // the old position and jumping.
    drainOrdered();
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
    }

    // Hit the cap with work still queued. Whoever pushed the remainder may have
    // found the drain already scheduled, so ask for the next one here rather
    // than leaving those commands until the surface is touched again.
    if (orderedRead_.load(std::memory_order_relaxed) !=
        orderedWrite_.load(std::memory_order_acquire))
        scheduleDrain();
}

void OscRouter::setSink(std::unique_ptr<OscCommandSink> sink) {
    jassert(sink != nullptr);
    if (sink != nullptr)
        sink_ = std::move(sink);
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
