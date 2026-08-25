#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/RackInfo.hpp"

// ChainInfo copies through hand-written operations, because a chain element can
// be a nested rack held by unique_ptr and that has to be deep copied. Every
// other field has to be listed by hand there, and one that is forgotten is lost
// silently on the first copy rather than failing to build.
//
// The note range was forgotten exactly that way when pads arrived (#2192): a
// DeviceInfo is copied by value all over the app, so every pad in a copied
// padRack came back answering to every note instead of its own.
//
// This sets every field to something that is not its default and copies both
// ways. A field added to ChainInfo and not to the copy operations fails here.

namespace {

magda::ChainInfo makeFullyPopulatedChain() {
    magda::ChainInfo chain;
    chain.id = 42;
    chain.name = "Snare";
    chain.outputIndex = 3;
    chain.muted = true;
    chain.solo = true;
    chain.bypassed = true;
    chain.volume = -6.0f;
    chain.pan = -0.5f;
    chain.lowNote = 38;
    chain.highNote = 40;
    chain.rootNote = 39;
    chain.expanded = false;

    magda::DeviceInfo device;
    device.id = 7;
    device.name = "Sampler";
    chain.elements.push_back(device);

    auto nested = std::make_unique<magda::RackInfo>();
    nested->id = 5;
    nested->name = "Pad FX";
    chain.elements.push_back(std::move(nested));

    return chain;
}

void requireCarriesEverything(const magda::ChainInfo& copy) {
    CHECK(copy.id == 42);
    CHECK(copy.name == "Snare");
    CHECK(copy.outputIndex == 3);
    CHECK(copy.muted);
    CHECK(copy.solo);
    CHECK(copy.bypassed);
    CHECK(copy.volume == -6.0f);
    CHECK(copy.pan == -0.5f);
    CHECK(copy.lowNote == 38);
    CHECK(copy.highNote == 40);
    CHECK(copy.rootNote == 39);
    CHECK_FALSE(copy.expanded);
    CHECK_FALSE(copy.answersToEveryNote());

    REQUIRE(copy.elements.size() == 2);
    REQUIRE(magda::isDevice(copy.elements[0]));
    CHECK(magda::getDevice(copy.elements[0]).id == 7);
    REQUIRE(magda::isRack(copy.elements[1]));
    CHECK(magda::getRack(copy.elements[1]).id == 5);
}

}  // namespace

TEST_CASE("ChainInfo copy construction carries every field", "[chain][rack]") {
    const auto original = makeFullyPopulatedChain();
    const magda::ChainInfo copy(original);
    requireCarriesEverything(copy);
}

TEST_CASE("ChainInfo copy assignment carries every field", "[chain][rack]") {
    const auto original = makeFullyPopulatedChain();
    magda::ChainInfo copy;
    copy = original;
    requireCarriesEverything(copy);
}

TEST_CASE("ChainInfo copies the nested rack rather than sharing it", "[chain][rack]") {
    const auto original = makeFullyPopulatedChain();
    magda::ChainInfo copy(original);

    REQUIRE(magda::isRack(copy.elements[1]));
    REQUIRE(magda::isRack(original.elements[1]));
    CHECK(&magda::getRack(copy.elements[1]) != &magda::getRack(original.elements[1]));

    magda::getRack(copy.elements[1]).name = "Changed";
    CHECK(magda::getRack(original.elements[1]).name == "Pad FX");
}

TEST_CASE("A chain with no note range answers to every note", "[chain][rack]") {
    const magda::ChainInfo plain;
    CHECK(plain.answersToEveryNote());

    const magda::ChainInfo copy(plain);
    CHECK(copy.answersToEveryNote());
}
