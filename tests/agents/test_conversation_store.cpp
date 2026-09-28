#include <catch2/catch_test_macros.hpp>

#include "magda/agents/conversation_store.hpp"

using magda::ConversationStore;
using Channel = magda::ConversationStore::Channel;

TEST_CASE("ConversationStore: empty thread renders nothing", "[conversation]") {
    ConversationStore store;
    REQUIRE(store.render(Channel::Arrangement).empty());
}

TEST_CASE("ConversationStore: records and renders an exchange", "[conversation]") {
    ConversationStore store;
    store.record(Channel::Arrangement, "make a bass track", "Created track 'Bass'");

    auto rendered = store.render(Channel::Arrangement);
    REQUIRE(rendered.find("make a bass track") != std::string::npos);
    REQUIRE(rendered.find("Created track 'Bass'") != std::string::npos);
    // The reply is attributed to the assistant, the request to the user.
    REQUIRE(rendered.find("User: make a bass track") != std::string::npos);
    REQUIRE(rendered.find("You: Created track 'Bass'") != std::string::npos);
}

TEST_CASE("ConversationStore: threads are isolated per channel", "[conversation]") {
    ConversationStore store;
    store.record(Channel::Arrangement, "arrange q", "arrange a");
    store.record(Channel::Mixing, "mix q", "mix a");

    auto arrange = store.render(Channel::Arrangement);
    auto mix = store.render(Channel::Mixing);

    REQUIRE(arrange.find("arrange q") != std::string::npos);
    REQUIRE(arrange.find("mix q") == std::string::npos);
    REQUIRE(mix.find("mix q") != std::string::npos);
    REQUIRE(mix.find("arrange q") == std::string::npos);
}

TEST_CASE("ConversationStore: render keeps only the most recent maxTurns", "[conversation]") {
    ConversationStore store;
    for (int i = 0; i < 10; ++i)
        store.record(Channel::Arrangement, "u" + std::to_string(i), "a" + std::to_string(i));

    auto rendered = store.render(Channel::Arrangement, 3);
    // Oldest turns dropped, newest kept, in order.
    REQUIRE(rendered.find("u6") == std::string::npos);
    REQUIRE(rendered.find("u7") != std::string::npos);
    REQUIRE(rendered.find("u9") != std::string::npos);
    REQUIRE(rendered.find("u7") < rendered.find("u9"));
}

TEST_CASE("ConversationStore: empty exchanges are ignored", "[conversation]") {
    ConversationStore store;
    store.record(Channel::Arrangement, "", "");
    REQUIRE(store.render(Channel::Arrangement).empty());
}

TEST_CASE("ConversationStore: long entries are truncated", "[conversation]") {
    ConversationStore store;
    std::string huge(5000, 'x');
    store.record(Channel::Arrangement, "q", huge);

    auto rendered = store.render(Channel::Arrangement);
    REQUIRE(rendered.find("[...]") != std::string::npos);
    REQUIRE(rendered.size() < huge.size());
}

TEST_CASE("ConversationStore: newlines collapse to single spaces", "[conversation]") {
    ConversationStore store;
    store.record(Channel::Arrangement, "q", "line one\n\nline two\tindented");

    auto rendered = store.render(Channel::Arrangement);
    REQUIRE(rendered.find("line one line two indented") != std::string::npos);
}

TEST_CASE("ConversationStore: clear drops one channel, clearAll drops everything",
          "[conversation]") {
    ConversationStore store;
    store.record(Channel::Arrangement, "arrange q", "arrange a");
    store.record(Channel::Mixing, "mix q", "mix a");

    store.clear(Channel::Arrangement);
    REQUIRE(store.render(Channel::Arrangement).empty());
    REQUIRE_FALSE(store.render(Channel::Mixing).empty());

    store.clearAll();
    REQUIRE(store.render(Channel::Mixing).empty());
}
