#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/ui/components/chain/layout/FaustDeviceLayout.hpp"
#include "../magda/daw/ui/components/chain/slot/DeviceSlotParamLayoutFactory.hpp"
#include "../magda/daw/ui/components/chain/slot/DeviceSlotTraits.hpp"

using namespace magda;
using namespace magda::daw::ui;

namespace {

ParameterInfo param(int index, const juce::String& group = {}, int widthCells = 1) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = "Param " + juce::String(index);
    info.group = group;
    info.widthCells = widthCells;
    return info;
}

}  // namespace

TEST_CASE("FaustDeviceLayout packs author groups into named pages", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {param(0, "Filter"), param(7, "Filter"), param(32, "Modulation")};

    REQUIRE(layout.totalPages(device) == 2);
    REQUIRE(layout.pageName(device, 0) == "Filter");
    REQUIRE(layout.pageName(device, 1) == "Modulation");
    REQUIRE(layout.cellFor(device, 0, 0).targetParamIndex == 0);
    REQUIRE(layout.cellFor(device, 1, 0).targetParamIndex == 7);
    REQUIRE(layout.cellFor(device, 0, 1).targetParamIndex == 32);
}

TEST_CASE("FaustDeviceLayout decouples groups from pool-slot page boundaries", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {
        param(31, "Filter"),
        param(1, "Envelope"),
        param(32, "Filter"),
    };

    REQUIRE(layout.totalPages(device) == 2);
    REQUIRE(layout.pageName(device, 0) == "Filter");
    REQUIRE(layout.pageName(device, 1) == "Envelope");
    REQUIRE(layout.cellFor(device, 0, 0).targetParamIndex == 31);
    REQUIRE(layout.cellFor(device, 1, 0).targetParamIndex == 32);
    REQUIRE(layout.cellFor(device, 0, 1).targetParamIndex == 1);
}

TEST_CASE("FaustDeviceLayout resolves stable parameter identities to group pages",
          "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {
        param(0, "Filter"),
        param(1, "Filter"),
        param(2, "Envelope"),
        param(4, "Oscillator"),
    };

    REQUIRE(layout.totalPages(device) == 3);
    REQUIRE(layout.pageName(device, 0) == "Filter");
    REQUIRE(layout.pageName(device, 1) == "Envelope");
    REQUIRE(layout.pageName(device, 2) == "Oscillator");
    REQUIRE(layout.cellFor(device, 0, 0).targetParamIndex == 0);
    REQUIRE(layout.cellFor(device, 1, 0).targetParamIndex == 1);
    REQUIRE(layout.cellFor(device, 0, 1).targetParamIndex == 2);
    REQUIRE(layout.cellFor(device, 0, 2).targetParamIndex == 4);
    REQUIRE(layout.pageForParameter(device, 4) == 2);
}

TEST_CASE("FaustDeviceLayout names ungrouped and oversized pages", "[faust][layout]") {
    FaustDeviceLayout layout;

    DeviceInfo ungrouped;
    ungrouped.parameters = {param(0), param(1)};
    REQUIRE(layout.pageName(ungrouped, 0) == "Params");

    DeviceInfo oversized;
    for (int i = 0; i < 33; ++i)
        oversized.parameters.push_back(param(i, "Bank"));
    REQUIRE(layout.totalPages(oversized) == 2);
    REQUIRE(layout.pageName(oversized, 0) == "Bank 1/2");
    REQUIRE(layout.pageName(oversized, 1) == "Bank 2/2");
    REQUIRE(layout.cellFor(oversized, 0, 1).targetParamIndex == 32);
}

TEST_CASE("FaustDeviceLayout invalidates cached pages when parameter structure changes",
          "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {param(0, "Filter"), param(1, "Filter")};

    REQUIRE(layout.totalPages(device) == 1);
    device.parameters[1].group = "Envelope";
    REQUIRE(layout.totalPages(device) == 2);
    REQUIRE(layout.pageName(device, 1) == "Envelope");
    REQUIRE(layout.cellFor(device, 0, 1).targetParamIndex == 1);

    device.parameters.push_back(param(2, "Envelope"));
    REQUIRE(layout.cellFor(device, 1, 1).targetParamIndex == 2);

    device.parameters[2].paramIndex = 17;
    REQUIRE(layout.cellFor(device, 1, 1).targetParamIndex == 17);
    REQUIRE(layout.pageForParameter(device, 17) == 1);
    REQUIRE(layout.pageForParameter(device, 2) == -1);
}

TEST_CASE("Faust effects and instruments use identical page and parameter mappings",
          "[faust][layout]") {
    DeviceSlotTraits effectTraits;
    effectTraits.isFaust = true;
    DeviceSlotTraits instrumentTraits;
    instrumentTraits.isFaustInstrument = true;

    auto effectLayout = createDeviceSlotParamLayout(effectTraits);
    auto instrumentLayout = createDeviceSlotParamLayout(instrumentTraits);
    REQUIRE(dynamic_cast<FaustDeviceLayout*>(effectLayout.get()) != nullptr);
    REQUIRE(dynamic_cast<FaustDeviceLayout*>(instrumentLayout.get()) != nullptr);

    DeviceInfo device;
    device.parameters = {
        param(2, "Oscillator"),
        param(40, "Filter"),
        param(7, "Oscillator"),
        param(63, "Envelope"),
    };

    REQUIRE(effectLayout->totalPages(device) == instrumentLayout->totalPages(device));
    for (int page = 0; page < effectLayout->totalPages(device); ++page) {
        REQUIRE(effectLayout->pageName(device, page) == instrumentLayout->pageName(device, page));
        for (int cell = 0; cell < effectLayout->cellCount(); ++cell) {
            const auto effectCell = effectLayout->cellFor(device, cell, page);
            const auto instrumentCell = instrumentLayout->cellFor(device, cell, page);
            REQUIRE(effectCell.mode == instrumentCell.mode);
            REQUIRE(effectCell.targetParamIndex == instrumentCell.targetParamIndex);
        }
    }
}

// `[width:N]` lets a control claim more than one grid cell, so a segmented
// choice row or a long readout is legible instead of being squeezed into an
// eighth of a row. The layout owns the packing; the annotation is a request.

TEST_CASE("FaustDeviceLayout gives a wide param its extra cells", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {param(0, "Engine", 3), param(1, "Engine")};

    const auto wide = layout.cellFor(device, 0, 0);
    REQUIRE(wide.mode == ParamCell::Mode::Filled);
    REQUIRE(wide.targetParamIndex == 0);
    REQUIRE(wide.span == 3);

    // The two cells it swallowed render nothing at all.
    REQUIRE(layout.cellFor(device, 1, 0).mode == ParamCell::Mode::Hidden);
    REQUIRE(layout.cellFor(device, 2, 0).mode == ParamCell::Mode::Hidden);

    // The next param resumes after the span, not after one cell.
    const auto next = layout.cellFor(device, 3, 0);
    REQUIRE(next.mode == ParamCell::Mode::Filled);
    REQUIRE(next.targetParamIndex == 1);
    REQUIRE(next.span == 1);
}

TEST_CASE("FaustDeviceLayout defaults to a single cell", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {param(0, "Engine"), param(1, "Engine")};
    REQUIRE(layout.cellFor(device, 0, 0).span == 1);
    REQUIRE(layout.cellFor(device, 1, 0).targetParamIndex == 1);
}

TEST_CASE("FaustDeviceLayout never splits a control across rows", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    // Six single cells fill most of row one; a 3-wide control cannot fit in the
    // two that remain, so it starts row two rather than wrapping mid-widget.
    for (int i = 0; i < 6; ++i)
        device.parameters.push_back(param(i, "Engine"));
    device.parameters.push_back(param(6, "Engine", 3));

    REQUIRE(layout.cellFor(device, 6, 0).mode == ParamCell::Mode::Hidden);
    REQUIRE(layout.cellFor(device, 7, 0).mode == ParamCell::Mode::Hidden);
    const auto wrapped = layout.cellFor(device, 8, 0);
    REQUIRE(wrapped.mode == ParamCell::Mode::Filled);
    REQUIRE(wrapped.targetParamIndex == 6);
    REQUIRE(wrapped.span == 3);
}

TEST_CASE("FaustDeviceLayout clamps a width wider than a row", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {param(0, "Engine", 99)};
    const auto cell = layout.cellFor(device, 0, 0);
    REQUIRE(cell.span == FaustDeviceLayout::kCellsPerRow);
    REQUIRE(layout.totalPages(device) == 1);
}

TEST_CASE("FaustDeviceLayout finds a wide param's page", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {param(0, "Engine", 4), param(7, "Mix")};
    REQUIRE(layout.pageForParameter(device, 0) == 0);
    REQUIRE(layout.pageForParameter(device, 7) == 1);
}

TEST_CASE("FaustDeviceLayout invalidates cached pages when a width changes", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {param(0, "Engine"), param(1, "Engine")};
    REQUIRE(layout.cellFor(device, 1, 0).targetParamIndex == 1);

    device.parameters[0].widthCells = 3;
    REQUIRE(layout.cellFor(device, 1, 0).mode == ParamCell::Mode::Hidden);
    REQUIRE(layout.cellFor(device, 3, 0).targetParamIndex == 1);
}

// ============================================================================
// Meter cells
// ============================================================================

namespace {

MeterInfo meter(int index, const juce::String& group = {}, int widthCells = 1) {
    MeterInfo info;
    info.meterIndex = index;
    info.name = "Meter " + juce::String(index);
    info.group = group;
    info.widthCells = widthCells;
    return info;
}

}  // namespace

TEST_CASE("FaustDeviceLayout places meters after their group's controls", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {param(0, "Dynamics"), param(1, "Dynamics")};
    device.meters = {meter(0, "Dynamics", 2)};

    REQUIRE(layout.totalPages(device) == 1);

    REQUIRE(layout.cellFor(device, 0, 0).mode == ParamCell::Mode::Filled);
    REQUIRE(layout.cellFor(device, 1, 0).mode == ParamCell::Mode::Filled);

    const auto meterCell = layout.cellFor(device, 2, 0);
    REQUIRE(meterCell.mode == ParamCell::Mode::Meter);
    REQUIRE(meterCell.meterArrayIndex == 0);
    REQUIRE(meterCell.span == 2);
    // A meter carries no parameter identity, so nothing can bind to it.
    REQUIRE(meterCell.paramArrayIndex == -1);
    REQUIRE(meterCell.targetParamIndex == -1);

    // The second cell of a two-wide meter is absorbed, same as for a control.
    REQUIRE(layout.cellFor(device, 3, 0).mode == ParamCell::Mode::Hidden);
}

TEST_CASE("FaustDeviceLayout gives a meter-only group its own page", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {param(0, "Filter")};
    device.meters = {meter(0, "Analysis")};

    REQUIRE(layout.totalPages(device) == 2);
    REQUIRE(layout.pageName(device, 1) == "Analysis");
    REQUIRE(layout.cellFor(device, 0, 1).mode == ParamCell::Mode::Meter);
    REQUIRE(layout.cellFor(device, 0, 1).meterArrayIndex == 0);
}

TEST_CASE("FaustDeviceLayout keeps meters out of parameter page lookups", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {param(4, "Dynamics")};
    device.meters = {meter(0, "Dynamics")};

    REQUIRE(layout.pageForParameter(device, 4) == 0);
    // Meter index 0 collides with no parameter: the lookup must not answer for it.
    REQUIRE(layout.pageForParameter(device, 0) == -1);
}
