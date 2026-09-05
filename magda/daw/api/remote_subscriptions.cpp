#include "remote_subscriptions.hpp"

#include <juce_events/juce_events.h>

#include <algorithm>

#include "magda_api.hpp"
#include "transport_api.hpp"

namespace magda::remote {
namespace {

constexpr const char* kSubscribe = "subscriptions.subscribe";
constexpr const char* kUnsubscribe = "subscriptions.unsubscribe";
constexpr const char* kList = "subscriptions.list";
constexpr const char* kResync = "subscriptions.resync";

std::size_t indexOf(Topic topic) {
    return static_cast<std::size_t>(topic);
}

juce::var makeObject() {
    return {new juce::DynamicObject()};
}

void setProperty(const juce::var& object, const char* name, const juce::var& value) {
    if (auto* dynamicObject = object.getDynamicObject())
        dynamicObject->setProperty(juce::Identifier(name), value);
}

/**
 * @brief Structural equality for two projected payloads.
 *
 * `juce::var::operator==` compares two DynamicObjects by pointer, so every
 * re-projection would look different from the one before it and every marked
 * topic would produce an event whether or not anything changed. Comparing
 * serialized JSON instead would be correct but allocates a string per element
 * per flush, which is the wrong cost for something run over every clip in the
 * project at 30 Hz.
 */
bool deepEquals(const juce::var& lhs, const juce::var& rhs) {
    if (auto* lhsObject = lhs.getDynamicObject()) {
        auto* rhsObject = rhs.getDynamicObject();
        if (rhsObject == nullptr)
            return false;

        const auto& lhsProperties = lhsObject->getProperties();
        const auto& rhsProperties = rhsObject->getProperties();
        if (lhsProperties.size() != rhsProperties.size())
            return false;

        for (int i = 0; i < lhsProperties.size(); ++i) {
            const auto name = lhsProperties.getName(i);
            if (!rhsProperties.contains(name))
                return false;
            if (!deepEquals(lhsProperties.getValueAt(i), rhsProperties[name]))
                return false;
        }
        return true;
    }

    if (const auto* lhsArray = lhs.getArray()) {
        const auto* rhsArray = rhs.getArray();
        if (rhsArray == nullptr || lhsArray->size() != rhsArray->size())
            return false;
        for (int i = 0; i < lhsArray->size(); ++i)
            if (!deepEquals(lhsArray->getReference(i), rhsArray->getReference(i)))
                return false;
        return true;
    }

    // Anything else is a scalar; a scalar never equals a container.
    if (rhs.getDynamicObject() != nullptr || rhs.getArray() != nullptr)
        return false;
    return lhs == rhs;
}

/**
 * @brief The fields that identify one element of a keyed topic.
 *
 * Empty for a topic that is not diffed element-wise. `devices` is deliberately
 * absent: a `DeviceId` is unique only within one track section, so an id-keyed
 * diff would merge three different devices into one.
 */
std::vector<const char*> keyFieldsFor(Topic topic) {
    switch (topic) {
        case Topic::Tracks:
        case Topic::Clips:
        case Topic::Automation:
            return {"id"};
        case Topic::Session:
            return {"trackId", "sceneIndex"};
        case Topic::Project:
        case Topic::Devices:
        case Topic::Selection:
        case Topic::Transport:
        case Topic::Meters:
        case Topic::Playhead:
            break;
    }
    return {};
}

/// The member of a keyed topic's payload that holds its elements, or nullptr
/// when the payload is the array itself.
const char* collectionFieldFor(Topic topic) {
    return topic == Topic::Session ? "slots" : nullptr;
}

const juce::Array<juce::var>* elementsOf(Topic topic, const juce::var& payload) {
    if (const auto* field = collectionFieldFor(topic))
        return payload[field].getArray();
    return payload.getArray();
}

juce::String keyOf(const juce::var& element, const std::vector<const char*>& fields) {
    juce::String key;
    for (const auto* field : fields) {
        // A unit separator rather than a plain join: two id fields concatenated
        // bare would make {1, 23} and {12, 3} the same key.
        key << element[field].toString() << juce::String::charToString(31);
    }
    return key;
}

/// What a `removed` entry carries: the bare id for a single-field key, and the
/// identifying fields as an object for a composite one.
juce::var identityOf(const juce::var& element, const std::vector<const char*>& fields) {
    if (fields.size() == 1)
        return element[fields.front()];

    auto identity = makeObject();
    for (const auto* field : fields)
        setProperty(identity, field, element[field]);
    return identity;
}

struct Diff {
    juce::Array<juce::var> added;
    juce::Array<juce::var> updated;
    juce::Array<juce::var> removed;

    bool changed() const {
        return !added.isEmpty() || !updated.isEmpty() || !removed.isEmpty();
    }

    juce::var toJson() const {
        auto payload = makeObject();
        setProperty(payload, "added", added);
        setProperty(payload, "updated", updated);
        setProperty(payload, "removed", removed);
        return payload;
    }
};

Diff diffElements(Topic topic, const juce::var& previous, const juce::var& current) {
    const auto fields = keyFieldsFor(topic);
    Diff diff;

    const auto* before = elementsOf(topic, previous);
    const auto* after = elementsOf(topic, current);
    if (after == nullptr)
        return diff;

    std::vector<std::pair<juce::String, const juce::var*>> beforeByKey;
    if (before != nullptr) {
        beforeByKey.reserve(static_cast<std::size_t>(before->size()));
        for (const auto& element : *before)
            beforeByKey.emplace_back(keyOf(element, fields), &element);
        std::sort(beforeByKey.begin(), beforeByKey.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    }

    const auto find = [&beforeByKey](const juce::String& key) -> const juce::var* {
        const auto found = std::lower_bound(
            beforeByKey.begin(), beforeByKey.end(), key,
            [](const auto& entry, const juce::String& value) { return entry.first < value; });
        if (found == beforeByKey.end() || found->first != key)
            return nullptr;
        return found->second;
    };

    std::vector<juce::String> seen;
    seen.reserve(static_cast<std::size_t>(after->size()));

    for (const auto& element : *after) {
        auto key = keyOf(element, fields);
        const auto* existing = find(key);
        if (existing == nullptr)
            diff.added.add(element);
        else if (!deepEquals(*existing, element))
            diff.updated.add(element);
        seen.push_back(std::move(key));
    }

    std::sort(seen.begin(), seen.end());
    for (const auto& [key, element] : beforeByKey)
        if (!std::binary_search(seen.begin(), seen.end(), key))
            diff.removed.add(identityOf(*element, fields));

    return diff;
}

/// The read operation whose output is a topic's complete state. Reusing the
/// registry's own handlers is what keeps a pushed snapshot byte-identical to
/// what polling the same operation would return.
const char* snapshotOperationFor(Topic topic) {
    switch (topic) {
        case Topic::Project:
            return "project.get";
        case Topic::Tracks:
            return "tracks.list";
        case Topic::Clips:
            return "clips.list";
        case Topic::Devices:
            return "devices.list";
        case Topic::Selection:
            return "selection.get";
        case Topic::Transport:
            return "transport.get";
        case Topic::Session:
            return "session.get";
        case Topic::Automation:
            return "automation.listLanes";
        case Topic::Meters:
        case Topic::Playhead:
            break;
    }
    return nullptr;
}

}  // namespace

const char* toString(SubscriptionEvent::Type type) {
    switch (type) {
        case SubscriptionEvent::Type::Snapshot:
            return "snapshot";
        case SubscriptionEvent::Type::Delta:
            return "delta";
        case SubscriptionEvent::Type::Sample:
            return "sample";
    }
    return "snapshot";
}

juce::var SubscriptionEvent::toJson() const {
    auto event = makeObject();
    setProperty(event, "topic", juce::String(toString(topic)));
    setProperty(event, "type", juce::String(toString(type)));
    setProperty(event, "revision", static_cast<juce::int64>(revision));
    setProperty(event, "payload", payload);
    return event;
}

// ===========================================================================
// Internals
// ===========================================================================

struct SubscriptionHub::Client {
    ClientId id = 0;
    Sink sink;
    Disconnect disconnect;
    std::array<bool, TOPIC_COUNT> subscribed{};
    /// Set when a delivery was refused: the client has no baseline for the
    /// delta it just missed, so the next thing it takes has to be complete.
    std::array<bool, TOPIC_COUNT> needsSnapshot{};

    /// Flushes in a row in which this client refused something.
    int consecutiveDrops = 0;

    /**
     * What happened to this client during the flush now in progress.
     *
     * Meaningful only between the start and the end of one `publish`, which
     * resets them and folds them into `consecutiveDrops` exactly once. They
     * exist because a flush delivers up to one event per subscribed topic, and
     * per-event accounting gets both directions wrong: several refusals in one
     * flush would count as several flushes, and — worse — a client that accepts
     * one topic and refuses another every time would have the counter reset by
     * the acceptance before the refusal put it back, so it would sit at one
     * forever while the refused topic stayed permanently stale.
     */
    bool refusedThisFlush = false;
    bool acceptedThisFlush = false;
};

/**
 * Drives `sampleNow` for continuous topics.
 *
 * Separate from the hub for the same reason `ChangeSource::Pump` is: the header
 * does not inherit `juce::Timer`, and a headless host — where constructing a
 * Timer is invalid — simply never creates one and calls `sampleNow()` directly.
 */
class SubscriptionHub::Sampler : private juce::Timer {
  public:
    Sampler(SubscriptionHub& owner, int intervalMs) : owner_(owner) {
        startTimer(intervalMs);
    }

    ~Sampler() override {
        stopTimer();
    }

  private:
    void timerCallback() override {
        owner_.sampleNow();
    }

    SubscriptionHub& owner_;
};

/**
 * Keeps the change-source listener from outliving the hub.
 *
 * `ChangeSource::flush` copies its listeners and calls them outside its own
 * lock, so `removeListener` returns without waiting for a call already in
 * flight. Taking this mutex and clearing the pointer in `shutdown()` is what
 * makes "no listener is running and none can start" true — the same shape
 * `RemoteApiService::ExecutionState` uses for queued work.
 */
struct SubscriptionHub::Gate {
    std::mutex mutex;
    SubscriptionHub* hub = nullptr;
};

// ===========================================================================
// SubscriptionHub
// ===========================================================================

SubscriptionHub::SubscriptionHub(MagdaApi& api, RemoteApiService& service)
    : SubscriptionHub(api, service, Options{}) {}

SubscriptionHub::SubscriptionHub(MagdaApi& api, RemoteApiService& service, Options options)
    : api_(api), service_(service), options_(options), gate_(std::make_shared<Gate>()) {
    gate_->hub = this;
}

SubscriptionHub::~SubscriptionHub() {
    shutdown();
}

void SubscriptionHub::shutdown() {
    // Retire the listener first and outside the hub's own lock: this blocks
    // until any flush already inside publish() has finished, and that flush
    // needs mutex_ to finish.
    {
        const std::scoped_lock lock(gate_->mutex);
        gate_->hub = nullptr;
    }

    int token = 0;
    {
        const std::scoped_lock lock(mutex_);
        if (shutdown_)
            return;
        shutdown_ = true;
        token = changeToken_;
        changeToken_ = 0;
        clients_.clear();
        for (auto& topic : topics_)
            topic = {};
        sampler_.reset();
    }

    if (token != 0)
        service_.changes().removeListener(token);
}

SubscriptionHub::ClientId SubscriptionHub::addClient(Sink sink, Disconnect disconnect) {
    const std::scoped_lock lock(mutex_);
    if (shutdown_)
        return 0;

    const auto id = nextClientId_++;
    clients_.push_back(Client{id, std::move(sink), std::move(disconnect), {}, {}, 0});
    return id;
}

void SubscriptionHub::removeClient(ClientId client) {
    {
        const std::scoped_lock lock(mutex_);
        const auto found = std::remove_if(clients_.begin(), clients_.end(),
                                          [client](const Client& c) { return c.id == client; });
        if (found == clients_.end())
            return;
        clients_.erase(found, clients_.end());
        releaseIdleTopicsLocked();
    }
    // This runs on the departing connection's own thread, so the timers are
    // somebody else's to touch.
    scheduleTopology();
}

int SubscriptionHub::clientCount() const {
    const std::scoped_lock lock(mutex_);
    return static_cast<int>(clients_.size());
}

void SubscriptionHub::setMeterSource(std::unique_ptr<MeterSource> source) {
    const std::scoped_lock lock(mutex_);
    meters_ = std::move(source);
}

bool SubscriptionHub::isSubscriptionMethod(const juce::String& method) {
    return method == kSubscribe || method == kUnsubscribe || method == kList || method == kResync;
}

SubscriptionHub::Client* SubscriptionHub::findLocked(ClientId client) {
    const auto found = std::find_if(clients_.begin(), clients_.end(),
                                    [client](const Client& c) { return c.id == client; });
    return found == clients_.end() ? nullptr : &*found;
}

bool SubscriptionHub::anySubscriberLocked(Topic topic) const {
    const auto index = indexOf(topic);
    return std::any_of(clients_.begin(), clients_.end(),
                       [index](const Client& client) { return client.subscribed[index]; });
}

void SubscriptionHub::releaseIdleTopicsLocked() {
    // A baseline exists to diff against; with nobody subscribed there is nothing
    // to diff for, and holding a projection of every clip in the project on
    // behalf of no one is exactly the memory this design is trying not to spend.
    for (std::size_t index = 0; index < TOPIC_COUNT; ++index)
        if (!anySubscriberLocked(static_cast<Topic>(index)))
            topics_[index] = {};
}

/**
 * Reconcile both timers with what is actually subscribed.
 *
 * Both are `juce::Timer`s, and both are started and stopped through this one
 * function on the message thread, never from a transport thread and never from
 * inside a timer callback. JUCE releases its own lock before invoking a timer
 * callback, so stopping a timer from elsewhere can free one whose callback is
 * on the stack — and `~Timer` says as much in an assertion.
 *
 * Detaching matters as much as attaching: `ChangeSource::addListener` starts the
 * 30 Hz flush timer, so a hub that stayed subscribed with no clients would keep
 * waking the message thread for the life of the process.
 */
void SubscriptionHub::applyTopology() {
    bool wantChanges = false;
    bool wantSampler = false;
    int existing = 0;
    {
        const std::scoped_lock lock(mutex_);
        topologyPending_ = false;
        if (shutdown_)
            return;

        for (std::size_t index = 0; index < TOPIC_COUNT; ++index) {
            const auto topic = static_cast<Topic>(index);
            if (!anySubscriberLocked(topic))
                continue;
            if (isContinuousTopic(topic))
                wantSampler = true;
            if (!isContinuousTopic(topic))
                wantChanges = true;
        }
        existing = changeToken_;

        // No MessageManager means no timer thread at all — the headless case,
        // where tests drive sampleNow() and flush() by hand.
        const bool canTime = juce::MessageManager::getInstanceWithoutCreating() != nullptr;
        if (!wantSampler)
            sampler_.reset();
        else if (sampler_ == nullptr && canTime)
            sampler_ = std::make_unique<Sampler>(*this, std::max(1, options_.samplingIntervalMs));
    }

    if (wantChanges == (existing != 0))
        return;

    if (wantChanges) {
        auto gate = gate_;
        const auto token = service_.changes().addListener(
            [gate](const std::vector<ChangeSource::Change>& changes) {
                const std::scoped_lock lock(gate->mutex);
                if (gate->hub != nullptr)
                    gate->hub->publish(changes);
            });

        bool keep = false;
        {
            const std::scoped_lock lock(mutex_);
            // Shutdown may have landed while the listener was being attached,
            // in which case this token is surplus.
            if (!shutdown_ && changeToken_ == 0) {
                changeToken_ = token;
                keep = true;
            }
        }
        if (!keep)
            service_.changes().removeListener(token);
        return;
    }

    int token = 0;
    {
        const std::scoped_lock lock(mutex_);
        token = changeToken_;
        changeToken_ = 0;
    }
    if (token != 0)
        service_.changes().removeListener(token);
}

void SubscriptionHub::scheduleTopology() {
    // Deliberately asynchronous even when this is already the message thread:
    // publish() and dropAbandoned() run inside the flush timer's own callback,
    // and detaching the listener there would destroy that timer mid-callback.
    if (juce::MessageManager::getInstanceWithoutCreating() == nullptr) {
        applyTopology();
        return;
    }

    {
        const std::scoped_lock lock(mutex_);
        if (shutdown_ || topologyPending_)
            return;
        topologyPending_ = true;
    }

    auto gate = gate_;
    if (!juce::MessageManager::callAsync([gate] {
            const std::scoped_lock lock(gate->mutex);
            if (gate->hub != nullptr)
                gate->hub->applyTopology();
        })) {
        // The message loop is going away, so nothing will reconcile anything
        // again; shutdown() detaches everything regardless.
        const std::scoped_lock lock(mutex_);
        topologyPending_ = false;
    }
}

// ---------------------------------------------------------------------------
// Projection
// ---------------------------------------------------------------------------

juce::var SubscriptionHub::projectTopic(Topic topic) {
    const auto* name = snapshotOperationFor(topic);
    if (name == nullptr)
        return {};

    const auto* operation = OperationRegistry::instance().find(name);
    if (operation == nullptr || operation->handler == nullptr) {
        jassertfalse;  // A topic naming an operation the registry does not have.
        return {};
    }

    RequestContext context;
    context.clientId = "subscriptions";
    auto result = operation->handler(api_, makeObject(), context);
    return result.failed() ? juce::var() : result.value;
}

juce::var SubscriptionHub::sampleTopic(Topic topic) {
    if (topic == Topic::Playhead) {
        auto payload = makeObject();
        setProperty(payload, "positionBeats", api_.transport().getPositionBeats());
        setProperty(payload, "playing", api_.transport().isPlaying());
        return payload;
    }

    juce::Array<juce::var> levels;
    // No engine means no meters. Delivering an empty sample rather than an error
    // keeps the subscription valid on a headless host, which is not something a
    // remote client can do anything about.
    if (meters_ != nullptr) {
        for (const auto& track : meters_->sample()) {
            auto entry = makeObject();
            setProperty(entry, "trackId", static_cast<int>(track.trackId));
            setProperty(entry, "peakL", static_cast<double>(track.peakL));
            setProperty(entry, "peakR", static_cast<double>(track.peakR));
            setProperty(entry, "clipped", track.clipped);
            levels.add(entry);
        }
    }

    auto payload = makeObject();
    setProperty(payload, "tracks", levels);
    return payload;
}

// ---------------------------------------------------------------------------
// Delivery
// ---------------------------------------------------------------------------

void SubscriptionHub::deliverLocked(Client& client, const SubscriptionEvent& event) {
    const auto index = indexOf(event.topic);
    const bool sent = client.sink != nullptr && client.sink(event);

    if (sent) {
        if (event.type == SubscriptionEvent::Type::Snapshot)
            client.needsSnapshot[index] = false;
        if (event.type != SubscriptionEvent::Type::Sample)
            client.acceptedThisFlush = true;
        return;
    }

    // A refused sample is the policy working, not a client falling behind:
    // meters and the playhead are latest-value-wins and the next one is along in
    // a few tens of milliseconds.
    if (event.type == SubscriptionEvent::Type::Sample)
        return;

    client.needsSnapshot[index] = true;
    client.refusedThisFlush = true;
}

/**
 * Charge one flush against every client that refused something during it.
 *
 * A refusal outweighs an acceptance in the same flush: a client keeping up on
 * one topic while never taking another is not keeping up, and counting the
 * acceptance would leave it connected and permanently stale on the rest.
 *
 * A flush that delivered nothing to a client leaves its count alone. That is not
 * evidence in either direction, and a subscriber to a quiet topic must not have
 * its arrears forgiven by the silence.
 */
void SubscriptionHub::foldFlushOutcomesLocked() {
    for (auto& client : clients_) {
        if (client.refusedThisFlush)
            ++client.consecutiveDrops;
        else if (client.acceptedThisFlush)
            client.consecutiveDrops = 0;
    }
}

bool SubscriptionHub::owesSnapshotLocked(Topic topic) const {
    const auto index = indexOf(topic);
    return std::any_of(clients_.begin(), clients_.end(), [index](const Client& client) {
        return client.subscribed[index] && client.needsSnapshot[index];
    });
}

void SubscriptionHub::publishTopicLocked(Topic topic, Revision revision) {
    const auto index = indexOf(topic);

    // A client that refused an event is owed complete state, and it is owed it
    // whether or not anything has changed since. Deciding that before the
    // early-out below is what makes the promise good: otherwise the snapshot
    // would arrive only on the next observable change, and a client that dropped
    // one event from a project that then went quiet would stay stale until it
    // thought to ask for a resync.
    const bool owed = owesSnapshotLocked(topic);

    auto current = projectTopic(topic);
    auto& state = topics_[index];
    const bool hadBaseline = state.hasBaseline;

    SubscriptionEvent delta{topic, SubscriptionEvent::Type::Delta, revision, {}};
    bool changed = !hadBaseline;

    if (hadBaseline) {
        if (!keyFieldsFor(topic).empty()) {
            const auto difference = diffElements(topic, state.baseline, current);
            changed = difference.changed();
            if (changed)
                delta.payload = difference.toJson();
        } else {
            changed = !deepEquals(state.baseline, current);
            if (changed)
                delta.payload = current;
        }
    }

    state.baseline = current;
    state.hasBaseline = true;

    // The topic was marked but nothing observable changed — a parameter that
    // came back to where it started, or a notification for state this projection
    // does not expose — and nobody is behind. The coalescing that matters most
    // is the event never sent.
    if (!changed && !owed)
        return;

    const SubscriptionEvent snapshot{topic, SubscriptionEvent::Type::Snapshot, revision, current};

    for (auto& client : clients_) {
        if (!client.subscribed[index])
            continue;
        if (client.needsSnapshot[index] || !hadBaseline) {
            deliverLocked(client, snapshot);
            continue;
        }
        // A client that is current and has nothing to be told: this pass exists
        // only for the ones that are behind.
        if (changed)
            deliverLocked(client, delta);
    }
}

void SubscriptionHub::publish(const std::vector<ChangeSource::Change>& changes) {
    auto ordered = changes;
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        return left.revision < right.revision;
    });
    bool dropped = false;
    std::vector<Topic> stillOwed;
    {
        const std::scoped_lock lock(mutex_);
        if (shutdown_)
            return;

        for (auto& client : clients_) {
            client.refusedThisFlush = false;
            client.acceptedThisFlush = false;
        }

        for (const auto& change : ordered) {
            // Nothing marks these, and if something did, a revisioned delta is
            // the wrong shape for a continuous signal.
            if (isContinuousTopic(change.topic))
                continue;
            if (!anySubscriberLocked(change.topic))
                continue;
            publishTopicLocked(change.topic, change.revision);
        }

        // Once per client per flush, after every topic has had its turn, so the
        // unit the disconnect threshold counts is the one it is named for.
        foldFlushOutcomesLocked();
        dropped = dropAbandonedLocked();

        for (std::size_t index = 0; index < TOPIC_COUNT; ++index) {
            const auto topic = static_cast<Topic>(index);
            if (!isContinuousTopic(topic) && owesSnapshotLocked(topic))
                stillOwed.push_back(topic);
        }
    }

    // A client that refused again is owed a snapshot that nothing else will
    // deliver: a quiet project produces no flush at all, so there would be no
    // next attempt. Marking the topic dirty schedules one, which keeps the retry
    // on the coalescing pump that already exists rather than giving it a timer
    // of its own — and keeps the disconnect budget meaning what it says, since
    // each retry is one more flush the client refused.
    if (!stillOwed.empty()) {
        const auto revision = service_.currentRevision();
        for (const auto topic : stillOwed)
            service_.changes().markChanged(topic, revision);
    }

    // Only when the client set actually changed. This runs inside the flush
    // timer's callback, so the reconciliation it asks for has to happen later.
    if (dropped)
        scheduleTopology();
}

void SubscriptionHub::sampleNow() {
    const std::scoped_lock lock(mutex_);
    if (shutdown_)
        return;

    const auto revision = service_.currentRevision();
    for (const auto topic : {Topic::Meters, Topic::Playhead}) {
        if (!anySubscriberLocked(topic))
            continue;

        const SubscriptionEvent event{topic, SubscriptionEvent::Type::Sample, revision,
                                      sampleTopic(topic)};
        for (auto& client : clients_)
            if (client.subscribed[indexOf(topic)])
                deliverLocked(client, event);
    }
}

bool SubscriptionHub::dropAbandonedLocked() {
    if (options_.droppedFlushesBeforeDisconnect <= 0)
        return false;

    const auto abandoned = [this](const Client& client) {
        return client.consecutiveDrops >= options_.droppedFlushesBeforeDisconnect;
    };

    // Collected before anything is removed, and deliberately not from the tail
    // that `remove_if` leaves behind. That tail holds moved-from elements, not
    // the ones that matched: with a stalled client followed by a healthy one,
    // the healthy one is moved over the stalled one and what remains at the end
    // is a hollowed-out copy whose `disconnect` is empty. The stalled client
    // would then be dropped from the hub while keeping its socket, its thread,
    // and its connection slot — silently, because the callback that was supposed
    // to close it no longer exists.
    std::vector<Disconnect> departing;
    for (const auto& client : clients_)
        if (abandoned(client) && client.disconnect != nullptr)
            departing.push_back(client.disconnect);

    const auto removed = std::remove_if(clients_.begin(), clients_.end(), abandoned);
    if (removed == clients_.end())
        return false;
    clients_.erase(removed, clients_.end());

    for (const auto& disconnect : departing)
        disconnect("subscriber is not consuming events");

    releaseIdleTopicsLocked();
    return true;
}

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------

void SubscriptionHub::sendSnapshotsLocked(Client& client, const std::vector<Topic>& topics,
                                          Revision revision, juce::Array<juce::var>& into) {
    for (const auto topic : topics) {
        if (isContinuousTopic(topic))
            continue;

        const auto index = indexOf(topic);
        auto current = projectTopic(topic);

        // Deliberately does not overwrite an existing baseline. Another client
        // may still be current against it, and moving it forward here would
        // silently swallow the change it has not been told about yet. The new
        // subscriber may therefore receive a delta covering ground its snapshot
        // already had, which is harmless: added and updated are upserts keyed by
        // id, so applying one twice lands in the same place.
        if (!topics_[index].hasBaseline) {
            topics_[index].baseline = current;
            topics_[index].hasBaseline = true;
        }

        client.needsSnapshot[index] = false;
        into.add(SubscriptionEvent{topic, SubscriptionEvent::Type::Snapshot, revision,
                                   std::move(current)}
                     .toJson());
    }
}

bool SubscriptionHub::handle(ClientId client, const juce::String& method, const juce::var& params,
                             Completion onComplete) {
    if (!isSubscriptionMethod(method))
        return false;
    jassert(onComplete != nullptr);

    const auto revision = service_.currentRevision();

    // Validated on the calling thread, for the reason `RemoteApiService` does
    // the same: it is a pure function of the request, so a malformed one never
    // occupies the message thread. Against the schema the registry advertises,
    // so a transport never grows its own copy of the contract.
    const auto* operation = OperationRegistry::instance().find(method);
    if (operation == nullptr) {
        onComplete(Response::failure(ErrorCode::UnknownOperation, "unknown operation: " + method,
                                     revision));
        return true;
    }
    if (auto error = validateOperationInput(*operation, params)) {
        onComplete(Response::failure(*error, revision));
        return true;
    }

    std::vector<Topic> requested;
    bool topicsGiven = false;
    if (const auto* array = params["topics"].getArray()) {
        topicsGiven = true;
        for (const auto& entry : *array) {
            const auto topic = parseTopic(entry.toString());
            if (!topic) {
                onComplete(Response::failure(ErrorCode::ValidationFailed,
                                             "unknown topic: " + entry.toString(), revision));
                return true;
            }
            if (std::find(requested.begin(), requested.end(), *topic) == requested.end())
                requested.push_back(*topic);
        }
    }

    // Everything past this point projects live model state, which is message
    // thread only. Copied before the job takes ownership, so the callAsync
    // failure path still has a way to answer.
    auto fallback = onComplete;
    auto gate = gate_;
    auto job = [gate, client, method, params, requested, topicsGiven, revision,
                onComplete = std::move(onComplete)]() mutable {
        Response response;
        {
            const std::scoped_lock lock(gate->mutex);
            response = gate->hub != nullptr
                           ? gate->hub->execute(client, method, params, requested, topicsGiven)
                           : Response::failure(ErrorCode::Cancelled, "subscriptions are shut down",
                                               revision);
        }
        onComplete(std::move(response));
    };

    // Already on the message thread, or there is no message thread to hop to —
    // the headless host, where posting would queue work nothing dispatches.
    if (juce::MessageManager::existsAndIsCurrentThread() ||
        juce::MessageManager::getInstanceWithoutCreating() == nullptr) {
        job();
        return true;
    }

    if (!juce::MessageManager::callAsync(std::move(job)))
        fallback(Response::failure(ErrorCode::Cancelled,
                                   "message loop is shutting down; request not dispatched",
                                   revision));
    return true;
}

Response SubscriptionHub::execute(ClientId client, const juce::String& method,
                                  const juce::var& params, const std::vector<Topic>& requested,
                                  bool topicsGiven) {
    const auto revision = service_.currentRevision();

    Response response;
    {
        const std::scoped_lock lock(mutex_);
        if (shutdown_)
            return Response::failure(ErrorCode::Cancelled, "remote API service is shut down",
                                     revision);

        auto* entry = findLocked(client);
        if (entry == nullptr)
            return Response::failure(ErrorCode::InternalError, "connection is not subscribable",
                                     revision);

        auto result = makeObject();
        juce::Array<juce::var> snapshots;

        if (method == kSubscribe) {
            for (const auto topic : requested)
                entry->subscribed[indexOf(topic)] = true;

            // Subscribing snapshots, and the only thing that stops it is the
            // client saying so.
            //
            // There was a `fromRevision` here that skipped the snapshots when a
            // reconnecting client named the revision the project was still on.
            // It cannot work: `noteModelActivity` publishes events without
            // advancing the revision — a clip's play state, a parameter under
            // automation — so a client can disconnect at revision N, miss one of
            // those, and come back naming N. The revision is a commit cursor and
            // proves only that nothing was committed; establishing event
            // continuity would need a second, per-topic cursor on the wire, and
            // being wrong about it leaves a client silently stale, which is the
            // failure this whole design is meant to make impossible.
            const auto wanted = params["snapshot"];
            const bool asked = wanted.isVoid() || static_cast<bool>(wanted);

            if (asked)
                sendSnapshotsLocked(*entry, requested, revision, snapshots);
            else
                for (const auto topic : requested)
                    if (!isContinuousTopic(topic) && !topics_[indexOf(topic)].hasBaseline) {
                        topics_[indexOf(topic)].baseline = projectTopic(topic);
                        topics_[indexOf(topic)].hasBaseline = true;
                    }
        } else if (method == kUnsubscribe) {
            // No topics means all of them: the shape a client uses when it is
            // going away rather than narrowing what it watches.
            for (std::size_t index = 0; index < TOPIC_COUNT; ++index) {
                const auto topic = static_cast<Topic>(index);
                if (!topicsGiven ||
                    std::find(requested.begin(), requested.end(), topic) != requested.end())
                    entry->subscribed[index] = false;
            }
            releaseIdleTopicsLocked();
        } else if (method == kResync) {
            std::vector<Topic> targets;
            for (std::size_t index = 0; index < TOPIC_COUNT; ++index) {
                const auto topic = static_cast<Topic>(index);
                if (!entry->subscribed[index])
                    continue;
                if (topicsGiven &&
                    std::find(requested.begin(), requested.end(), topic) == requested.end())
                    continue;
                targets.push_back(topic);
            }
            sendSnapshotsLocked(*entry, targets, revision, snapshots);
            // The client has just been handed complete state, so whatever it
            // missed no longer matters.
            entry->consecutiveDrops = 0;
        }

        juce::Array<juce::var> subscribed;
        for (std::size_t index = 0; index < TOPIC_COUNT; ++index)
            if (entry->subscribed[index])
                subscribed.add(juce::String(toString(static_cast<Topic>(index))));

        setProperty(result, "topics", subscribed);
        setProperty(result, "revision", static_cast<juce::int64>(revision));
        if (method != kList && method != kUnsubscribe)
            setProperty(result, "snapshots", snapshots);

        response = Response::success(result, revision);
    }

    // This is the message thread and not a timer callback, so the timers can be
    // reconciled here and the client is subscribed by the time it is told so.
    applyTopology();
    return response;
}

}  // namespace magda::remote
