#include <catch2/catch_test_macros.hpp>

#include "magda/agents/automation_executor.hpp"
#include "magda/agents/automation_parser.hpp"
#include "magda/daw/api/magda_api_live.hpp"
#include "magda/daw/core/AutomationManager.hpp"
#include "magda/daw/core/ClipManager.hpp"
#include "magda/daw/core/SelectionManager.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/core/UndoManager.hpp"
#include "magda/daw/core/aliases/AliasRegistry.hpp"

using namespace magda;

// ============================================================================
// Helpers
// ============================================================================

namespace {

void resetState() {
    AutomationManager::getInstance().clearAll();
    ClipManager::getInstance().clearAllClips();
    TrackManager::getInstance().clearAllTracks();
    UndoManager::getInstance().clearHistory();
    SelectionManager::getInstance().clearSelection();
    AliasRegistry::getInstance().clearLayer(AliasLayer::UserProject);
    AliasRegistry::getInstance().clearLayer(AliasLayer::UserGlobal);
    AliasRegistry::getInstance().clearLayer(AliasLayer::Curated);
    AliasRegistry::getInstance().clearLayer(AliasLayer::AutoGen);
}

std::vector<AutoInstruction> parseOrFail(AutomationParser& parser, const juce::String& text) {
    auto out = parser.parse(text);
    INFO("Parser error: " << parser.getLastError().toStdString());
    REQUIRE(parser.getLastError().isEmpty());
    return out;
}

}  // namespace

// ============================================================================
// Parser: alias sigil tokens are parsed as Kind::Alias
// ============================================================================

TEST_CASE("AutomationParser: @plugin.param sets Kind::Alias with aliasToken",
          "[automation][alias]") {
    AutomationParser p;
    auto ir = parseOrFail(p, "AUTO sin start=0 end=4 min=0 max=1 target=@eq.low_shelf_freq");

    REQUIRE(ir.size() == 1);
    const auto& op = std::get<AutoShapeOp>(ir[0].payload);
    REQUIRE(op.target.kind == AutoTarget::Kind::Alias);
    REQUIRE(op.target.aliasToken == "@eq.low_shelf_freq");
}

TEST_CASE("AutomationParser: freeform with alias target", "[automation][alias]") {
    AutomationParser p;
    auto ir = parseOrFail(p, "AUTO freeform points=(0,0.1)(4,0.9) target=@compressor.threshold");

    REQUIRE(ir.size() == 1);
    REQUIRE(std::holds_alternative<AutoFreeformOp>(ir[0].payload));
    const auto& op = std::get<AutoFreeformOp>(ir[0].payload);
    REQUIRE(op.target.kind == AutoTarget::Kind::Alias);
    REQUIRE(op.target.aliasToken == "@compressor.threshold");
}

TEST_CASE("AutomationParser: clear with alias target", "[automation][alias]") {
    AutomationParser p;
    auto ir = parseOrFail(p, "AUTO clear target=@eq.low_shelf_freq");

    REQUIRE(ir.size() == 1);
    REQUIRE(std::holds_alternative<AutoClearOp>(ir[0].payload));
    const auto& op = std::get<AutoClearOp>(ir[0].payload);
    REQUIRE(op.target.kind == AutoTarget::Kind::Alias);
    REQUIRE(op.target.aliasToken == "@eq.low_shelf_freq");
}

// ============================================================================
// Parser regression: pre-existing target forms still work
// ============================================================================

TEST_CASE("AutomationParser: existing targets still parse correctly",
          "[automation][alias][regression]") {
    AutomationParser p;

    {
        auto ir = parseOrFail(p, "AUTO sin start=0 end=4 min=0 max=1 target=selected");
        REQUIRE(std::get<AutoShapeOp>(ir[0].payload).target.kind == AutoTarget::Kind::Selected);
    }
    {
        auto ir = parseOrFail(p, "AUTO sin start=0 end=4 min=0 max=1 target=volume");
        REQUIRE(std::get<AutoShapeOp>(ir[0].payload).target.kind == AutoTarget::Kind::TrackVolume);
    }
    {
        auto ir = parseOrFail(p, "AUTO sin start=0 end=4 min=0 max=1 target=pan");
        REQUIRE(std::get<AutoShapeOp>(ir[0].payload).target.kind == AutoTarget::Kind::TrackPan);
    }
}

// ============================================================================
// Executor: alias that cannot be resolved produces an error, not a crash
// ============================================================================

TEST_CASE("AutomationExecutor: unresolvable @alias target fails gracefully",
          "[automation][alias]") {
    resetState();

    AutomationParser parser;
    MagdaApiLive api;
    AutomationExecutor exec(api);

    // No registry entry for @ghost.param, no chain context — resolution fails.
    auto ir = parseOrFail(parser, "AUTO sin start=0 end=4 min=0 max=1 target=@ghost.param");
    REQUIRE_FALSE(exec.execute(ir));
    REQUIRE(exec.getError().isNotEmpty());
}
