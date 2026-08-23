#include "remote_changes.hpp"

#include <algorithm>

namespace magda::remote {
namespace {

std::uint32_t bitFor(Topic topic) {
    return 1u << static_cast<std::uint32_t>(topic);
}

}  // namespace

const char* toString(Topic topic) {
    switch (topic) {
        case Topic::Project:
            return "project";
        case Topic::Tracks:
            return "tracks";
        case Topic::Clips:
            return "clips";
        case Topic::Devices:
            return "devices";
        case Topic::Selection:
            return "selection";
        case Topic::Transport:
            return "transport";
        case Topic::Session:
            return "session";
        case Topic::Automation:
            return "automation";
        case Topic::Meters:
            return "meters";
        case Topic::Playhead:
            return "playhead";
    }
    return "project";
}

bool isContinuousTopic(Topic topic) {
    return topic == Topic::Meters || topic == Topic::Playhead;
}

std::optional<Topic> parseTopic(juce::StringRef topic) {
    const juce::String name(topic);
    for (std::size_t index = 0; index < TOPIC_COUNT; ++index) {
        const auto candidate = static_cast<Topic>(index);
        if (name == toString(candidate))
            return candidate;
    }
    return std::nullopt;
}

/**
 * Drives `flush` on the message thread.
 *
 * Separate from ChangeSource so the header does not have to inherit
 * juce::Timer publicly, and so the timer can be absent entirely: unit tests run
 * with no MessageManager, where constructing a Timer is invalid. There, the
 * pump is never created and tests call `flush()` directly.
 */
class ChangeSource::Pump : private juce::Timer {
  public:
    Pump(ChangeSource& owner, int intervalMs) : owner_(owner) {
        startTimer(intervalMs);
    }

    ~Pump() override {
        stopTimer();
    }

    void setInterval(int intervalMs) {
        startTimer(intervalMs);
    }

  private:
    void timerCallback() override {
        owner_.flush();
    }

    ChangeSource& owner_;
};

ChangeSource::ChangeSource() {
    for (auto& revision : revisions_)
        revision.store(INITIAL_REVISION, std::memory_order_relaxed);
}

ChangeSource::~ChangeSource() = default;

int ChangeSource::addListener(Listener listener) {
    int token = 0;
    {
        const std::lock_guard<std::mutex> lock(listenerMutex_);
        token = nextToken_++;
        listeners_.emplace_back(token, std::move(listener));
    }
    startPumpIfNeeded();
    return token;
}

void ChangeSource::removeListener(int token) {
    {
        const std::lock_guard<std::mutex> lock(listenerMutex_);
        listeners_.erase(
            std::remove_if(listeners_.begin(), listeners_.end(),
                           [token](const auto& entry) { return entry.first == token; }),
            listeners_.end());
    }
    stopPumpIfIdle();
}

void ChangeSource::markChanged(Topic topic, Revision revision) {
    const auto index = static_cast<std::size_t>(topic);

    // Latest-value-wins. A compare-exchange loop rather than a plain store: two
    // threads can mark the same topic concurrently, and the newer revision has
    // to survive regardless of which one lands second.
    auto& slot = revisions_[index];
    auto current = slot.load(std::memory_order_relaxed);
    while (revision > current &&
           !slot.compare_exchange_weak(current, revision, std::memory_order_relaxed)) {
        // `current` is refreshed by the failed exchange; retry.
    }

    dirtyMask_.fetch_or(bitFor(topic), std::memory_order_release);
}

void ChangeSource::flush() {
    const auto mask = dirtyMask_.exchange(0, std::memory_order_acquire);
    if (mask == 0)
        return;

    std::vector<Change> changes;
    for (std::size_t index = 0; index < TOPIC_COUNT; ++index) {
        const auto topic = static_cast<Topic>(index);
        if ((mask & bitFor(topic)) != 0)
            changes.push_back({topic, revisions_[index].load(std::memory_order_relaxed)});
    }
    if (changes.empty())
        return;

    // Copy under the lock, then call outside it: a listener is free to add or
    // remove subscriptions in response, which would otherwise deadlock or
    // invalidate the iterator mid-notification.
    std::vector<Listener> targets;
    {
        const std::lock_guard<std::mutex> lock(listenerMutex_);
        targets.reserve(listeners_.size());
        for (const auto& [token, listener] : listeners_)
            targets.push_back(listener);
    }
    for (const auto& listener : targets)
        listener(changes);
}

void ChangeSource::discardPending() {
    dirtyMask_.store(0, std::memory_order_release);
}

void ChangeSource::setFlushIntervalMs(int intervalMs) {
    flushIntervalMs_ = std::max(1, intervalMs);
    if (pump_)
        pump_->setInterval(flushIntervalMs_);
}

int ChangeSource::flushIntervalMs() const {
    return flushIntervalMs_;
}

void ChangeSource::startPumpIfNeeded() {
    if (pump_ != nullptr)
        return;
    // No MessageManager means no timer thread to drive the pump — this is the
    // headless unit-test case, where flush() is called directly.
    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr)
        return;
    pump_ = std::make_unique<Pump>(*this, flushIntervalMs_);
}

void ChangeSource::stopPumpIfIdle() {
    const std::lock_guard<std::mutex> lock(listenerMutex_);
    if (listeners_.empty())
        pump_.reset();
}

}  // namespace magda::remote
