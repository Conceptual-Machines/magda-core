#pragma once

#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "remote_api.hpp"

namespace magda::remote {

/**
 * @brief Subscription topics for state change notifications.
 *
 * The first eight partition the project model. `Meters` and `Playhead` are
 * separate because they are continuous rather than discrete: they change every
 * audio block, so a subscriber opts into them explicitly and receives sampled
 * values rather than a faithful history.
 */
enum class Topic {
    Project,
    Tracks,
    Clips,
    Devices,
    Selection,
    Transport,
    Session,
    Automation,
    Meters,
    Playhead,
};

inline constexpr std::size_t TOPIC_COUNT = 10;

const char* toString(Topic topic);
std::optional<Topic> parseTopic(juce::StringRef topic);

/**
 * @brief Coalescing, revisioned source of "what changed".
 *
 * Sits between MAGDA's model listeners and every remote transport. Model
 * callbacks arrive at whatever rate the model produces them — a 100 Hz fader
 * move is 100 callbacks — and this collapses them to at most one notification
 * per topic per flush interval, carrying the newest revision rather than a
 * queue of intermediate ones.
 *
 * **Why this is shared rather than private to one adapter.** WebSocket
 * subscriptions (#1857) and OSC feedback (#1757 §4) both need exactly this
 * collapse over exactly these model listeners. Their *delivery* differs
 * sharply — ordered per-client queues with resync versus stateless
 * fire-and-forget UDP — so delivery stays private to each adapter. Only the
 * change source is shared, which is the part that would otherwise be built
 * twice or retrofitted onto whichever landed second.
 *
 * **Revisions are the coalescing key.** Collapsing means "everything since
 * revision N", so the revision counter and this source are one mechanism, not
 * two that happen to cooperate.
 *
 * Threading: `markChanged` is lock-free and safe from any thread including the
 * audio thread — it touches only atomics and never allocates. Listeners are
 * always called on the JUCE message thread.
 */
class ChangeSource {
  public:
    struct Change {
        Topic topic = Topic::Project;
        Revision revision = INITIAL_REVISION;

        bool operator==(const Change&) const = default;
    };

    /// Called on the message thread with every topic that changed since the
    /// previous flush. Never called with an empty vector.
    using Listener = std::function<void(const std::vector<Change>&)>;

    ChangeSource();
    ~ChangeSource();

    ChangeSource(const ChangeSource&) = delete;
    ChangeSource& operator=(const ChangeSource&) = delete;

    /// @return A token for `removeListener`. Starts the flush timer on the
    ///         first listener, if a MessageManager exists.
    int addListener(Listener listener);
    void removeListener(int token);

    /**
     * @brief Record that a topic changed, at the given revision.
     *
     * Latest-value-wins: repeated calls before the next flush produce one
     * notification carrying the highest revision seen. Safe from any thread.
     */
    void markChanged(Topic topic, Revision revision);

    /// Deliver pending changes now. Called by the timer; exposed for tests and
    /// for adapters that need a synchronous drain before shutdown.
    void flush();

    /// Flush cadence. 30 Hz by default, matching the rate a control surface or
    /// a UI can actually consume.
    void setFlushIntervalMs(int intervalMs);
    int flushIntervalMs() const;

    /// Drop pending changes without delivering them. Used when the project is
    /// replaced and every pending revision refers to state that no longer
    /// exists.
    void discardPending();

  private:
    class Pump;

    void startPumpIfNeeded();
    void stopPumpIfIdle();

    std::atomic<std::uint32_t> dirtyMask_{0};
    std::array<std::atomic<Revision>, TOPIC_COUNT> revisions_{};

    mutable std::mutex listenerMutex_;
    std::vector<std::pair<int, Listener>> listeners_;
    int nextToken_ = 1;

    std::unique_ptr<Pump> pump_;
    int flushIntervalMs_ = 33;
};

}  // namespace magda::remote
