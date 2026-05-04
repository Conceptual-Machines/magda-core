#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/ChainFingerprint.hpp"
#include "../magda/daw/core/ChainNode.hpp"
#include "../magda/daw/core/MacroInfo.hpp"
#include "../magda/daw/core/ModInfo.hpp"
#include "../magda/daw/core/RackInfo.hpp"

using namespace magda;

namespace {

// Build a ConstChainNode with given mods/macros for fingerprintOf tests.
ConstChainNode makeNode(const ModArray* mods, const MacroArray* macros) {
    ConstChainNode node;
    node.scope = ChainScope::Track;
    node.mods = mods;
    node.macros = macros;
    return node;
}

ModInfo makeMod(int id, bool enabled, std::vector<ModLink> links) {
    ModInfo mod;
    mod.id = id;
    mod.enabled = enabled;
    mod.links = std::move(links);
    return mod;
}

MacroInfo makeMacro(int id, std::vector<MacroLink> links) {
    MacroInfo macro;
    macro.id = id;
    macro.links = std::move(links);
    return macro;
}

ModLink modLink(DeviceId targetDev, int paramIdx, float amount = 0.5f, bool bipolar = false) {
    ModLink link;
    link.target.kind = ControlTarget::Kind::PluginParam;
    link.target.devicePath = ChainNodePath::topLevelDevice(1, targetDev);
    link.target.paramIndex = paramIdx;
    link.amount = amount;
    link.bipolar = bipolar;
    return link;
}

MacroLink macroLink(DeviceId targetDev, int paramIdx, float amount = 0.5f, bool bipolar = false) {
    MacroLink link;
    link.target.kind = ControlTarget::Kind::PluginParam;
    link.target.devicePath = ChainNodePath::topLevelDevice(1, targetDev);
    link.target.paramIndex = paramIdx;
    link.amount = amount;
    link.bipolar = bipolar;
    return link;
}

DeviceInfo makeDevice(DeviceId id, ModArray mods = {}, MacroArray macros = {},
                      bool bypassed = false) {
    DeviceInfo dev;
    dev.id = id;
    dev.mods = std::move(mods);
    dev.macros = std::move(macros);
    dev.bypassed = bypassed;
    return dev;
}

}  // namespace

// ============================================================================
// fingerprintOf — single-node invariants
// ============================================================================

TEST_CASE("fingerprintOf - empty / invalid node", "[modulation][fingerprint]") {
    SECTION("Default-constructed ConstChainNode is invalid -> zero fingerprint") {
        ConstChainNode node;  // mods/macros nullptr, valid() returns false
        auto fp = fingerprintOf(node);
        REQUIRE(fp == ChainFingerprint{});
    }

    SECTION("Empty mods + macros -> zero fingerprint") {
        ModArray mods;
        MacroArray macros;
        auto fp = fingerprintOf(makeNode(&mods, &macros));
        REQUIRE(fp.modCount == 0);
        REQUIRE(fp.modLinkCount == 0);
        REQUIRE(fp.macroCount == 0);
        REQUIRE(fp.macroLinkCount == 0);
        REQUIRE(fp.bipolarCount == 0);
    }
}

TEST_CASE("fingerprintOf - mod gating rules", "[modulation][fingerprint]") {
    MacroArray emptyMacros;

    SECTION("Disabled mod with link does NOT count") {
        ModArray mods;
        mods.push_back(makeMod(0, /*enabled=*/false, {modLink(DeviceId(1), 0)}));
        auto fp = fingerprintOf(makeNode(&mods, &emptyMacros));
        REQUIRE(fp.modCount == 0);
        REQUIRE(fp.modLinkCount == 0);
    }

    SECTION("Enabled mod WITHOUT links does NOT count") {
        ModArray mods;
        mods.push_back(makeMod(0, /*enabled=*/true, /*links=*/{}));
        auto fp = fingerprintOf(makeNode(&mods, &emptyMacros));
        REQUIRE(fp.modCount == 0);
        REQUIRE(fp.modLinkCount == 0);
    }

    SECTION("Enabled mod WITH links counts") {
        ModArray mods;
        mods.push_back(
            makeMod(0, /*enabled=*/true, {modLink(DeviceId(1), 0), modLink(DeviceId(1), 1)}));
        auto fp = fingerprintOf(makeNode(&mods, &emptyMacros));
        REQUIRE(fp.modCount == 1);
        REQUIRE(fp.modLinkCount == 2);
    }

    SECTION("Bipolar links bump bipolarCount once per link") {
        ModArray mods;
        mods.push_back(makeMod(0, true,
                               {modLink(DeviceId(1), 0, 0.5f, /*bipolar=*/true),
                                modLink(DeviceId(1), 1, 0.5f, /*bipolar=*/false),
                                modLink(DeviceId(1), 2, 0.5f, /*bipolar=*/true)}));
        auto fp = fingerprintOf(makeNode(&mods, &emptyMacros));
        REQUIRE(fp.modLinkCount == 3);
        REQUIRE(fp.bipolarCount == 2);
    }
}

TEST_CASE("fingerprintOf - macro gating rules", "[modulation][fingerprint]") {
    ModArray emptyMods;

    SECTION("Macro with no links does NOT count") {
        MacroArray macros;
        macros.push_back(makeMacro(0, /*links=*/{}));
        auto fp = fingerprintOf(makeNode(&emptyMods, &macros));
        REQUIRE(fp.macroCount == 0);
        REQUIRE(fp.macroLinkCount == 0);
    }

    SECTION("Macro with links counts; bipolar bumps bipolarCount") {
        MacroArray macros;
        macros.push_back(makeMacro(0, {macroLink(DeviceId(1), 0, 0.5f, /*bipolar=*/true),
                                       macroLink(DeviceId(1), 1, 0.5f, /*bipolar=*/false)}));
        auto fp = fingerprintOf(makeNode(&emptyMods, &macros));
        REQUIRE(fp.macroCount == 1);
        REQUIRE(fp.macroLinkCount == 2);
        REQUIRE(fp.bipolarCount == 1);
    }
}

TEST_CASE("ChainFingerprint - operator+= sums", "[modulation][fingerprint]") {
    ChainFingerprint a;
    a.modCount = 1;
    a.modLinkCount = 2;
    a.macroCount = 3;
    a.macroLinkCount = 4;
    a.bipolarCount = 5;

    ChainFingerprint b;
    b.modCount = 10;
    b.modLinkCount = 20;
    b.macroCount = 30;
    b.macroLinkCount = 40;
    b.bipolarCount = 50;

    a += b;
    REQUIRE(a.modCount == 11);
    REQUIRE(a.modLinkCount == 22);
    REQUIRE(a.macroCount == 33);
    REQUIRE(a.macroLinkCount == 44);
    REQUIRE(a.bipolarCount == 55);
}

// ============================================================================
// computeRackFingerprint — sums rack-scope + each enabled inner device
// ============================================================================

namespace {

// Build a RackInfo with one chain containing the given devices and the given
// rack-scope mods. Tests live or die on getting this shape right; helper
// keeps the test bodies focused on the semantics.
RackInfo makeRack(RackId id, ModArray rackMods, std::vector<DeviceInfo> chainDevices) {
    RackInfo rack;
    rack.id = id;
    rack.mods = std::move(rackMods);

    ChainInfo chain;
    chain.id = ChainId(1);
    for (auto& device : chainDevices)
        chain.elements.push_back(std::move(device));
    rack.chains.push_back(std::move(chain));
    return rack;
}

}  // namespace

TEST_CASE("computeRackFingerprint - empty rack -> zero", "[modulation][fingerprint][rack]") {
    auto rack = makeRack(RackId(1), {}, {});
    auto fp = computeRackFingerprint(rack);
    REQUIRE(fp == ChainFingerprint{});
}

TEST_CASE("computeRackFingerprint - rack-scope mod only counts",
          "[modulation][fingerprint][rack]") {
    ModArray rackMods;
    rackMods.push_back(makeMod(0, true, {modLink(DeviceId(42), 0)}));
    auto rack = makeRack(RackId(1), std::move(rackMods), {});
    auto fp = computeRackFingerprint(rack);
    REQUIRE(fp.modCount == 1);
    REQUIRE(fp.modLinkCount == 1);
}

TEST_CASE("computeRackFingerprint - inner-device mod counts", "[modulation][fingerprint][rack]") {
    // Regression coverage: this is the case that was silently dropped before
    // the inner-device-mods fix. Fingerprint must sum inner-device mods so
    // resyncAllModifiers fires the structural rebuild path when one is
    // added/removed.
    ModArray innerMods;
    innerMods.push_back(makeMod(0, true, {modLink(DeviceId(42), 0)}));
    auto rack = makeRack(RackId(1), {}, {makeDevice(DeviceId(42), std::move(innerMods))});
    auto fp = computeRackFingerprint(rack);
    REQUIRE(fp.modCount == 1);
    REQUIRE(fp.modLinkCount == 1);
}

TEST_CASE("computeRackFingerprint - rack mod + inner device mod sum",
          "[modulation][fingerprint][rack]") {
    ModArray rackMods;
    rackMods.push_back(makeMod(0, true, {modLink(DeviceId(42), 0)}));

    ModArray innerMods;
    innerMods.push_back(makeMod(0, true, {modLink(DeviceId(42), 1), modLink(DeviceId(42), 2)}));

    auto rack =
        makeRack(RackId(1), std::move(rackMods), {makeDevice(DeviceId(42), std::move(innerMods))});
    auto fp = computeRackFingerprint(rack);
    REQUIRE(fp.modCount == 2);      // rack mod + inner device mod
    REQUIRE(fp.modLinkCount == 3);  // 1 (rack) + 2 (inner)
}

TEST_CASE("computeRackFingerprint - bypassed devices' mods do NOT count",
          "[modulation][fingerprint][rack]") {
    // Bypassed devices skip the modifier graph entirely (per syncStructure
    // which passes nullptr to the walker). Fingerprint must agree, otherwise
    // toggling bypass would falsely trigger structural rebuilds.
    ModArray innerMods;
    innerMods.push_back(makeMod(0, true, {modLink(DeviceId(42), 0)}));
    auto rack = makeRack(
        RackId(1), {},
        {makeDevice(DeviceId(42), std::move(innerMods), /*macros=*/{}, /*bypassed=*/true)});
    auto fp = computeRackFingerprint(rack);
    REQUIRE(fp == ChainFingerprint{});
}

TEST_CASE("computeRackFingerprint - non-device chain elements (nested racks) are skipped",
          "[modulation][fingerprint][rack]") {
    // A nested rack inside a rack is not a DeviceInfo; computeRackFingerprint
    // walks chain.elements via isDevice() so it must skip non-device entries
    // gracefully. (Recursive nested-rack handling is out of scope here — that
    // would be a follow-up; the test exists to pin the no-crash invariant.)
    RackInfo outer;
    outer.id = RackId(1);
    ChainInfo chain;
    chain.id = ChainId(1);
    auto nested = std::make_unique<RackInfo>();
    nested->id = RackId(2);
    chain.elements.push_back(std::move(nested));
    outer.chains.push_back(std::move(chain));

    auto fp = computeRackFingerprint(outer);
    REQUIRE(fp == ChainFingerprint{});
}
