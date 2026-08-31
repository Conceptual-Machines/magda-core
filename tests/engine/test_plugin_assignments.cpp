#include <catch2/catch_test_macros.hpp>

#include "magda/daw/audio/plugins/engine/PluginAssignments.hpp"

/**
 * @file test_plugin_assignments.cpp
 * @brief Who a plugin load belongs to, decided at runtime (#2261).
 *
 * The identity these tests are about used to be a counter on DeviceInfo, minted
 * by whichever call site remembered to. What is asserted here is the property
 * that replaced it: nothing in the model carries assignment identity, so no
 * copy of a device can inherit one, and a device nobody registered has none to
 * lend. Every case below is stated as what a load is allowed to complete onto.
 */

namespace adapter = magda::daw::audio::engine_adapter;

namespace {

magda::engine::DeviceKey fx(magda::DeviceId id) {
    return {magda::ChainSegment::Fx, id};
}

magda::engine::DeviceKey postFx(magda::DeviceId id) {
    return {magda::ChainSegment::PostFx, id};
}

magda::engine::DeviceKey mixerAnalysis(magda::DeviceId id) {
    return {magda::ChainSegment::MixerAnalysis, id};
}

}  // namespace

TEST_CASE("A load completes onto the assignment it was requested for", "[plugins][assignments]") {
    adapter::PluginAssignments assignments;
    assignments.assign(fx(1));

    const auto request = assignments.request(fx(1));
    CHECK(assignments.accepts(request));

    // Nothing about the assignment changes because the device was edited,
    // enriched by a scan, restored from a preset or recompiled into a plan:
    // none of those touch the coordinator, so the load is still wanted.
    CHECK(assignments.accepts(request));
}

TEST_CASE("A deleted device cannot accept the load it asked for", "[plugins][assignments]") {
    adapter::PluginAssignments assignments;
    assignments.assign(fx(1));

    const auto request = assignments.request(fx(1));
    assignments.release(fx(1));

    CHECK_FALSE(assignments.accepts(request));
    CHECK_FALSE(static_cast<bool>(assignments.current(fx(1))));
}

TEST_CASE("A replaced plugin cannot accept the load its predecessor asked for",
          "[plugins][assignments]") {
    // The slot survives and keeps its id: what changed is which plugin it is
    // asking for. Metadata cannot tell this from a moved file or a corrected
    // vendor, which is the whole reason identity is not read off the model.
    adapter::PluginAssignments assignments;
    assignments.assign(fx(1));
    const auto request = assignments.request(fx(1));

    assignments.assign(fx(1));

    CHECK_FALSE(assignments.accepts(request));
    CHECK(assignments.accepts(assignments.request(fx(1))));
}

TEST_CASE("An id handed out again is a different assignment", "[plugins][assignments]") {
    // What clearAllTracks() does: DeviceId counters restart, so the next
    // project's device 1 is not the one a load is still in flight for.
    adapter::PluginAssignments assignments;
    assignments.assign(fx(1));
    const auto request = assignments.request(fx(1));

    assignments.releaseAll();
    CHECK(assignments.size() == 0);

    assignments.assign(fx(1));

    CHECK_FALSE(assignments.accepts(request));
}

TEST_CASE("A copy of a live device is not the device it was copied from",
          "[plugins][assignments]") {
    // Duplicate, paste, preset import and undo reinsertion all end at the same
    // shape: a second device that came from the first and is placed under an id
    // of its own. It is registered like any other placement, and there is
    // nothing it could have inherited, because assignment identity is not in
    // the value that was copied.
    adapter::PluginAssignments assignments;
    assignments.assign(fx(1));
    const auto sourceRequest = assignments.request(fx(1));

    assignments.assign(fx(2));

    CHECK_FALSE(assignments.accepts({.key = fx(2), .handle = sourceRequest.handle}));
    CHECK(assignments.accepts(sourceRequest));
}

TEST_CASE("Sections holding the same DeviceId hold different assignments",
          "[plugins][assignments]") {
    // DeviceId is allocated per section, so all three of these are device 1.
    adapter::PluginAssignments assignments;
    assignments.assign(fx(1));
    assignments.assign(postFx(1));
    assignments.assign(mixerAnalysis(1));

    const auto request = assignments.request(fx(1));

    CHECK_FALSE(assignments.accepts({.key = postFx(1), .handle = request.handle}));
    CHECK_FALSE(assignments.accepts({.key = mixerAnalysis(1), .handle = request.handle}));

    // Releasing one leaves the other two alone.
    assignments.release(fx(1));
    CHECK(assignments.size() == 2);
    CHECK(assignments.accepts(assignments.request(postFx(1))));
}

TEST_CASE("A device that was never registered has no load to accept", "[plugins][assignments]") {
    // The failure direction that matters. A forgotten registration yields an
    // expired request, so the load is refused; the token it replaced would have
    // been carried by a copy and the load accepted.
    adapter::PluginAssignments assignments;

    const auto request = assignments.request(fx(7));
    CHECK(request.handle.expired());
    CHECK_FALSE(assignments.accepts(request));
}

TEST_CASE("A handle held past its release does not revive the assignment",
          "[plugins][assignments]") {
    // The caller keeps the ActiveAssignment it was handed, so the weak
    // reference still locks. The live set is what decides.
    adapter::PluginAssignments assignments;
    const auto held = assignments.assign(fx(1));
    REQUIRE(static_cast<bool>(held));

    const auto request = assignments.request(fx(1));
    assignments.release(fx(1));

    REQUIRE_FALSE(request.handle.expired());
    CHECK_FALSE(assignments.accepts(request));
}

TEST_CASE("A default-constructed request is refused", "[plugins][assignments]") {
    adapter::PluginAssignments assignments;
    assignments.assign(fx(magda::INVALID_DEVICE_ID));

    CHECK_FALSE(assignments.accepts({}));
}
