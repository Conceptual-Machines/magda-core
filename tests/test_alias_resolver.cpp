#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/aliases/AliasRegistry.hpp"
#include "../magda/daw/core/aliases/ChainContext.hpp"
#include "../magda/daw/core/aliases/ResolverRegistry.hpp"
#include "../magda/daw/core/aliases/Target.hpp"
#include "../magda/daw/core/aliases/TargetResolver.hpp"

using namespace magda;

// ============================================================================
// Test fixture helpers
// ============================================================================

/**
 * Build a DeviceInfo with given name and parameters.
 * paramNames: list of parameter names to add (index == position in list).
 */
static DeviceInfo makeDevice(DeviceId id, const juce::String& name,
                             const std::vector<juce::String>& paramNames) {
    DeviceInfo dev;
    dev.id = id;
    dev.name = name;
    for (int i = 0; i < static_cast<int>(paramNames.size()); ++i) {
        ParameterInfo p;
        p.paramIndex = i;
        p.name = paramNames[static_cast<size_t>(i)];
        dev.parameters.push_back(p);
    }
    return dev;
}

static ChainNodePath makePath(int trackId, int deviceId) {
    return ChainNodePath::topLevelDevice(trackId, deviceId);
}

// ============================================================================
// TargetResolver::resolve(ControlTarget)
// ============================================================================

TEST_CASE("TargetResolver - resolve ControlTarget ok", "[aliases][resolver]") {
    FixedChainContext ctx;
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    ControlTarget st;
    st.devicePath = makePath(1, 10);
    st.paramIndex = 3;

    auto result = resolver.resolve(Target{st});
    REQUIRE(result.ok());
    REQUIRE(result.target.paramIndex == 3);
    REQUIRE(result.target.devicePath == st.devicePath);
}

TEST_CASE("TargetResolver - resolve invalid ControlTarget fails", "[aliases][resolver]") {
    FixedChainContext ctx;
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    ControlTarget st;  // invalid (no path, paramIndex -1)
    auto result = resolver.resolve(Target{st});
    REQUIRE_FALSE(result.ok());
}

// ============================================================================
// TargetResolver::resolve(ResolverRef)
// ============================================================================

TEST_CASE("TargetResolver - resolve ResolverRef master.volume", "[aliases][resolver]") {
    FixedChainContext ctx;
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    ResolverRef rr;
    rr.kind = "master.volume";

    auto result = resolver.resolve(Target{rr});
    REQUIRE(result.ok());
    REQUIRE(result.target.paramIndex == 0);
    REQUIRE(result.target.devicePath.isTrackLevel);
    REQUIRE(result.target.devicePath.trackId == MASTER_TRACK_ID);
}

TEST_CASE("TargetResolver - resolve ResolverRef master.pan", "[aliases][resolver]") {
    FixedChainContext ctx;
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    ResolverRef rr;
    rr.kind = "master.pan";

    auto result = resolver.resolve(Target{rr});
    REQUIRE(result.ok());
    REQUIRE(result.target.paramIndex == 1);
    REQUIRE(result.target.devicePath.isTrackLevel);
}

TEST_CASE("TargetResolver - resolve unknown ResolverRef kind fails", "[aliases][resolver]") {
    FixedChainContext ctx;
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    ResolverRef rr;
    rr.kind = "nonexistent_resolver";

    auto result = resolver.resolve(Target{rr});
    REQUIRE_FALSE(result.ok());
}

// ============================================================================
// @ sigil - selected-chain device preference
// ============================================================================

TEST_CASE("@ resolution prefers selected-chain devices over registry", "[aliases][resolver]") {
    // A device named "Serum" lives on the selected track (track 1).
    // The AliasRegistry also has a type-level alias for "serum.cutoff" pointing
    // to a different path. The resolver must return the concrete chain device,
    // NOT the registry alias.
    DeviceInfo serum = makeDevice(10, "Serum", {"Filter Cutoff", "Resonance", "Env Attack"});
    auto serumPath = makePath(1, 10);

    FixedChainContext ctx;
    ctx.setSelectedTrack(1);
    ctx.addDevice(serumPath, serum);

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);

    // Register a type-level alias pointing to a *different* device path
    StoredAlias alias;
    alias.pluginTypeKey = "serum";
    alias.paramIndex = 0;
    alias.path = makePath(99, 99);  // different path
    reg.set(AliasLayer::UserGlobal, "serum.cutoff", alias);

    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    auto parsed = tryParse("@serum.filter_cutoff");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE(result.ok());
    // Must resolve to the concrete chain device, not the registry alias path
    REQUIRE(result.target.devicePath == serumPath);
    REQUIRE(result.target.paramIndex == 0);  // "Filter Cutoff" is index 0
}

TEST_CASE("@ resolution falls back to registry when no track selected", "[aliases][resolver]") {
    // No track selected — resolution must fall through to AliasRegistry.
    DeviceInfo serum = makeDevice(10, "Serum", {"Filter Cutoff"});

    FixedChainContext ctx;
    // No setSelectedTrack — selectedTrack() returns INVALID_TRACK_ID
    ctx.addDevice(makePath(1, 10), serum);

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);

    StoredAlias alias;
    alias.pluginTypeKey = "serum";
    alias.paramIndex = 5;
    alias.path = makePath(2, 20);  // some concrete registry path
    reg.set(AliasLayer::UserGlobal, "serum.cutoff", alias);

    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    auto parsed = tryParse("@serum.cutoff");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE(result.ok());
    REQUIRE(result.target.devicePath == makePath(2, 20));
    REQUIRE(result.target.paramIndex == 5);
}

// ============================================================================
// @ sigil (scoped)
// ============================================================================

TEST_CASE("TargetResolver::resolveSigil - @master.volume", "[aliases][resolver]") {
    FixedChainContext ctx;
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    auto parsed = tryParse("@master.volume");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->isScoped);

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE(result.ok());
    REQUIRE(result.target.devicePath.trackId == MASTER_TRACK_ID);
    REQUIRE(result.target.paramIndex == 0);
}

TEST_CASE("TargetResolver::resolveSigil - @selected.volume with selected track",
          "[aliases][resolver]") {
    FixedChainContext ctx;
    ctx.setSelectedTrack(5);

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    auto parsed = tryParse("@selected.volume");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE(result.ok());
    REQUIRE(result.target.devicePath.trackId == 5);
    REQUIRE(result.target.paramIndex == 0);
}

TEST_CASE("TargetResolver::resolveSigil - @selected.volume no track selected fails",
          "[aliases][resolver]") {
    FixedChainContext ctx;  // no track selected

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    auto parsed = tryParse("@selected.volume");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("TargetResolver::resolveSigil - @focused.filter_cutoff", "[aliases][resolver]") {
    DeviceInfo serum = makeDevice(10, "Serum", {"Filter Cutoff", "Resonance"});
    auto path = makePath(1, 10);

    FixedChainContext ctx;
    ctx.setFocusedDevice(path);
    ctx.addDevice(path, serum);

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    auto parsed = tryParse("@focused.filter_cutoff");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE(result.ok());
    REQUIRE(result.target.devicePath == path);
    REQUIRE(result.target.paramIndex == 0);
}

TEST_CASE("TargetResolver::resolveSigil - @focused no device focused fails",
          "[aliases][resolver]") {
    FixedChainContext ctx;  // no focused device

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    auto parsed = tryParse("@focused.filter_cutoff");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE_FALSE(result.ok());
}

// ============================================================================
// ResolverRegistry
// ============================================================================

TEST_CASE("ResolverRegistry - built-ins are registered", "[aliases][resolver]") {
    auto& reg = ResolverRegistry::getInstance();
    REQUIRE(reg.findResolver("focused.macro") != nullptr);
    REQUIRE(reg.findResolver("selected.volume") != nullptr);
    REQUIRE(reg.findResolver("selected.pan") != nullptr);
    REQUIRE(reg.findResolver("master.volume") != nullptr);
    REQUIRE(reg.findResolver("master.pan") != nullptr);
    REQUIRE(reg.findResolver("nonexistent") == nullptr);
}

TEST_CASE("ResolverRegistry - custom resolver can be registered", "[aliases][resolver]") {
    struct DummyResolver : AliasResolver {
        juce::String kind() const override {
            return "dummy.test_resolver";
        }
        std::optional<ControlTarget> resolve(const juce::StringPairArray&,
                                             const ChainContext&) const override {
            return std::nullopt;
        }
    };

    auto& reg = ResolverRegistry::getInstance();
    reg.registerResolver(std::make_unique<DummyResolver>());
    REQUIRE(reg.findResolver("dummy.test_resolver") != nullptr);
}

// ============================================================================
// Owner / DeviceMacro tests (automap fix)
// ============================================================================

TEST_CASE("FocusedDeviceMacroResolver returns DeviceMacro owner", "[aliases][resolver][owner]") {
    auto path = makePath(1, 42);
    FixedChainContext ctx;
    ctx.setFocusedDevice(path);

    auto& resolvers = ResolverRegistry::getInstance();
    const auto* resolver = resolvers.findResolver("focused.macro");
    REQUIRE(resolver != nullptr);

    juce::StringPairArray args;
    args.set("macroIndex", "3");
    auto result = resolver->resolve(args, ctx);

    REQUIRE(result.has_value());
    REQUIRE(result->kind == ControlTarget::Kind::DeviceMacro);
    REQUIRE(result->paramIndex == 3);
    REQUIRE(result->devicePath == path);
}

TEST_CASE("FocusedDeviceMacroResolver targets focused rack's macros",
          "[aliases][resolver][rack-automap]") {
    // When the user focuses a user-created rack, automap should bind
    // controller knobs to the rack's macros.
    auto rackPath = ChainNodePath::rack(1, /*rackId=*/7);
    FixedChainContext ctx;
    ctx.setFocusedMacroOwner(rackPath);

    const auto* resolver = ResolverRegistry::getInstance().findResolver("focused.macro");
    REQUIRE(resolver != nullptr);

    juce::StringPairArray args;
    args.set("macroIndex", "2");
    auto result = resolver->resolve(args, ctx);

    REQUIRE(result.has_value());
    REQUIRE(result->kind == ControlTarget::Kind::DeviceMacro);
    REQUIRE(result->paramIndex == 2);
    REQUIRE(result->devicePath == rackPath);
    REQUIRE(result->devicePath.getType() == ChainNodeType::Rack);
}

TEST_CASE("FocusedDeviceMacroResolver consumes focusedMacroOwner, not focusedDevice",
          "[aliases][resolver][rack-automap]") {
    // The resolver must read focusedMacroOwner() exclusively. Set the
    // two getters to different paths and verify the resolver follows
    // the macro-owner one. This documents the contract — the live
    // path-walk logic (rack/chain/device → owner) lives in
    // DefaultChainContext::focusedMacroOwner() and is not exercised here.
    auto rackPath = ChainNodePath::rack(1, /*rackId=*/7);
    auto unrelatedDevicePath = makePath(1, /*deviceId=*/55);
    FixedChainContext ctx;
    ctx.setFocusedMacroOwner(rackPath);
    ctx.setFocusedDevice(unrelatedDevicePath);

    const auto* resolver = ResolverRegistry::getInstance().findResolver("focused.macro");
    auto result = resolver->resolve({}, ctx);

    REQUIRE(result.has_value());
    REQUIRE(result->devicePath == rackPath);
    REQUIRE(result->devicePath != unrelatedDevicePath);
}

TEST_CASE("FocusedDeviceMacroResolver returns nullopt when no macro owner is focused",
          "[aliases][resolver][rack-automap]") {
    // Inner-device focus / track focus / nothing focused all collapse to
    // an invalid focusedMacroOwner; the resolver must produce no target
    // (so no automap binding fires).
    FixedChainContext ctx;  // default: invalid macro owner

    const auto* resolver = ResolverRegistry::getInstance().findResolver("focused.macro");
    auto result = resolver->resolve({}, ctx);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ControlTarget JSON round-trip preserves DeviceMacro owner",
          "[aliases][resolver][owner]") {
    ControlTarget st;
    st.devicePath = makePath(2, 99);
    st.paramIndex = 2;
    st.kind = ControlTarget::Kind::DeviceMacro;

    auto encoded = encodeTarget(Target{st});
    auto decoded = decodeTarget(encoded);

    REQUIRE(decoded.has_value());
    auto* dst = std::get_if<ControlTarget>(&*decoded);
    REQUIRE(dst != nullptr);
    REQUIRE(dst->kind == ControlTarget::Kind::DeviceMacro);
    REQUIRE(dst->paramIndex == 2);
    REQUIRE(dst->devicePath == st.devicePath);
}

TEST_CASE("ControlTarget JSON decode defaults to PluginParam when owner field absent",
          "[aliases][resolver][owner]") {
    // Hand-crafted JSON without an "owner" key -- simulates old bindings
    juce::String json = "{\"kind\":\"static\",\"paramIndex\":5,"
                        "\"path\":{\"trackId\":1,\"topLevelDeviceId\":10,\"isTrackLevel\":false,"
                        "\"steps\":[]}}";

    auto decoded = decodeTarget(json);
    REQUIRE(decoded.has_value());
    auto* dst = std::get_if<ControlTarget>(&*decoded);
    REQUIRE(dst != nullptr);
    REQUIRE(dst->kind == ControlTarget::Kind::PluginParam);
    REQUIRE(dst->paramIndex == 5);
}

TEST_CASE("TargetResolver propagates DeviceMacro owner from ControlTarget",
          "[aliases][resolver][owner]") {
    FixedChainContext ctx;
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    ControlTarget st;
    st.devicePath = makePath(1, 10);
    st.paramIndex = 1;
    st.kind = ControlTarget::Kind::DeviceMacro;

    auto result = resolver.resolve(Target{st});
    REQUIRE(result.ok());
    REQUIRE(result.target.kind == ControlTarget::Kind::DeviceMacro);
    REQUIRE(result.target.paramIndex == 1);
}

TEST_CASE("TargetResolver propagates DeviceMacro owner from ResolverRef via FocusedDeviceMacro",
          "[aliases][resolver][owner]") {
    auto path = makePath(3, 77);
    FixedChainContext ctx;
    ctx.setFocusedDevice(path);

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    ResolverRef rr;
    rr.kind = "focused.macro";
    rr.args.set("macroIndex", "0");

    auto result = resolver.resolve(Target{rr});
    REQUIRE(result.ok());
    REQUIRE(result.target.kind == ControlTarget::Kind::DeviceMacro);
    REQUIRE(result.target.paramIndex == 0);
    REQUIRE(result.target.devicePath == path);
}
