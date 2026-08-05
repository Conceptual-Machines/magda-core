#define CPPHTTPLIB_WEBSOCKET_MAX_PAYLOAD_LENGTH (256 * 1024)

#include <httplib.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/api/remote_model_bridge.hpp"
#include "magda/daw/api/remote_service.hpp"
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

        beginTest("An edit made in the UI is visible to the next request");
        {
            Fixture fixture;
            const auto before = fixture.service.currentRevision();

            // Not through the API — this is the user doing something in MAGDA
            // while a client is connected, which is what the model bridge is for.
            TrackManager::getInstance().createTrack("By hand", TrackType::Audio);

            const auto reply = fixture.exchange(requestJson("tracks.list", object({})));
            const auto* tracks = reply["result"].getArray();
            expect(tracks != nullptr);
            if (tracks != nullptr)
                expect(tracks->size() == 1);

            // The client's expectedRevision has to go stale, or it would happily
            // overwrite an edit it never saw.
            expect(fixture.service.currentRevision() > before);
        }
    }

  private:
    struct Fixture {
        MagdaApiLive api;
        RemoteApiService service{api};
        std::unique_ptr<ModelChangeBridge> bridge;
        std::unique_ptr<RemoteWebSocketServer> server;

        Fixture() {
            reset();
            bridge = std::make_unique<ModelChangeBridge>(service);

            RemoteWebSocketServer::Options options;
            options.bearerToken = kToken;
            server = std::make_unique<RemoteWebSocketServer>(service, options);
            server->start();
        }

        ~Fixture() {
            server.reset();
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
                httplib::ws::WebSocketClient client(
                    "ws://127.0.0.1:" + std::to_string(server->boundPort()) + "/rpc",
                    {{"Authorization", std::string("Bearer ") + kToken}});
                if (client.connect()) {
                    client.send(payload);
                    client.read(received);
                }
                finished.store(true);
            });

            const auto deadline =
                juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
            while (!finished.load() && juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);

            worker.join();

            juce::var parsed;
            juce::JSON::parse(juce::String(received), parsed);
            return parsed;
        }
    };
};

RemoteWebSocketLiveTest remoteWebSocketLiveTest;

}  // namespace
