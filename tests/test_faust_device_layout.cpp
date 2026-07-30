#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/ui/components/chain/layout/FaustDeviceLayout.hpp"
#include "../magda/daw/ui/components/chain/slot/DeviceSlotParamLayoutFactory.hpp"
#include "../magda/daw/ui/components/chain/slot/DeviceSlotTraits.hpp"

using namespace magda;
using namespace magda::daw::ui;

namespace {

ParameterInfo param(int index, const juce::String& group = {}) {
    ParameterInfo info;
    info.paramIndex = index;
    info.name = "Param " + juce::String(index);
    info.group = group;
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
