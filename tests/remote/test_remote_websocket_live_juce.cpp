#include <httplib.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/api/remote_clients.hpp"
#include "magda/daw/api/remote_model_bridge.hpp"
#include "magda/daw/api/remote_service.hpp"
#include "magda/daw/api/remote_subscriptions.hpp"
#include "magda/daw/api/remote_websocket_server.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"

// The WebSocket transport against the live facade and the real singletons: a
// frame arriving on a socket becomes an actual change to an actual project.
// test_remote_websocket_server.cpp proves the transport behaves against a mock;
// this proves the two halves are connected.
//
// It lives in the JUCE target for the reason every command-executing test does:
// an undoable write constructs ProjectManager through UndoableMutationScope,
// whose constructor starts a timer, and the Catch2 runner has no message system
// for that to be valid in.

namespace {

using namespace magda;
using namespace magda::remote;

constexpr const char* kToken = "live-token-4c71";

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

std::string requestJson(const juce::String& method, const juce::var& params,
                        const juce::var& meta = {}, int id = 1) {
    auto* request = new juce::DynamicObject();
    request->setProperty("jsonrpc", "2.0");
    request->setProperty("id", id);
    request->setProperty("method", method);
    request->setProperty("params", params);
    if (!meta.isVoid())
        request->setProperty("meta", meta);
    return juce::JSON::toString(juce::var(request), true).toStdString();
}

class RemoteWebSocketLiveTest final : public juce::UnitTest {
  public:
    RemoteWebSocketLiveTest() : juce::UnitTest("Remote WebSocket Live", "magda") {}

    void runTest() override {
        beginTest("A frame off the socket creates a real track");
        {
            Fixture fixture;
            const auto before = fixture.service.currentRevision();

            const auto reply = fixture.exchange(requestJson(
                "tracks.create", object({{"name", "Over the wire"}, {"type", "audio"}})));

            expect(reply["error"].isVoid());
            expect(reply["jsonrpc"].toString() == "2.0");

            // The point of the whole exercise: not that the transport answered,
            // but that the project changed.
            const auto trackId = static_cast<int>(reply["result"]["id"]);
            const auto* track = TrackManager::getInstance().getTrack(trackId);
            expect(track != nullptr);
            if (track != nullptr)
                expect(track->name == "Over the wire");

            // The revision the client is told is the one it can write against.
            expect(fixture.service.currentRevision() == before + 1);
            expect(static_cast<juce::int64>(reply["meta"]["revision"]) ==
                   static_cast<juce::int64>(before + 1));
        }

        beginTest("A read over the socket sees what a write over the socket did");
        {
            Fixture fixture;
            fixture.exchange(
                requestJson("tracks.create", object({{"name", "First"}, {"type", "audio"}})));
            const auto reply = fixture.exchange(requestJson("tracks.list", object({})));

            expect(reply["error"].isVoid());
            // tracks.list answers with a bare array, and it arrives as one:
            // result is the operation's output rather than a wrapper around it.
            const auto* tracks = reply["result"].getArray();
            expect(tracks != nullptr);
            if (tracks != nullptr) {
                expect(tracks->size() == 1);
                if (!tracks->isEmpty())
                    expect((*tracks)[0]["name"].toString() == "First");
            }
        }

        beginTest("A stale expectedRevision is refused and changes nothing");
        {
            Fixture fixture;
            fixture.exchange(
                requestJson("tracks.create", object({{"name", "Present"}, {"type", "audio"}})));

            const auto stale = static_cast<juce::int64>(INITIAL_REVISION);
            const auto reply = fixture.exchange(
                requestJson("tracks.create", object({{"name", "Doomed"}, {"type", "audio"}}),
                            object({{"expectedRevision", stale}})));

            // Conflict has no standard JSON-RPC code, so it takes an
            // implementation-defined one and carries the MAGDA name in data.
            expect(static_cast<int>(reply["error"]["code"]) == -32002);
            expect(reply["error"]["data"]["code"].toString() == "conflict");

            // The revision comes back with the rejection, so a client that lost
            // the race can retry without a second round trip to discover it.
            expect(static_cast<juce::int64>(reply["error"]["data"]["revision"]) ==
                   static_cast<juce::int64>(fixture.service.currentRevision()));

            expect(TrackManager::getInstance().getTracks().size() == 1);
        }

        beginTest("A grouping over the wire is one undoable step");
        {
            Fixture fixture;
            const auto first = static_cast<int>(fixture.exchange(requestJson(
                "tracks.create", object({{"name", "Drums"}, {"type", "audio"}})))["result"]["id"]);
            const auto second = static_cast<int>(fixture.exchange(requestJson(
                "tracks.create", object({{"name", "Bass"}, {"type", "audio"}})))["result"]["id"]);

            juce::Array<juce::var> ids;
            ids.add(first);
            ids.add(second);
            const auto reply = fixture.exchange(requestJson(
                "tracks.group", object({{"trackIds", juce::var(ids)}, {"name", "Rhythm"}})));

            expect(reply["error"].isVoid());
            const auto groupId = static_cast<int>(reply["result"]["id"]);
            expect(TrackManager::getInstance().getTrack(groupId) != nullptr);

            // The grouping went through a command inside the request's
            // compound; a handler that wrote through the facade would leave
            // the compound empty and this undo with nothing to revert.
            expect(UndoManager::getInstance().undo());
            expect(TrackManager::getInstance().getTrack(groupId) == nullptr);
            expect(TrackManager::getInstance().getTopLevelTracks().size() == 2);
        }

        beginTest("A move over the wire is one undoable step");
        {
            Fixture fixture;
            const auto first = static_cast<int>(fixture.exchange(requestJson(
                "tracks.create", object({{"name", "Drums"}, {"type", "audio"}})))["result"]["id"]);
            const auto second = static_cast<int>(fixture.exchange(requestJson(
                "tracks.create", object({{"name", "Bass"}, {"type", "audio"}})))["result"]["id"]);

            const auto reply = fixture.exchange(
                requestJson("tracks.move", object({{"trackId", second}, {"position", 1}})));

            expect(reply["error"].isVoid());
            auto order = TrackManager::getInstance().getTopLevelTracks();
            expect(order.size() == 2 && static_cast<int>(order[0]) == second);

            expect(UndoManager::getInstance().undo());
            order = TrackManager::getInstance().getTopLevelTracks();
            expect(order.size() == 2 && static_cast<int>(order[0]) == first);
        }

        beginTest("An edit made in the UI is visible to the next request");
        {
            Fixture fixture;
            const auto before = fixture.service.currentRevision();

            // Not through the API — this is the user doing something in MAGDA
            // while a client is connected, which is what the model bridge is for.
            TrackManager::getInstance().createTrack("By hand", TrackType::Media);

            const auto reply = fixture.exchange(requestJson("tracks.list", object({})));
            const auto* tracks = reply["result"].getArray();
            expect(tracks != nullptr);
            if (tracks != nullptr)
                expect(tracks->size() == 1);

            // The client's expectedRevision has to go stale, or it would happily
            // overwrite an edit it never saw.
            expect(fixture.service.currentRevision() > before);
        }

        beginTest("A UI edit reaches a subscribed client through the real flush timer");
        {
            Fixture fixture;

            // Everything else in the remote suite drives the coalescing window by
            // hand, because the Catch2 runner has no MessageManager for a timer
            // to live on. Here there is one, so this is the only place the 30 Hz
            // pump — and the hop from a model listener to a socket write — is
            // actually exercised.
            const auto pushed = fixture.subscribeThenObserve({"tracks"}, [] {
                TrackManager::getInstance().createTrack("By hand", TrackType::Media);
            });

            expect(pushed["method"].toString() == "subscriptions.event");
            expect(pushed["id"].isVoid());
            expect(pushed["params"]["topic"].toString() == "tracks");
            const auto* added = pushed["params"]["payload"]["added"].getArray();
            expect(added != nullptr);
            if (added != nullptr && !added->isEmpty())
                expect((*added)[0]["name"].toString() == "By hand");
        }

        beginTest("Snapshots are projected on the message thread, not the socket's");
        {
            Fixture fixture;

            // `selection`, `session`, and `transport` all project through
            // helpers that assert the message thread, and this target does not
            // relax that assertion. Subscribing arrives on a connection thread,
            // so a hub that projected where the request landed would trip here —
            // which is the whole reason handle() answers through a callback.
            juce::Array<juce::var> topics;
            topics.add("selection");
            topics.add("session");
            topics.add("transport");

            const auto reply = fixture.exchange(
                requestJson("subscriptions.subscribe", object({{"topics", topics}})));

            expect(reply["error"].isVoid());
            const auto* snapshots = reply["result"]["snapshots"].getArray();
            expect(snapshots != nullptr);
            if (snapshots != nullptr)
                expect(snapshots->size() == 3);
        }

        beginTest("The playhead is sampled on its own timer, with nothing marking it");
        {
            Fixture fixture;

            // Nothing in the model ever says the playhead moved — it moves with
            // the transport and notifies no one. A sample arriving at all is the
            // sampling timer working, which is the half of #1857 that no model
            // notification can drive.
            const auto pushed = fixture.subscribeThenObserve({"playhead"}, [] {});

            expect(pushed["method"].toString() == "subscriptions.event");
            expect(pushed["params"]["topic"].toString() == "playhead");
            expect(pushed["params"]["type"].toString() == "sample");
            expect(pushed["params"]["payload"].hasProperty("positionBeats"));
        }
    }

  private:
    struct Fixture {
        MagdaApiLive api;
        RemoteApiService service{api};
        std::unique_ptr<ModelChangeBridge> bridge;
        std::unique_ptr<SubscriptionHub> subscriptions;
        /// Declared before `server`, which holds a pointer to it. Without a
        /// registry the transport refuses everything, subscribing included
        /// (#1860), and these tests are about live model behaviour rather than
        /// about who is allowed to watch it.
        RemoteClientRegistry clients;
        std::unique_ptr<RemoteWebSocketServer> server;

        Fixture() {
            reset();
            bridge = std::make_unique<ModelChangeBridge>(service);

            SubscriptionHub::Options hubOptions;
            // Faster than the shipping 20 Hz so a sampling test spends less of
            // its budget waiting. It is still the real timer, on the real
            // message thread — which is the part being exercised.
            hubOptions.samplingIntervalMs = 10;
            subscriptions = std::make_unique<SubscriptionHub>(api, service, hubOptions);

            // These clients send no `?client=`, so they are all `unknown`.
            clients.setScopes(ANONYMOUS_CLIENT, allScopes());

            RemoteWebSocketServer::Options options;
            options.bearerToken = kToken;
            options.clients = &clients;
            server = std::make_unique<RemoteWebSocketServer>(service, options, subscriptions.get());
            server->start();
        }

        ~Fixture() {
            server.reset();
            subscriptions.reset();
            bridge.reset();
            reset();
        }

        Fixture(const Fixture&) = delete;
        Fixture& operator=(const Fixture&) = delete;

        static void reset() {
            AutomationManager::getInstance().clearAll();
            ClipManager::getInstance().clearAllClips();
            TrackManager::getInstance().clearAllTracks();
            UndoManager::getInstance().clearHistory();
            SelectionManager::getInstance().clearSelection();
        }

        /**
         * @brief One request over a real socket, from a thread that is not this one.
         *
         * Both halves are load-bearing. The socket work has to happen elsewhere
         * because a blocking read here would never return: dispatch hops to the
         * message thread, and the message thread is this one, so the handler
         * could not run until the read gave up. Meanwhile this thread has to
         * keep pumping, because that hop is what executes the operation.
         *
         * That is exactly the arrangement the real app is in, which is why it is
         * worth reproducing rather than working around.
         */
        juce::var exchange(const std::string& payload, int timeoutMs = 5000) {
            std::atomic<bool> finished{false};
            std::string received;

            std::thread worker([&] {
                httplib::ws::WebSocketClient client(url(), headers());
                if (client.connect()) {
                    client.send(payload);
                    client.read(received);
                }
                finished.store(true);
            });

            pumpUntil(finished, timeoutMs);
            worker.join();

            juce::var parsed;
            juce::JSON::parse(juce::String(received), parsed);
            return parsed;
        }

        /**
         * @brief Subscribe over a real socket, cause something, and read what is pushed.
         *
         * The same split as `exchange`, for the same reason and one more: the
         * flush timer and the sampling timer both live on the message thread, so
         * this thread pumping is what produces the event the worker is waiting
         * for.
         */
        juce::var subscribeThenObserve(const std::vector<const char*>& topics,
                                       const std::function<void()>& cause, int timeoutMs = 15000) {
            juce::Array<juce::var> names;
            for (const auto* topic : topics)
                names.add(juce::String(topic));

            std::atomic<bool> subscribed{false};
            std::atomic<bool> finished{false};
            std::string reply;
            std::string event;

            std::thread worker([&] {
                httplib::ws::WebSocketClient client(url(), headers());
                if (client.connect()) {
                    client.send(
                        requestJson("subscriptions.subscribe", object({{"topics", names}})));
                    if (client.read(reply) == httplib::ws::ReadResult::Text) {
                        // Only now is the subscription registered, so only now is
                        // it safe to cause the change it is meant to observe.
                        subscribed.store(true);
                        client.read(event);
                    }
                }
                subscribed.store(true);
                finished.store(true);
            });

            pumpUntil(subscribed, timeoutMs);
            cause();
            pumpUntil(finished, timeoutMs);
            worker.join();

            juce::var parsed;
            juce::JSON::parse(juce::String(event), parsed);
            return parsed;
        }

        std::string url() const {
            return "ws://127.0.0.1:" + std::to_string(server->boundPort()) + "/rpc";
        }

        static httplib::Headers headers() {
            return {{"Authorization", std::string("Bearer ") + kToken}};
        }

        static void pumpUntil(const std::atomic<bool>& done, int timeoutMs) {
            const auto deadline =
                juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
            while (!done.load() && juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        }
    };
};

RemoteWebSocketLiveTest remoteWebSocketLiveTest;

}  // namespace
