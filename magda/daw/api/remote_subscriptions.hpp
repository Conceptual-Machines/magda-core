#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "remote_api.hpp"
#include "remote_changes.hpp"
#include "remote_service.hpp"

namespace magda {

class AudioEngine;
class MagdaApi;

namespace remote {

/**
 * @brief One pushed state change, independent of the transport that carries it.
 *
 * The envelope is deliberately four fields and no framing: WebSocket wraps it in
 * a JSON-RPC notification, MCP (#1858) will wrap it in a resource update, and
 * neither shape belongs in here. `revision` is the project revision the payload
 * describes, which is what makes a stream orderable and a gap detectable.
 */
struct SubscriptionEvent {
    enum class Type {
        /// Complete state for the topic. The first delivery on a topic, and the
        /// recovery form after a client falls behind.
        Snapshot,
        /// What changed since the previous event on this topic. Only meaningful
        /// to a client that received every earlier event in order.
        Delta,
        /// A point reading of a continuously changing value — meters, playhead.
        /// Carries no history and is safe to drop.
        Sample,
    };

    Topic topic = Topic::Project;
    Type type = Type::Snapshot;
    Revision revision = INITIAL_REVISION;
    juce::var payload;

    /// `{"topic":…,"type":…,"revision":…,"payload":…}`
    juce::var toJson() const;
};

const char* toString(SubscriptionEvent::Type type);

/**
 * @brief Where per-track meter levels come from.
 *
 * An interface rather than a direct read of the audio engine because the remote
 * API layer is built and tested without one: `magda_tests` links the API but not
 * the engine, and the hub has to be exercisable there. The live implementation
 * lives in `remote_meters_live.cpp`, the same `_live` split the rest of the
 * facade uses.
 *
 * `sample()` is called on the message thread and must not block: it reads a
 * lock-free snapshot the audio path has already published, and never touches the
 * audio thread itself.
 */
class MeterSource {
  public:
    struct TrackLevels {
        TrackId trackId = INVALID_TRACK_ID;
        float peakL = 0.0f;
        float peakR = 0.0f;
        bool clipped = false;

        bool operator==(const TrackLevels&) const = default;
    };

    virtual ~MeterSource() = default;

    /// Latest levels for every track that has any, plus `MASTER_TRACK_ID`.
    virtual std::vector<TrackLevels> sample() = 0;
};

/**
 * @brief Turns "a topic changed" into per-client event streams (#1857).
 *
 * Sits between `ChangeSource` — which says *that* a topic changed, coalesced and
 * revisioned — and the transports, which have to say *what* changed to each
 * client that asked. It is shared rather than private to the WebSocket adapter
 * for the same reason `ChangeSource` is: MCP resource updates need the identical
 * projection and the identical resync rules, and the second implementation would
 * drift from the first.
 *
 * ## What a client receives
 *
 * One `Snapshot` per topic when it subscribes, then a `Delta` per coalesced
 * flush in which that topic changed. Deltas are computed once per topic per
 * flush and shared by every subscriber, so N clients cost one projection rather
 * than N.
 *
 * Not every topic deltas. `tracks`, `clips`, `automation`, and `session` are
 * keyed collections where a delta is most of the saving — a note added to one
 * clip should not re-send every clip in the project. `project`, `transport`, and
 * `selection` are single small objects with nothing to diff. `devices` is a
 * three-array graph whose ids are unique only within a track section, so an
 * id-keyed diff would be wrong rather than merely unhelpful; it re-sends the
 * graph, which changes at edit rate and not at gesture rate. Those four
 * therefore arrive as `Delta` events whose payload is the whole topic — the type
 * still tells a client this is a change rather than a subscription snapshot.
 *
 * `meters` and `playhead` never delta and are never driven by `ChangeSource`.
 * Nothing marks them: the playhead moves without notifying anyone and meters are
 * a continuous signal. They are sampled on their own timer instead, at
 * `Options::samplingIntervalMs`, latest value wins. A client that does not
 * subscribe to them costs nothing, and the timer does not run.
 *
 * ## Falling behind
 *
 * A sink returns false when the client cannot take the event — its outgoing
 * queue is full, which for a bounded queue means the client has stopped reading.
 * The event is dropped, not buffered: buffering on behalf of a client that is
 * not listening is how a stalled connection grows memory without limit. The
 * client is marked for resync, so the next event it *can* take is a fresh
 * `Snapshot` rather than a delta it has no baseline for. A client that keeps
 * refusing for `Options::droppedFlushesBeforeDisconnect` consecutive flushes is
 * disconnected: at that point it is holding a connection slot and a thread and
 * consuming nothing.
 *
 * Dropped `Sample` events do not count towards either, because dropping them is
 * the specified behaviour rather than a symptom.
 *
 * ## Threading
 *
 * Projection reads live model state, so it happens on the message thread and
 * nowhere else — the same constraint `RemoteApiService` exists to enforce, for
 * the same reason. `publish` and `sampleNow` are already there, driven by
 * timers. `handle` is not: it arrives on a transport thread, validates its input
 * there, and hops for the part that touches the model, which is why it answers
 * through a callback rather than returning a `Response`.
 *
 * Timer lifetime is the other reason. The flush listener and the sampler are
 * `juce::Timer`s, and JUCE calls a timer's callback with its own lock released —
 * so starting or stopping one from a network thread can destroy a timer whose
 * callback is running. Every change to what is subscribed therefore only records
 * the intent; the timers themselves are reconciled on the message thread.
 *
 * One mutex covers the client table and the per-topic baselines. Sinks are
 * called while it is held — a sink appends to the client's own queue and must
 * not call back into the hub.
 */
class SubscriptionHub {
  public:
    using ClientId = int;

    /// Deliver one event. Returns false when the client cannot take it now.
    using Sink = std::function<bool(const SubscriptionEvent&)>;

    /// Drop a client that has stopped consuming. Called with the hub's lock
    /// held, so it must do no more than mark the connection.
    using Disconnect = std::function<void(const juce::String& reason)>;

    /// Answer to one subscription method. Called exactly once.
    using Completion = std::function<void(Response)>;

    struct Options {
        /// Cadence for `meters` and `playhead`. 20 Hz: fast enough for a meter
        /// bridge or a moving playhead readout, and an order of magnitude below
        /// the block rate that produces the underlying values.
        int samplingIntervalMs = 50;

        /// Consecutive flushes a client may refuse before it is disconnected.
        /// At the 30 Hz flush cadence this is about three seconds of a client
        /// that is connected, subscribed, and reading nothing.
        int droppedFlushesBeforeDisconnect = 100;
    };

    // Two overloads rather than a defaulted parameter: `Options`'s own member
    // initializers are not usable in a default argument inside the class that
    // declares it.
    SubscriptionHub(MagdaApi& api, RemoteApiService& service);
    SubscriptionHub(MagdaApi& api, RemoteApiService& service, Options options);
    ~SubscriptionHub();

    SubscriptionHub(const SubscriptionHub&) = delete;
    SubscriptionHub& operator=(const SubscriptionHub&) = delete;

    /// Register a transport connection. The returned id addresses it until
    /// `removeClient`; ids are never reused.
    ClientId addClient(Sink sink, Disconnect disconnect);
    void removeClient(ClientId client);

    /// Subscribed clients. For tests and diagnostics.
    int clientCount() const;

    /**
     * @brief Execute one subscription method on behalf of a client.
     *
     * Returns false without calling `onComplete` when `method` is not a
     * subscription method, which is the adapter's signal to route it to
     * `RemoteApiService::dispatch` instead.
     *
     * Otherwise `onComplete` is invoked exactly once: on the calling thread for
     * a request rejected before the hop, and on the message thread for one that
     * had to read the model. Input is validated against the same schema the
     * registry advertises, so a transport does not re-declare one.
     */
    bool handle(ClientId client, const juce::String& method, const juce::var& params,
                Completion onComplete);

    /// True for the four `subscriptions.*` methods the registry declares as
    /// transport-scoped.
    static bool isSubscriptionMethod(const juce::String& method);

    /**
     * @brief Project the topics that changed and push them.
     *
     * Wired to `RemoteApiService::changes()` for the hub's lifetime. Exposed
     * because a host with no MessageManager has no flush timer, which is the
     * headless test case.
     */
    void publish(const std::vector<ChangeSource::Change>& changes);

    /// Read and push `meters` and `playhead`. Driven by the sampling timer;
    /// exposed for the same reason as `publish`.
    void sampleNow();

    /**
     * @brief Install the source `meters` reads.
     *
     * Without one, `meters` subscribes successfully and delivers empty samples
     * rather than failing: whether an audio engine exists is not something a
     * remote client can do anything about, and a subscription that errors on a
     * headless host would make every client special-case it.
     */
    void setMeterSource(std::unique_ptr<MeterSource> source);

    /// Stop delivering, drop every client, and detach from the change source.
    /// Idempotent; the destructor calls it. Call on the message thread, which is
    /// where the timers it stops belong.
    void shutdown();

  private:
    struct Client;
    struct Gate;
    class Sampler;

    /// State the hub keeps per topic so it can produce a delta rather than a
    /// snapshot. Held only while the topic has at least one subscriber.
    struct TopicState {
        bool hasBaseline = false;
        juce::var baseline;
    };

    juce::var projectTopic(Topic topic);
    juce::var sampleTopic(Topic topic);

    Response execute(ClientId client, const juce::String& method, const juce::var& params,
                     const std::vector<Topic>& requested, bool topicsGiven);

    void publishTopicLocked(Topic topic, Revision revision);
    void deliverLocked(Client& client, const SubscriptionEvent& event);
    void sendSnapshotsLocked(Client& client, const std::vector<Topic>& topics, Revision revision,
                             juce::Array<juce::var>& into);
    bool anySubscriberLocked(Topic topic) const;
    /// Whether any subscriber of `topic` is behind and owed a snapshot.
    bool owesSnapshotLocked(Topic topic) const;
    void releaseIdleTopicsLocked();
    /// @return true when a client was dropped, so the caller knows to reconcile.
    bool dropAbandonedLocked();

    /// Reconcile the flush listener and the sampling timer with what is
    /// subscribed. Message thread only — it starts and stops `juce::Timer`s.
    void applyTopology();
    /// Ask for that reconciliation from wherever this is called, including from
    /// inside a timer callback.
    void scheduleTopology();

    Client* findLocked(ClientId client);

    MagdaApi& api_;
    RemoteApiService& service_;
    const Options options_;

    mutable std::mutex mutex_;
    std::vector<Client> clients_;
    ClientId nextClientId_ = 1;
    std::array<TopicState, TOPIC_COUNT> topics_{};

    std::unique_ptr<MeterSource> meters_;
    std::unique_ptr<Sampler> sampler_;

    std::shared_ptr<Gate> gate_;
    int changeToken_ = 0;
    bool topologyPending_ = false;
    bool shutdown_ = false;
};

/**
 * @brief The meter source backed by a running audio engine.
 *
 * Defined in `remote_meters_live.cpp`, which is the only file in the remote API
 * layer that includes the engine — the same `_live` split `ModelChangeBridge`
 * uses for the model singletons.
 */
std::unique_ptr<MeterSource> makeLiveMeterSource(AudioEngine& engine);

}  // namespace remote
}  // namespace magda
