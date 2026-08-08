// The permission model (#1860): scopes, grants, revocation, and the audit log.
//
// Three layers, tested where each one lives:
//
//  - `remote_scopes`: the vocabulary, and the normalisation that decides two
//    spellings of a client name are one client.
//  - `RemoteClientRegistry`: grants, defaults, connections, disconnection.
//  - `RemoteApiService`: the enforcement itself, and what it records.
//
// What the *transports* do with all of this — the query parameter, `clientInfo`,
// and whether a denial survives the round trip — is in
// test_remote_permission_conformance.cpp, which drives both of them over real
// sockets against the same table of cases.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "MockMagdaApi.hpp"
#include "RemoteTestScopes.hpp"
#include "magda/daw/api/remote_audit.hpp"
#include "magda/daw/api/remote_clients.hpp"
#include "magda/daw/api/remote_service.hpp"

using namespace magda;
using namespace magda::remote;
using magda::test::MockMagdaApi;

namespace {

struct MessageThreadRelaxation {
    ScopedMessageThreadAssertionDisabler disabler;
};

juce::var emptyInput() {
    return juce::var(new juce::DynamicObject());
}

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> fields) {
    auto* result = new juce::DynamicObject();
    for (const auto& [key, value] : fields)
        result->setProperty(key, value);
    return result;
}

/// One operation, run to completion. Dispatch executes inline with no message
/// thread to hop to, so this is synchronous.
Response run(RemoteApiService& service, const juce::String& name, const juce::var& input,
             const RequestContext& context) {
    Response captured;
    int completions = 0;
    service.dispatch(name, input, context, [&](Response response) {
        captured = std::move(response);
        ++completions;
    });
    REQUIRE(completions == 1);
    return captured;
}

RequestContext contextWith(ScopeSet scopes, const juce::String& client = "test-client") {
    RequestContext context;
    context.clientId = "ws:1:1";
    context.clientName = client;
    context.transport = TRANSPORT_WEBSOCKET;
    context.scopes = scopes;
    return context;
}

}  // namespace

// ===========================================================================
// The vocabulary
// ===========================================================================

TEST_CASE("Every scope round-trips through its wire name", "[remote][permissions][scopes]") {
    for (const auto scope : allScopeValues()) {
        const auto name = scopeName(scope);
        REQUIRE(name.isNotEmpty());
        REQUIRE(scopeFromName(name) == scope);
    }
    REQUIRE(allScopeValues().size() == SCOPE_COUNT);
}

TEST_CASE("An unknown scope name is nothing rather than a guess", "[remote][permissions][scopes]") {
    // The forwards-compatibility rule: a config written by a newer MAGDA loads
    // with the scopes this build understands instead of failing shut or, worse,
    // mapping an unrecognised word onto whichever scope happened to be first.
    REQUIRE_FALSE(scopeFromName("admin").has_value());
    REQUIRE_FALSE(scopeFromName("").has_value());
    REQUIRE_FALSE(scopeFromName("READ").has_value());
}

TEST_CASE("A scope set is a set", "[remote][permissions][scopes]") {
    ScopeSet scopes;
    REQUIRE(scopes.empty());
    REQUIRE_FALSE(scopes.has(Scope::Read));

    scopes.add(Scope::Read).add(Scope::Edit);
    REQUIRE(scopes.has(Scope::Read));
    REQUIRE(scopes.has(Scope::Edit));
    REQUIRE_FALSE(scopes.has(Scope::Session));

    scopes.remove(Scope::Edit);
    REQUIRE_FALSE(scopes.has(Scope::Edit));

    REQUIRE(allScopes().containsAll(ScopeSet{Scope::Edit, Scope::Session}));
    REQUIRE_FALSE(ScopeSet{Scope::Read}.containsAll(ScopeSet{Scope::Read, Scope::Edit}));
}

TEST_CASE("Stored bits cannot grant a scope this build has no name for",
          "[remote][permissions][scopes]") {
    // Grants are persisted, so the bit pattern is an input from a file. A bit
    // above the known set would otherwise become a permission nothing could
    // display, revoke, or explain.
    const auto restored = ScopeSet::fromBits(0xFFFFFFFFu);
    REQUIRE(restored == allScopes());
}

TEST_CASE("Scopes serialise as names and survive the round trip", "[remote][permissions][scopes]") {
    const ScopeSet original{Scope::Read, Scope::Transport};
    const auto json = scopesToJson(original);
    REQUIRE(json.isArray());
    REQUIRE(json.getArray()->size() == 2);
    REQUIRE(scopesFromJson(json) == original);

    // Unknown names are dropped, known ones kept — a mixed array from a newer
    // build downgrades rather than failing.
    juce::Array<juce::var> mixed;
    mixed.add("edit");
    mixed.add("teleport");
    REQUIRE(scopesFromJson(juce::var(mixed)) == ScopeSet{Scope::Edit});
}

TEST_CASE("Client names normalise to one key per client", "[remote][permissions][scopes]") {
    REQUIRE(normaliseClientName("Cursor") == "cursor");
    REQUIRE(normaliseClientName("  cursor  ") == "cursor");
    REQUIRE(normaliseClientName("Claude Code (v2)") == "claude-code-v2");
    REQUIRE(normaliseClientName("claude.code") == "claude.code");

    // Anything that survives none of it becomes the anonymous bucket, which is
    // a real client entry rather than a rejection.
    REQUIRE(normaliseClientName("") == ANONYMOUS_CLIENT);
    REQUIRE(normaliseClientName("!!!") == ANONYMOUS_CLIENT);

    // Bounded: this ends up in a config file and a settings table.
    const juce::String long_(std::string(500, 'a'));
    REQUIRE(normaliseClientName(long_).length() == MAX_CLIENT_NAME_LENGTH);
}

// ===========================================================================
// The registry
// ===========================================================================

TEST_CASE("A client MAGDA has never seen is read-only", "[remote][permissions][registry]") {
    RemoteClientRegistry registry;
    REQUIRE_FALSE(registry.peekScopes("cursor").has_value());

    const auto scopes = registry.scopesFor("cursor");
    REQUIRE(scopes == defaultClientScopes());
    REQUIRE(scopes.has(Scope::Read));
    REQUIRE_FALSE(scopes.has(Scope::Edit));

    // Asking registered it, which is what puts a client in the settings list
    // the moment it connects rather than only once the user goes looking.
    REQUIRE(registry.peekScopes("cursor").has_value());
    REQUIRE(registry.grants().size() == 1);
}

TEST_CASE("Granting a scope takes effect on the next lookup", "[remote][permissions][registry]") {
    RemoteClientRegistry registry;
    REQUIRE(registry.scopesFor("cursor") == defaultClientScopes());

    registry.setScopes("cursor", ScopeSet{Scope::Edit});
    const auto scopes = registry.scopesFor("cursor");
    REQUIRE(scopes.has(Scope::Edit));
    // `read` is forced on: a grant without it would describe a client that may
    // write but not look, which no operation set expresses.
    REQUIRE(scopes.has(Scope::Read));

    registry.setScopes("cursor", ScopeSet{});
    REQUIRE(registry.scopesFor("cursor") == ScopeSet{Scope::Read});
}

TEST_CASE("Two spellings of a name are one client", "[remote][permissions][registry]") {
    RemoteClientRegistry registry;
    registry.setScopes("Cursor", ScopeSet{Scope::Edit});
    REQUIRE(registry.scopesFor("cursor").has(Scope::Edit));
    REQUIRE(registry.scopesFor("  CURSOR ").has(Scope::Edit));
    REQUIRE(registry.grants().size() == 1);
}

TEST_CASE("Forgetting a client returns it to read-only", "[remote][permissions][registry]") {
    RemoteClientRegistry registry;
    registry.setScopes("cursor", allScopes());
    registry.forget("cursor");

    REQUIRE(registry.grants().empty());
    REQUIRE(registry.scopesFor("cursor") == defaultClientScopes());
}

TEST_CASE("Grants persist as JSON and reload", "[remote][permissions][registry]") {
    RemoteClientRegistry saved;
    saved.setScopes("cursor", ScopeSet{Scope::Edit, Scope::Transport});
    saved.setScopes("monitor", ScopeSet{});

    const auto json = saved.grantsToJson();

    RemoteClientRegistry loaded;
    loaded.loadGrantsFromJson(json);
    REQUIRE(loaded.grants().size() == 2);
    REQUIRE(loaded.scopesFor("cursor") == ScopeSet{Scope::Read, Scope::Edit, Scope::Transport});
    REQUIRE(loaded.scopesFor("monitor") == ScopeSet{Scope::Read});
}

TEST_CASE("A hand-edited or newer grants file loads without granting nonsense",
          "[remote][permissions][registry]") {
    juce::Array<juce::var> entries;
    entries.add(object({{"name", "cursor"}, {"scopes", [] {
                                                 juce::Array<juce::var> names;
                                                 names.add("edit");
                                                 names.add("root");  // not a scope
                                                 return juce::var(names);
                                             }()}}));
    // No name at all: normalises to the anonymous bucket rather than being
    // dropped silently.
    entries.add(object({{"scopes", juce::var(juce::Array<juce::var>{})}}));

    RemoteClientRegistry registry;
    registry.loadGrantsFromJson(juce::var(entries));

    REQUIRE(registry.scopesFor("cursor") == ScopeSet{Scope::Read, Scope::Edit});
    REQUIRE(registry.scopesFor(ANONYMOUS_CLIENT) == ScopeSet{Scope::Read});
}

TEST_CASE("Loading grants does not fire the change handler", "[remote][permissions][registry]") {
    // Otherwise startup would answer the notification by writing the config file
    // back out with exactly what it just read from it.
    RemoteClientRegistry registry;
    int changes = 0;
    registry.setChangeHandler([&] { ++changes; });

    juce::Array<juce::var> entries;
    entries.add(object({{"name", "cursor"}, {"scopes", scopesToJson(allScopes())}}));
    registry.loadGrantsFromJson(juce::var(entries));

    REQUIRE(changes == 0);
    registry.setScopes("cursor", ScopeSet{Scope::Edit});
    REQUIRE(changes == 1);
}

TEST_CASE("A grant set to what it already is changes nothing", "[remote][permissions][registry]") {
    // The config file is rewritten from the change handler, so a no-op write
    // would churn the user's disk every time the settings page repainted.
    RemoteClientRegistry registry;
    registry.setScopes("cursor", ScopeSet{Scope::Edit});

    int changes = 0;
    registry.setChangeHandler([&] { ++changes; });
    registry.setScopes("cursor", ScopeSet{Scope::Edit});
    REQUIRE(changes == 0);

    registry.setScopes("cursor", ScopeSet{Scope::Session});
    REQUIRE(changes == 1);
}

TEST_CASE("Connections are listed and disconnected by handle", "[remote][permissions][registry]") {
    RemoteClientRegistry registry;

    ConnectedClient first;
    first.connectionId = "ws:1:1";
    first.name = "cursor";
    first.transport = TRANSPORT_WEBSOCKET;
    registry.noteConnected(first);

    ConnectedClient second;
    second.connectionId = "ws:1:2";
    second.name = "cursor";
    second.transport = TRANSPORT_WEBSOCKET;
    registry.noteConnected(second);

    REQUIRE(registry.connectionCount() == 2);

    std::vector<juce::String> closed;
    registry.setDisconnectHandler(TRANSPORT_WEBSOCKET, [&](const juce::String& handle) {
        closed.push_back(handle);
        registry.noteDisconnected(handle);
        return true;
    });

    REQUIRE(registry.disconnect("ws:1:1"));
    REQUIRE(closed.size() == 1);
    REQUIRE(registry.connectionCount() == 1);

    // A handle that is not live is not an error to ask about; it is simply not
    // there any more, which is the common case when a settings row is clicked
    // a moment after the client went away.
    REQUIRE_FALSE(registry.disconnect("ws:1:1"));
}

TEST_CASE("Disconnecting a client closes every connection it holds",
          "[remote][permissions][registry]") {
    RemoteClientRegistry registry;
    for (int index = 1; index <= 3; ++index) {
        ConnectedClient client;
        client.connectionId = "ws:1:" + juce::String(index);
        client.name = index == 3 ? "other" : "cursor";
        client.transport = TRANSPORT_WEBSOCKET;
        registry.noteConnected(client);
    }

    registry.setDisconnectHandler(TRANSPORT_WEBSOCKET, [&](const juce::String& handle) {
        registry.noteDisconnected(handle);
        return true;
    });

    REQUIRE(registry.disconnectClient("cursor") == 2);
    REQUIRE(registry.connectionCount() == 1);
    REQUIRE(registry.connections().front().name == "other");
}

TEST_CASE("A connection whose transport has gone cannot be disconnected",
          "[remote][permissions][registry]") {
    // What the settings dialog hits if the user switches the remote API off
    // between the list being drawn and the button being pressed.
    RemoteClientRegistry registry;
    ConnectedClient client;
    client.connectionId = "ws:1:1";
    client.name = "cursor";
    client.transport = TRANSPORT_WEBSOCKET;
    registry.noteConnected(client);

    REQUIRE_FALSE(registry.disconnect("ws:1:1"));
}

// ===========================================================================
// Enforcement
// ===========================================================================

TEST_CASE("A read-only client can read", "[remote][permissions][service]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.project_.info.tempo = 128.0;
    RemoteApiService service(api);

    const auto response =
        run(service, "project.get", emptyInput(), contextWith(defaultClientScopes()));
    REQUIRE(response.ok);
    REQUIRE(static_cast<double>(response.result["tempo"]) == 128.0);
}

TEST_CASE("A read-only client cannot mutate", "[remote][permissions][service]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    api.project_.info.tempo = 120.0;
    RemoteApiService service(api);

    const auto response = run(service, "project.setTempo", object({{"tempo", 140.0}}),
                              contextWith(defaultClientScopes()));

    REQUIRE_FALSE(response.ok);
    REQUIRE(response.error.code == ErrorCode::PermissionDenied);
    // The scope that would have allowed it, by name — a client should be able
    // to tell the user what to grant rather than just that it failed.
    REQUIRE(response.error.message.contains("edit"));
    // And nothing happened.
    REQUIRE(api.project_.info.tempo == 120.0);
}

TEST_CASE("Each write scope admits only its own operations", "[remote][permissions][service]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    struct Case {
        const char* operation;
        juce::var input;
        Scope required;
    };
    const std::vector<Case> cases = {
        {"project.setTempo", object({{"tempo", 130.0}}), Scope::Edit},
        {"transport.play", emptyInput(), Scope::Transport},
        {"session.stopAll", emptyInput(), Scope::Session},
    };

    for (const auto& testCase : cases) {
        // Granted: it runs. Whether the handler then succeeds is not this
        // test's business — only that it was not refused for permission.
        const auto allowed = run(service, testCase.operation, testCase.input,
                                 contextWith(ScopeSet{Scope::Read, testCase.required}));
        REQUIRE(allowed.error.code != ErrorCode::PermissionDenied);

        // Every other write scope: refused. This is the property that makes the
        // split worth having — a transport grant must not become an edit grant.
        for (const auto other : allScopeValues()) {
            if (other == testCase.required || other == Scope::Read)
                continue;
            const auto refused = run(service, testCase.operation, testCase.input,
                                     contextWith(ScopeSet{Scope::Read, other}));
            REQUIRE_FALSE(refused.ok);
            REQUIRE(refused.error.code == ErrorCode::PermissionDenied);
        }
    }
}

TEST_CASE("Permission is checked before the input is validated", "[remote][permissions][service]") {
    // A refusal that varied with whether the payload was well formed would be
    // an oracle for a schema the client is not allowed to reach.
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    const auto garbage = run(service, "project.setTempo", object({{"nonsense", true}}),
                             contextWith(defaultClientScopes()));
    REQUIRE(garbage.error.code == ErrorCode::PermissionDenied);

    // With the scope, the same payload fails validation — so the two checks are
    // genuinely in this order rather than the input happening to be valid.
    const auto validated = run(service, "project.setTempo", object({{"nonsense", true}}),
                               contextWith(ScopeSet{Scope::Read, Scope::Edit}));
    REQUIRE(validated.error.code == ErrorCode::ValidationFailed);
}

TEST_CASE("A revoked grant is not replayed out of the idempotency cache",
          "[remote][permissions][service]") {
    // The one path where revocation could fail to take effect immediately: a
    // cached response is a previous success, and replaying one to a client
    // whose grant has since been withdrawn would hand it the answer anyway.
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);

    auto granted = contextWith(ScopeSet{Scope::Read, Scope::Edit});
    granted.requestId = "retry-1";
    const auto first = run(service, "project.setTempo", object({{"tempo", 130.0}}), granted);
    REQUIRE(first.ok);

    auto revoked = contextWith(defaultClientScopes());
    revoked.requestId = "retry-1";
    const auto replay = run(service, "project.setTempo", object({{"tempo", 130.0}}), revoked);
    REQUIRE_FALSE(replay.ok);
    REQUIRE(replay.error.code == ErrorCode::PermissionDenied);
}

TEST_CASE("Every declared write requires more than read", "[remote][permissions][registry]") {
    // The guarantee behind the scope policy table: an operation added without a
    // policy entry keeps the `read` default, and would then be callable by every
    // read-only client. Nothing else in the system would notice.
    for (const auto& operation : OperationRegistry::instance().operations()) {
        if (operation.access == OperationAccess::Write) {
            INFO("operation: " << operation.name);
            REQUIRE(operation.requiredScope != Scope::Read);
        }
    }
}

TEST_CASE("system.describe publishes the scope each operation needs",
          "[remote][permissions][registry]") {
    // A client should be able to present the user with what to grant before it
    // tries something, rather than after being refused.
    const auto described = OperationRegistry::instance().describe();
    const auto* operations = described["operations"].getArray();
    REQUIRE(operations != nullptr);

    bool sawWrite = false;
    for (const auto& entry : *operations) {
        const auto scope = entry["requiredScope"].toString();
        REQUIRE(scopeFromName(scope).has_value());
        if (entry["name"].toString() == "transport.play") {
            REQUIRE(scope == "transport");
            sawWrite = true;
        }
    }
    REQUIRE(sawWrite);
}

// ===========================================================================
// The audit log
// ===========================================================================

TEST_CASE("A dispatched request is recorded with its outcome", "[remote][permissions][audit]") {
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteAuditLog log;
    service.setAuditLog(&log);

    auto context = contextWith(defaultClientScopes(), "cursor");
    context.requestId = "req-7";
    run(service, "project.get", emptyInput(), context);
    run(service, "project.setTempo", object({{"tempo", 140.0}}), context);

    const auto entries = log.entries();
    REQUIRE(entries.size() == 2);

    REQUIRE(entries[0].client == "cursor");
    REQUIRE(entries[0].transport == TRANSPORT_WEBSOCKET);
    REQUIRE(entries[0].connectionId == "ws:1:1");
    REQUIRE(entries[0].operation == "project.get");
    REQUIRE(entries[0].requestId == "req-7");
    REQUIRE(entries[0].outcome == AuditOutcome::Ok);
    REQUIRE(entries[0].timestampMs > 0);

    REQUIRE(entries[1].operation == "project.setTempo");
    REQUIRE(entries[1].outcome == AuditOutcome::Denied);
    // The scope that would have allowed it — the one word a user would act on.
    REQUIRE(entries[1].detail == "edit");
}

TEST_CASE("A failure is recorded by code, never by message", "[remote][permissions][audit]") {
    // A validation message quotes the offending value, which is client input
    // and may be anything at all — including something the client should not
    // have sent and the user should not have in a buffer they may screenshot.
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteAuditLog log;
    service.setAuditLog(&log);

    run(service, "project.setTempo", object({{"tempo", "not-a-number"}}),
        contextWith(ScopeSet{Scope::Read, Scope::Edit}, "cursor"));

    const auto entries = log.entries();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].outcome == AuditOutcome::Failed);
    REQUIRE(entries[0].detail == "validation_failed");
    REQUIRE_FALSE(entries[0].detail.contains("not-a-number"));
}

TEST_CASE("In-process callers are not audited", "[remote][permissions][audit]") {
    // The subscription hub projecting a snapshot and the tests themselves would
    // otherwise bury the remote traffic the log exists to show.
    const MessageThreadRelaxation relaxation;
    MockMagdaApi api;
    RemoteApiService service(api);
    RemoteAuditLog log;
    service.setAuditLog(&log);

    run(service, "project.get", emptyInput(), magda::test::fullyGrantedContext());
    REQUIRE(log.entries().empty());
}

TEST_CASE("The audit log is bounded and says when it lost entries",
          "[remote][permissions][audit]") {
    RemoteAuditLog log(4);
    for (int index = 0; index < 10; ++index) {
        AuditEntry entry;
        entry.client = "cursor";
        entry.operation = "project.get";
        entry.detail = juce::String(index);
        log.record(entry);
    }

    REQUIRE(log.size() == 4);
    REQUIRE(log.totalRecorded() == 10);
    // Oldest dropped, newest kept.
    REQUIRE(log.entries().front().detail == "6");
    REQUIRE(log.entries().back().detail == "9");

    REQUIRE(log.recent(2).size() == 2);
    REQUIRE(log.recent(2).back().detail == "9");
    // Asking for more than there is returns what there is.
    REQUIRE(log.recent(100).size() == 4);
}

TEST_CASE("Entries can be read back for one client", "[remote][permissions][audit]") {
    RemoteAuditLog log;
    for (const auto* name : {"cursor", "monitor", "cursor"}) {
        AuditEntry entry;
        entry.client = name;
        entry.operation = "project.get";
        log.record(entry);
    }
    REQUIRE(log.forClient("cursor").size() == 2);
    REQUIRE(log.forClient("monitor").size() == 1);
    REQUIRE(log.forClient("nobody").empty());
}

// ===========================================================================
// Redaction
// ===========================================================================

TEST_CASE("A registered secret never survives into a log line",
          "[remote][permissions][redaction]") {
    forgetAllRemoteSecrets();
    const juce::String token = "9f3a1c7e5b2d8406";
    registerRemoteSecret(token);

    const auto redacted = redactSecrets("connecting with token " + token + " to the endpoint");
    REQUIRE_FALSE(redacted.contains(token));
    REQUIRE(redacted.contains("***"));

    forgetRemoteSecret(token);
    REQUIRE(redactSecrets(token).contains(token));
    forgetAllRemoteSecrets();
}

TEST_CASE("A bearer credential MAGDA never held is still masked",
          "[remote][permissions][redaction]") {
    // A client's own malformed `Authorization` header, echoed into a rejection
    // reason: not a value MAGDA generated, so not one it could have registered.
    forgetAllRemoteSecrets();
    const auto redacted = redactSecrets("rejected Authorization: Bearer sk-live-abcdef123456 here");
    REQUIRE_FALSE(redacted.contains("sk-live-abcdef123456"));
    REQUIRE(redacted.contains("Bearer ***"));
    // The text either side survives, or the log stops being diagnosable.
    REQUIRE(redacted.contains("rejected"));
    REQUIRE(redacted.contains("here"));
}

TEST_CASE("Redaction terminates on a bearer scheme with nothing after it",
          "[remote][permissions][redaction]") {
    forgetAllRemoteSecrets();
    // The empty-value case is what would otherwise scan the same position for
    // ever, because there is nothing to replace and therefore nothing to
    // advance past.
    REQUIRE(redactSecrets("Bearer ").contains("Bearer"));
    REQUIRE(redactSecrets("Bearer  Bearer ").isNotEmpty());
}

TEST_CASE("A secret too short to be one is refused", "[remote][permissions][redaction]") {
    // A one- or two-character "secret" matches somewhere in almost every
    // message and would turn the whole log into asterisks.
    forgetAllRemoteSecrets();
    registerRemoteSecret("ab");
    REQUIRE(redactSecrets("a table of absolute values").contains("table"));
    forgetAllRemoteSecrets();
}

TEST_CASE("An audit detail is redacted on the way in", "[remote][permissions][redaction]") {
    // At the one point every entry passes through, rather than at each call
    // site — where each new one is a chance to forget.
    forgetAllRemoteSecrets();
    const juce::String token = "abcdef0123456789";
    registerRemoteSecret(token);

    RemoteAuditLog log;
    AuditEntry entry;
    entry.client = "cursor";
    entry.operation = "connection.rejected";
    entry.detail = "token " + token + " refused";
    log.record(entry);

    REQUIRE_FALSE(log.entries().front().detail.contains(token));
    forgetAllRemoteSecrets();
}

TEST_CASE("A file is named without saying where it lives", "[remote][permissions][redaction]") {
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("remote-api-4021.json");
    const auto named = redactedFileName(file);
    REQUIRE(named == "remote-api-4021.json");
    REQUIRE_FALSE(named.contains(juce::File::getSeparatorString()));
    REQUIRE(redactedFileName(juce::File()).isEmpty());
}
