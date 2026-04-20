#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/aliases/AliasRegistry.hpp"
#include "../magda/daw/core/aliases/ChainContext.hpp"
#include "../magda/daw/core/aliases/LocalBindings.hpp"
#include "../magda/daw/core/aliases/ResolverRegistry.hpp"
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
// # sigil
// ============================================================================

TEST_CASE("TargetResolver::resolveSigil - # first device match", "[aliases][resolver]") {
    // Build context with two devices: Serum and Surge XT
    DeviceInfo serum = makeDevice(10, "Serum", {"Filter Cutoff", "Resonance", "Env Attack"});
    DeviceInfo surge = makeDevice(11, "Surge XT", {"OSC1 Pitch", "Filter Cutoff"});

    FixedChainContext ctx;
    ctx.addDevice(makePath(1, 10), serum);
    ctx.addDevice(makePath(1, 11), surge);

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    auto parsed = tryParse("#serum.filter_cutoff");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE(result.ok());
    REQUIRE(result.devicePath == makePath(1, 10));
    REQUIRE(result.paramIndex == 0);  // "Filter Cutoff" is index 0
}

TEST_CASE("TargetResolver::resolveSigil - # instance index selects Nth device",
          "[aliases][resolver]") {
    DeviceInfo serum1 = makeDevice(10, "Serum", {"Filter Cutoff"});
    DeviceInfo serum2 = makeDevice(11, "Serum", {"Filter Cutoff"});

    FixedChainContext ctx;
    ctx.addDevice(makePath(1, 10), serum1);
    ctx.addDevice(makePath(1, 11), serum2);

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    // #serum_2.filter_cutoff -> second Serum (index 1)
    auto parsed = tryParse("#serum_2.filter_cutoff");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE(result.ok());
    REQUIRE(result.devicePath == makePath(1, 11));
}

TEST_CASE("TargetResolver::resolveSigil - # no chain focused returns failure",
          "[aliases][resolver]") {
    FixedChainContext ctx;  // empty context

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    auto parsed = tryParse("#serum.cutoff");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.sourceLabel.isNotEmpty());
}

TEST_CASE("TargetResolver::resolveSigil - # param not found returns failure",
          "[aliases][resolver]") {
    DeviceInfo serum = makeDevice(10, "Serum", {"Filter Cutoff", "Resonance"});

    FixedChainContext ctx;
    ctx.addDevice(makePath(1, 10), serum);

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    auto parsed = tryParse("#serum.nonexistent_param");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("TargetResolver::resolveSigil - # no matching device returns failure",
          "[aliases][resolver]") {
    DeviceInfo surge = makeDevice(11, "Surge XT", {"OSC1 Pitch"});

    FixedChainContext ctx;
    ctx.addDevice(makePath(1, 11), surge);

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx};

    auto parsed = tryParse("#serum.cutoff");  // Serum not in chain
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE_FALSE(result.ok());
}

// ============================================================================
// $ sigil
// ============================================================================

TEST_CASE("TargetResolver::resolveSigil - $ finds bound device param", "[aliases][resolver]") {
    DeviceInfo synth = makeDevice(20, "MySynth", {"Cutoff", "Resonance", "Volume"});
    auto path = makePath(1, 20);

    FixedChainContext ctx;
    ctx.addDevice(path, synth);

    LocalBindings bindings;
    bindings.bindDevice("mysynth", path);

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx, &bindings};

    auto parsed = tryParse("$mysynth.volume");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE(result.ok());
    REQUIRE(result.devicePath == path);
    REQUIRE(result.paramIndex == 2);  // "Volume" is at index 2
}

TEST_CASE("TargetResolver::resolveSigil - $ unbound name fails", "[aliases][resolver]") {
    FixedChainContext ctx;
    LocalBindings bindings;  // nothing bound

    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx, &bindings};

    auto parsed = tryParse("$mysynth.cutoff");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE_FALSE(result.ok());
}

TEST_CASE("TargetResolver::resolveSigil - $ null LocalBindings fails", "[aliases][resolver]") {
    FixedChainContext ctx;
    auto& reg = AliasRegistry::getInstance();
    reg.clearLayer(AliasLayer::UserProject);
    reg.clearLayer(AliasLayer::UserGlobal);
    reg.clearLayer(AliasLayer::Curated);
    reg.clearLayer(AliasLayer::AutoGen);
    auto& resolvers = ResolverRegistry::getInstance();
    TargetResolver resolver{reg, resolvers, ctx, nullptr};

    auto parsed = tryParse("$mysynth.cutoff");
    REQUIRE(parsed.has_value());

    auto result = resolver.resolveSigil(*parsed);
    REQUIRE_FALSE(result.ok());
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
    REQUIRE(reg.findResolver("focused_device_macro") != nullptr);
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
