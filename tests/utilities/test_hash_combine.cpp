#include <bitset>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "../third_party/tracktion_engine/modules/tracktion_core/utilities/tracktion_Hash.h"

/**
 * tracktion::hash_combine, which every node in a playback graph gets its
 * identity from (#2085).
 *
 * The inputs it is given are small: a per-type compile-time seed stirred with
 * an EditItemID in the low thousands, then with the ids of a handful of input
 * nodes. A combine that does not avalanche leaves a whole Edit's node ids
 * agreeing in their top thirty-odd bits, which is close enough for two of them
 * to collide on a three-track project. What is asserted
 * here is the property that prevents that, rather than any particular value:
 * pinning the numbers would make an intentional change to the function look
 * like a regression, and the numbers are not a contract with anything.
 */

using tracktion::hash;
using tracktion::hash_combine;

namespace {

/// How many bits differ between two hashes.
int hammingDistance(std::size_t a, std::size_t b) {
    return static_cast<int>(std::bitset<64>(static_cast<std::uint64_t>(a ^ b)).count());
}

/// The seed a node type would use: a hash of its own name, so a large constant
/// with no relationship to the small values it gets combined with.
constexpr std::size_t kNodeTypeSeed = 7653239033668669842ull;

}  // namespace

TEST_CASE("hash_combine avalanches on the inputs node identity actually uses",
          "[hash][node_identity]") {
    // The itemIDs of a small project's tracks: consecutive-ish small integers,
    // which is the case the engine hands it and the case it used to fail.
    std::vector<std::size_t> hashes;
    for (std::size_t itemId = 1000; itemId < 1064; ++itemId)
        hashes.push_back(hash(kNodeTypeSeed, itemId));

    SECTION("neighbouring inputs land far apart") {
        // Half the bits is the expectation for a good mix; a third of them is
        // a floor that a mix has to be badly broken to fall through. The old
        // function scored between one and seven.
        for (std::size_t index = 1; index < hashes.size(); ++index)
            REQUIRE(hammingDistance(hashes[index - 1], hashes[index]) > 20);
    }

    SECTION("the high bits move") {
        // What the collision was made of: every id in an Edit shared its top
        // bits, so a whole graph's worth of them lived in a window a few
        // million wide, and two adjacent ones could differ by thirteen.
        std::set<std::size_t> topBits;
        for (auto value : hashes)
            topBits.insert(value >> 40);

        REQUIRE(topBits.size() > hashes.size() / 2);
    }

    SECTION("no two inputs share a hash") {
        REQUIRE(std::set<std::size_t>(hashes.begin(), hashes.end()).size() == hashes.size());
    }
}

TEST_CASE("hash_combine keeps the properties the graph relies on", "[hash][node_identity]") {
    SECTION("order matters") {
        // A node's identity is its inputs in order, so two nodes fed the same
        // inputs the other way round have to differ.
        auto forwards = kNodeTypeSeed;
        hash_combine(forwards, std::size_t{7});
        hash_combine(forwards, std::size_t{9});

        auto backwards = kNodeTypeSeed;
        hash_combine(backwards, std::size_t{9});
        hash_combine(backwards, std::size_t{7});

        REQUIRE(forwards != backwards);
    }

    SECTION("combining always changes the seed") {
        // Including with a value that hashes to zero: a node whose input
        // contributed nothing would otherwise be indistinguishable from the
        // node that has no input at all.
        auto seed = kNodeTypeSeed;
        hash_combine(seed, std::size_t{0});
        REQUIRE(seed != kNodeTypeSeed);
    }

    SECTION("it is a function of its inputs alone") {
        // Ids are compared across a graph swap, so two runs of the same
        // combine have to agree.
        REQUIRE(hash(kNodeTypeSeed, std::size_t{42}) == hash(kNodeTypeSeed, std::size_t{42}));
    }
}

TEST_CASE("chaining combines does not reintroduce the clustering", "[hash][node_identity]") {
    // The shape a node id is actually built in, and the one that collided: a
    // node seeded by its own type and stirred with its track's itemID, then
    // combined with the id of the node feeding it, which was built the same
    // way from the same itemID. A mix that avalanches once but lets a chain
    // settle back into a narrow band would pass the case above and still lose
    // this.
    constexpr std::size_t kFeedingNodeTypeSeed = 10553084373558490035ull;

    std::vector<std::size_t> ids;
    for (std::size_t track = 0; track < 64; ++track) {
        const auto itemId = 1003 + track * 3;

        auto id = hash(kNodeTypeSeed, itemId);
        hash_combine(id, hash(kFeedingNodeTypeSeed, itemId));

        ids.push_back(id);
    }

    std::set<std::size_t> topBits;
    for (auto value : ids)
        topBits.insert(value >> 40);

    REQUIRE(topBits.size() > ids.size() / 2);
    REQUIRE(std::set<std::size_t>(ids.begin(), ids.end()).size() == ids.size());

    for (std::size_t index = 1; index < ids.size(); ++index)
        REQUIRE(hammingDistance(ids[index - 1], ids[index]) > 20);
}
