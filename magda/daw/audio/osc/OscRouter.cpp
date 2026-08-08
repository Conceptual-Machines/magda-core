#include "osc/OscRouter.hpp"

#include <juce_events/juce_events.h>

#include <bit>

namespace magda::osc {

namespace {

/**
 * @brief The first argument that carries a number, if the message has one.
 *
 * Only the first is considered. Surfaces that send several — an XY pad, a
 * multi-touch fader bank — address each value separately in this namespace, so
 * a second argument means the sender is speaking a dialect we do not have a
 * mapping for, and guessing which of its numbers we wanted would be worse than
 * taking the one it put first.
 */
std::optional<float> firstNumericArgument(const juce::OSCMessage& message) {
    for (const auto& argument : message) {
        if (argument.isFloat32())
            return argument.getFloat32();
        if (argument.isInt32())
            return static_cast<float>(argument.getInt32());
        break;
    }
    return std::nullopt;
}

}  // namespace

// ============================================================================
// Argument extraction
// ============================================================================

std::optional<float> oscValueFor(const OscCommand& command, const juce::OSCMessage& message) {
    const auto argument = firstNumericArgument(message);

    switch (argKindFor(command.kind)) {
        case OscArgKind::Trigger:
            // A momentary button sends 1 on press and 0 on release. Acting on
            // the release would make holding Play stop the transport when the
            // finger came off.
            if (argument.has_value() && *argument == 0.0f)
                return std::nullopt;
            return 1.0f;

        case OscArgKind::Toggle:
            if (!argument.has_value())
                return kOscToggleRequest;
            return *argument != 0.0f ? 1.0f : 0.0f;

        case OscArgKind::Normalized:
            if (!argument.has_value())
                return std::nullopt;
            return juce::jlimit(0.0f, 1.0f, *argument);

        case OscArgKind::Bpm:
        case OscArgKind::Beats:
            // Ranges belong to the model, which clamps against the project's
            // real limits; a tempo of 0 is not this layer's to reinterpret.
            return argument;
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
    const auto command = parseOscAddress(message.getAddressPattern().toString());
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
    const int slot = oscSlotIndex(command);
    jassert(slot >= 0 && slot < kOscSlotCount);

    // Value first, then the bit that advertises it: a drain that observes the
    // bit is guaranteed to observe at least this value, and a store it misses
    // leaves the bit set for the next one.
    values_[static_cast<size_t>(slot)].store(value, std::memory_order_relaxed);
    dirty_[static_cast<size_t>(slot / 64)].fetch_or(1ULL << (slot % 64), std::memory_order_release);
    scheduleDrain();
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

}  // namespace magda::osc
