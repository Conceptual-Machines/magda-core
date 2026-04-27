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
// TargetResolver::resolve(StaticTarget)
// ============================================================================

TEST_CASE("TargetResolver - resolve StaticTarget ok", "[aliases][resolver]") {
    FixedChainContext ctx;
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    StaticTarget st;
    st.devicePath = makePath(1, 10);
    st.paramIndex = 3;

    auto result = resolver.resolve(Target{st});
    REQUIRE(result.ok());
    REQUIRE(result.paramIndex == 3);
    REQUIRE(result.devicePath == st.devicePath);
}

TEST_CASE("TargetResolver - resolve invalid StaticTarget fails", "[aliases][resolver]") {
    FixedChainContext ctx;
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    StaticTarget st;  // invalid (no path, paramIndex -1)
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
    REQUIRE(result.paramIndex == 0);
    REQUIRE(result.devicePath.isTrackLevel);
    REQUIRE(result.devicePath.trackId == MASTER_TRACK_ID);
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
    REQUIRE(result.paramIndex == 1);
    REQUIRE(result.devicePath.isTrackLevel);
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
    REQUIRE(result.devicePath == serumPath);
    REQUIRE(result.paramIndex == 0);  // "Filter Cutoff" is index 0
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
    REQUIRE(result.devicePath == makePath(2, 20));
    REQUIRE(result.paramIndex == 5);
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
    REQUIRE(result.devicePath.trackId == MASTER_TRACK_ID);
    REQUIRE(result.paramIndex == 0);
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
    REQUIRE(result.devicePath.trackId == 5);
    REQUIRE(result.paramIndex == 0);
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
    REQUIRE(result.devicePath == path);
    REQUIRE(result.paramIndex == 0);
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
        std::optional<StaticTarget> resolve(const juce::StringPairArray&,
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
    REQUIRE(result->owner == StaticTarget::Owner::DeviceMacro);
    REQUIRE(result->paramIndex == 3);
    REQUIRE(result->devicePath == path);
}

TEST_CASE("StaticTarget JSON round-trip preserves DeviceMacro owner",
          "[aliases][resolver][owner]") {
    StaticTarget st;
    st.devicePath = makePath(2, 99);
    st.paramIndex = 2;
    st.owner = StaticTarget::Owner::DeviceMacro;

    auto encoded = encodeTarget(Target{st});
    auto decoded = decodeTarget(encoded);

    REQUIRE(decoded.has_value());
    auto* dst = std::get_if<StaticTarget>(&*decoded);
    REQUIRE(dst != nullptr);
    REQUIRE(dst->owner == StaticTarget::Owner::DeviceMacro);
    REQUIRE(dst->paramIndex == 2);
    REQUIRE(dst->devicePath == st.devicePath);
}

TEST_CASE("StaticTarget JSON decode defaults to PluginParam when owner field absent",
          "[aliases][resolver][owner]") {
    // Hand-crafted JSON without an "owner" key -- simulates old bindings
    juce::String json = "{\"kind\":\"static\",\"paramIndex\":5,"
                        "\"path\":{\"trackId\":1,\"topLevelDeviceId\":10,\"isTrackLevel\":false,"
                        "\"steps\":[]}}";

    auto decoded = decodeTarget(json);
    REQUIRE(decoded.has_value());
    auto* dst = std::get_if<StaticTarget>(&*decoded);
    REQUIRE(dst != nullptr);
    REQUIRE(dst->owner == StaticTarget::Owner::PluginParam);
    REQUIRE(dst->paramIndex == 5);
}

TEST_CASE("TargetResolver propagates DeviceMacro owner from StaticTarget",
          "[aliases][resolver][owner]") {
    FixedChainContext ctx;
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    StaticTarget st;
    st.devicePath = makePath(1, 10);
    st.paramIndex = 1;
    st.owner = StaticTarget::Owner::DeviceMacro;

    auto result = resolver.resolve(Target{st});
    REQUIRE(result.ok());
    REQUIRE(result.owner == StaticTarget::Owner::DeviceMacro);
    REQUIRE(result.paramIndex == 1);
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
    REQUIRE(result.owner == StaticTarget::Owner::DeviceMacro);
    REQUIRE(result.paramIndex == 0);
    REQUIRE(result.devicePath == path);
}
