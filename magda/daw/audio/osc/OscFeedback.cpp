#include "osc/OscFeedback.hpp"

#include <bit>
#include <cmath>

namespace magda::osc {

// ============================================================================
// OscSenderSink
// ============================================================================

OscSenderSink::~OscSenderSink() {
    disconnect();
}

bool OscSenderSink::connect(const juce::String& host, int port) {
    if (connected_ && host == host_ && port == port_)
        return true;

    disconnect();

    if (host.isEmpty() || port <= 0 || port > 65535)
        return false;

    if (!sender_.connect(host, port))
        return false;

    host_ = host;
    port_ = port;
    connected_ = true;
    return true;
}

void OscSenderSink::disconnect() {
    if (connected_) {
        sender_.disconnect();
        connected_ = false;
    }
    host_ = {};
    port_ = 0;
}

bool OscSenderSink::send(const juce::String& address, float value) {
    if (!connected_)
        return false;
    try {
        return sender_.send(juce::OSCMessage(juce::OSCAddressPattern(address), value));
    } catch (const juce::OSCFormatError&) {
        return false;
    }
}

// ============================================================================
// OscFeedback
// ============================================================================

OscFeedback::OscFeedback(std::unique_ptr<OscMessageSink> sink) : sink_(std::move(sink)) {
    jassert(sink_ != nullptr);
}

OscFeedback::~OscFeedback() = default;

bool OscFeedback::isSendable(const OscCommand& command) {
    return argKindFor(command.kind) != OscArgKind::Delta;
}

void OscFeedback::publish(int slot, float value) {
    jassert(slot >= 0 && slot < kOscSlotCount);
    if (slot < 0 || slot >= kOscSlotCount)
        return;

    const auto index = static_cast<std::size_t>(slot);
    current_[index] = value;
    set(valid_, slot);

    // Dirty is recomputed rather than latched, so a value that moves away from
    // what the surface holds and back again between two flushes sends nothing.
    // The surface is already right; the intermediate value never existed as far
    // as it is concerned.
    if (!test(hasSent_, slot) || lastSent_[index] != value)
        set(dirty_, slot);
    else
        clear(dirty_, slot);
}

void OscFeedback::retire(int slot) {
    jassert(slot >= 0 && slot < kOscSlotCount);
    if (slot < 0 || slot >= kOscSlotCount)
        return;

    clear(valid_, slot);
    clear(dirty_, slot);
    clear(hasSent_, slot);
}

void OscFeedback::noteReceived(int slot, float value) {
    jassert(slot >= 0 && slot < kOscSlotCount);
    if (slot < 0 || slot >= kOscSlotCount)
        return;

    lastReceived_[static_cast<std::size_t>(slot)] = value;
    set(echoed_, slot);
}

void OscFeedback::requestSnapshot() {
    snapshotPending_ = true;
}

void OscFeedback::reset() {
    valid_ = {};
    hasSent_ = {};
    dirty_ = {};
    echoed_ = {};
    snapshotPending_ = false;
}

int OscFeedback::flush() {
    const bool snapshot = snapshotPending_;
    snapshotPending_ = false;

    if (snapshot) {
        // Everything that has a value, whether or not it has moved since the
        // surface was last told. A surface that just joined has been told
        // nothing.
        for (int word = 0; word < kWords; ++word)
            dirty_[static_cast<std::size_t>(word)] |= valid_[static_cast<std::size_t>(word)];
    }

    int sent = 0;

    for (int word = 0; word < kWords; ++word) {
        auto bits = dirty_[static_cast<std::size_t>(word)];
        while (bits != 0) {
            const int slot = (word * 64) + std::countr_zero(bits);
            bits &= bits - 1;

            const auto index = static_cast<std::size_t>(slot);
            const auto command = oscCommandForSlot(slot);

            // A delta address has no state behind it. Nothing should ever
            // publish into one; if something does, it is dropped here rather
            // than sent, so a rewind button cannot be pressed by feedback.
            if (!isSendable(command)) {
                clear(dirty_, slot);
                continue;
            }

            // The echo, and the only thing that is not sent for a reason other
            // than the budget. Recorded as sent anyway: the surface holds this
            // value, so it is what the next change has to differ from.
            if (!snapshot && test(echoed_, slot) &&
                std::abs(current_[index] - lastReceived_[index]) <= kEchoEpsilon) {
                lastSent_[index] = current_[index];
                set(hasSent_, slot);
                clear(dirty_, slot);
                continue;
            }

            if (sent >= kMaxMessagesPerFlush) {
                // Left dirty on purpose. The walk continues so the echo rule
                // above still runs for the rest of the table before its bits
                // are cleared below.
                ++deferred_;
                continue;
            }

            if (!sink_->send(formatOscAddress(command), current_[index])) {
                // No destination. Drop the send but keep the slot dirty, so
                // configuring one later starts from what is true rather than
                // from what happened to change after it was configured.
                continue;
            }

            lastSent_[index] = current_[index];
            set(hasSent_, slot);
            clear(dirty_, slot);
            ++sent;
        }
    }

    echoed_ = {};
    sent_ += static_cast<std::uint64_t>(sent);
    return sent;
}

}  // namespace magda::osc
