#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/ParameterInfo.hpp"
#include "param/ParamBlock.hpp"
#include "param/ParamResolve.hpp"
#include "param/ParamSpec.hpp"

/**
 * @file test_param_lane.cpp
 * @brief The parameter lane and its precedence (#2116).
 *
 * The lanes are fed by hand here, which is the point: the sources that will
 * write them arrive in later slices (automation in #2118, the modifiers in
 * #2119 and #2120), and the rules about how they combine are settled now, where
 * a case can put a curve and a modifier and a knob move against each other and
 * say what the device must hear.
 */

using magda::engine::DeviceParams;
using magda::engine::ModContribution;
using magda::engine::ParamSegment;
using magda::engine::ParamSources;
using magda::engine::ParamSpec;
using magda::engine::paramSpecFrom;
using magda::engine::ParamValues;
using magda::engine::ResolvedParams;
using magda::engine::resolveParam;

namespace {

Catch::Approx approx(float value) {
    return Catch::Approx(value).margin(1e-5);
}

/// A parameter reading 0 to 100 of something, so a normalised position and the
/// value a device reads cannot be confused for each other.
ParamSpec percentSpec(bool segmentAccurate = false) {
    ParamSpec spec;
    spec.domain.scale = magda::ParameterScale::Linear;
    spec.domain.minValue = 0.0f;
    spec.domain.maxValue = 100.0f;
    spec.segmentAccurate = segmentAccurate;
    return spec;
}

/// One block's worth of table, holding one parameter.
struct OneParam {
    explicit OneParam(int numSamples, int capacity = ResolvedParams::kDefaultSegmentCapacity) {
        params.prepare(1, capacity);
        params.beginBlock(numSamples);
    }

    ParamValues resolve(const ParamSpec& spec, const ParamSources& sources) {
        resolveParam(params, 0, spec, sources);
        return params[0];
    }

    ResolvedParams params;
};

}  // namespace

TEST_CASE("A parameter with nothing but a base reads its base", "[engine][param]") {
    OneParam table{512};

    ParamSources sources;
    sources.base = 0.25f;

    const auto values = table.resolve(percentSpec(), sources);

    REQUIRE(values.numSegments() == 1);
    CHECK(values.isConstant());
    CHECK(values.value() == approx(25.0f));
    CHECK(values.valueAt(511) == approx(25.0f));
}

TEST_CASE("Automation replaces the base where it covers the block", "[engine][param]") {
    OneParam table{100};

    const std::vector<ParamSegment> lane{{40, 0.9f, 0.9f}};

    ParamSources sources;
    sources.base = 0.1f;
    sources.automation = lane;
    sources.automationEnd = 80;

    const auto values = table.resolve(percentSpec(/*segmentAccurate=*/true), sources);

    // Base, lane, base: an automation clip starting and stopping inside a block
    // leaves the parameter to its stored value on either side of itself.
    REQUIRE(values.numSegments() == 3);
    CHECK(values.valueAt(0) == approx(10.0f));
    CHECK(values.valueAt(39) == approx(10.0f));
    CHECK(values.valueAt(40) == approx(90.0f));
    CHECK(values.valueAt(79) == approx(90.0f));
    CHECK(values.valueAt(80) == approx(10.0f));
    CHECK(values.valueAt(99) == approx(10.0f));
}

TEST_CASE("An absolute lane covers the whole block without saying so", "[engine][param]") {
    OneParam table{64};

    const std::vector<ParamSegment> lane{{0, 0.5f, 0.5f}};

    ParamSources sources;
    sources.base = 0.0f;
    sources.automation = lane;

    const auto values = table.resolve(percentSpec(/*segmentAccurate=*/true), sources);

    REQUIRE(values.numSegments() == 1);
    CHECK(values.valueAt(63) == approx(50.0f));
}

TEST_CASE("A host write under an active modifier is not dropped", "[engine][param]") {
    // The bug class the two lanes exist to make impossible. In the incumbent
    // engine a parameter has one value and several writers, so a knob move
    // under a running LFO is overwritten by the LFO's next write and the user
    // watches the knob spring back. Here the write lands in a lane the
    // modulation never touches.
    OneParam table{256};

    const std::vector<ModContribution> lfo{{/*value=*/1.0f, /*amount=*/0.2f, /*bipolar=*/false}};

    ParamSources sources;
    sources.base = 0.3f;
    sources.modulation = lfo;

    CHECK(table.resolve(percentSpec(), sources).value() == approx(50.0f));

    // The knob moves. Nothing else changed.
    sources.base = 0.6f;

    CHECK(table.resolve(percentSpec(), sources).value() == approx(80.0f));
}

TEST_CASE("Modulation is summed before the one clamp", "[engine][param]") {
    OneParam table{32};

    // Individually the first of these leaves the range and the second brings it
    // back. Clamped per link the answer would be 40; clamped once it is 70,
    // which is where the parameter actually is.
    const std::vector<ModContribution> links{{1.0f, 0.8f, false}, {1.0f, -0.6f, false}};

    ParamSources sources;
    sources.base = 0.5f;
    sources.modulation = links;

    CHECK(table.resolve(percentSpec(), sources).value() == approx(70.0f));
}

TEST_CASE("Modulation is clamped into range at the end", "[engine][param]") {
    OneParam table{32};

    const std::vector<ModContribution> links{{1.0f, 0.9f, false}};

    ParamSources sources;
    sources.base = 0.8f;
    sources.modulation = links;

    CHECK(table.resolve(percentSpec(), sources).value() == approx(100.0f));
}

TEST_CASE("A bipolar link swings either side of the base", "[engine][param]") {
    OneParam table{32};

    ParamSources sources;
    sources.base = 0.5f;

    // A bipolar link at a quarter depth reaches a quarter of the range either
    // side of where the parameter is sitting.
    const std::vector<ModContribution> low{{0.0f, 0.25f, true}};
    sources.modulation = low;
    CHECK(table.resolve(percentSpec(), sources).value() == approx(25.0f));

    const std::vector<ModContribution> high{{1.0f, 0.25f, true}};
    sources.modulation = high;
    CHECK(table.resolve(percentSpec(), sources).value() == approx(75.0f));
}

TEST_CASE("A parameter that takes no modulation ignores its links", "[engine][param]") {
    OneParam table{32};

    auto spec = percentSpec();
    spec.modulatable = false;

    const std::vector<ModContribution> links{{1.0f, 1.0f, false}};

    ParamSources sources;
    sources.base = 0.25f;
    sources.modulation = links;

    CHECK(table.resolve(spec, sources).value() == approx(25.0f));
}

TEST_CASE("A device that reads once for the block gets one segment", "[engine][param]") {
    OneParam table{128};

    const std::vector<ParamSegment> lane{{0, 0.0f, 1.0f}};

    ParamSources sources;
    sources.automation = lane;

    // Not opted in: the value at the top of the block, held, which is what the
    // incumbent engine's parameters take at a block boundary.
    const auto blockRate = table.resolve(percentSpec(), sources);
    REQUIRE(blockRate.numSegments() == 1);
    CHECK(blockRate.isConstant());
    CHECK(blockRate.value() == approx(0.0f));
    CHECK(blockRate.valueAt(127) == approx(0.0f));

    // Opted in: the same lane, ramped.
    table.params.beginBlock(128);
    const auto segmented = table.resolve(percentSpec(/*segmentAccurate=*/true), sources);
    CHECK(segmented.valueAt(0) == approx(0.0f));
    CHECK(segmented.valueAt(64) == approx(50.0f));
    CHECK_FALSE(segmented.isConstant());
}

TEST_CASE("Consecutive segments meet at the sample they share", "[engine][param]") {
    OneParam table{100};

    const std::vector<ParamSegment> lane{{0, 0.0f, 0.5f}, {50, 0.5f, 1.0f}};

    ParamSources sources;
    sources.automation = lane;

    const auto values = table.resolve(percentSpec(/*segmentAccurate=*/true), sources);

    REQUIRE(values.numSegments() == 2);
    CHECK(values.valueAt(25) == approx(25.0f));
    CHECK(values.valueAt(49) == approx(49.0f));
    CHECK(values.valueAt(50) == approx(50.0f));
    CHECK(values.valueAt(75) == approx(75.0f));
}

TEST_CASE("Modulation shifts a ramp rather than flattening it", "[engine][param]") {
    OneParam table{100};

    const std::vector<ParamSegment> lane{{0, 0.0f, 0.5f}};
    const std::vector<ModContribution> links{{1.0f, 0.25f, false}};

    ParamSources sources;
    sources.automation = lane;
    sources.modulation = links;

    const auto values = table.resolve(percentSpec(/*segmentAccurate=*/true), sources);

    CHECK(values.valueAt(0) == approx(25.0f));
    CHECK(values.valueAt(50) == approx(50.0f));
}

TEST_CASE("A stepped parameter quantises and never ramps", "[engine][param]") {
    OneParam table{64};

    ParamSpec spec;
    spec.domain.scale = magda::ParameterScale::Discrete;
    spec.domain.choiceCount = 4;
    spec.segmentAccurate = true;

    const std::vector<ParamSegment> lane{{0, 0.0f, 1.0f}, {32, 1.0f, 1.0f}};

    ParamSources sources;
    sources.automation = lane;

    const auto values = table.resolve(spec, sources);

    REQUIRE(values.numSegments() == 2);
    // The first segment would ramp through 1.5 of a choice halfway along if a
    // ramp were allowed. It holds instead, and the value changes where the
    // next segment starts.
    CHECK(values.valueAt(0) == approx(0.0f));
    CHECK(values.valueAt(31) == approx(0.0f));
    CHECK(values.valueAt(32) == approx(3.0f));
}

TEST_CASE("A curve with more breakpoints than its slot arrives where it ends", "[engine][param]") {
    OneParam table{100, /*capacity=*/4};

    const std::vector<ParamSegment> lane{{0, 0.0f, 0.1f},  {10, 0.1f, 0.2f}, {20, 0.2f, 0.3f},
                                         {30, 0.3f, 0.4f}, {40, 0.4f, 0.5f}, {50, 0.5f, 0.8f}};

    ParamSources sources;
    sources.automation = lane;

    const auto values = table.resolve(percentSpec(/*segmentAccurate=*/true), sources);

    REQUIRE(values.numSegments() == 4);
    // The first three are the curve's own. The fourth carries the rest of it as
    // one ramp, and it ends where the curve ends rather than wherever it was
    // cut off.
    CHECK(values.valueAt(0) == approx(0.0f));
    CHECK(values.valueAt(20) == approx(20.0f));
    CHECK(values.segments().back().startSample == 30);
    CHECK(values.segments().back().endValue == approx(80.0f));
}

TEST_CASE("A parameter nobody resolved reads as empty", "[engine][param]") {
    ResolvedParams params;
    params.prepare(3);
    params.beginBlock(64);

    ParamSources sources;
    sources.base = 1.0f;
    resolveParam(params, 1, percentSpec(), sources);

    CHECK(params[0].empty());
    CHECK_FALSE(params[1].empty());
    CHECK(params[2].empty());

    // The next block starts with nothing carried over from this one.
    params.beginBlock(64);
    CHECK(params[1].empty());
}

TEST_CASE("A table hands a device its own parameters and no others", "[engine][param]") {
    ResolvedParams params;
    params.prepare(4);
    params.beginBlock(16);

    const auto spec = percentSpec();
    for (int i = 0; i < 4; ++i) {
        ParamSources sources;
        sources.base = 0.25f * static_cast<float>(i);
        resolveParam(params, i, spec, sources);
    }

    const auto device = params.device(/*firstParam=*/1, /*count=*/2);

    REQUIRE(device.size() == 2);
    CHECK(device[0].value() == approx(25.0f));
    CHECK(device[1].value() == approx(50.0f));

    // A device asking for a parameter it never declared gets nothing rather
    // than its neighbour's value.
    CHECK(device[2].empty());
    CHECK(device[-1].empty());
}

TEST_CASE("A spec carries the model parameter's domain", "[engine][param]") {
    magda::ParameterInfo info(0, "Cutoff", "Hz", 20.0f, 20000.0f, 632.0f,
                              magda::ParameterScale::Logarithmic);
    info.modulatable = false;

    const auto spec = paramSpecFrom(info);

    CHECK(spec.domain.scale == magda::ParameterScale::Logarithmic);
    CHECK(spec.domain.minValue == approx(20.0f));
    CHECK(spec.domain.maxValue == approx(20000.0f));
    CHECK_FALSE(spec.modulatable);
    // Nothing opts into segment accuracy during the port: the incumbent engine
    // settles a parameter at the block boundary, and a device resolving inside
    // the block would differ from it on every automated parameter.
    CHECK_FALSE(spec.segmentAccurate);

    OneParam table{32};
    ParamSources sources;
    sources.base = 0.5f;

    // Through the model's own curve, not a copy of it.
    CHECK(table.resolve(spec, sources).value() ==
          approx(magda::ParameterUtils::normalizedToReal(0.5f, info)));
}
