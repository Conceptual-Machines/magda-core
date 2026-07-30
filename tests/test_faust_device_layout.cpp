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

TEST_CASE("FaustDeviceLayout names aligned effect pages from author groups", "[faust][layout]") {
    FaustDeviceLayout layout;
    DeviceInfo device;
    device.parameters = {param(0, "Filter"), param(7, "Filter"), param(32, "Modulation")};

    REQUIRE(layout.totalPages(device) == 2);
    REQUIRE(layout.pageName(device, 0) == "Filter");
    REQUIRE(layout.pageName(device, 1) == "Modulation");
    REQUIRE(layout.cellFor(device, 7, 0).targetParamIndex == 7);
    REQUIRE(layout.cellFor(device, 0, 1).targetParamIndex == 32);
}

TEST_CASE("FaustDeviceLayout falls back to numbers for interleaved or straddled groups",
          "[faust][layout]") {
    FaustDeviceLayout layout;

    DeviceInfo interleaved;
    interleaved.parameters = {param(0, "Filter"), param(1, "Envelope")};
    REQUIRE(layout.pageName(interleaved, 0) == "1");

    DeviceInfo straddled;
    straddled.parameters = {param(31, "Filter"), param(32, "Filter")};
    REQUIRE(layout.pageName(straddled, 0) == "1");
    REQUIRE(layout.pageName(straddled, 1) == "2");
}

TEST_CASE("FaustDeviceLayout group mode packs instrument groups into named pages",
          "[faust][layout]") {
    FaustDeviceLayout layout(FaustDeviceLayout::PageMode::Groups);
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

TEST_CASE("FaustDeviceLayout group mode names ungrouped and oversized pages", "[faust][layout]") {
    FaustDeviceLayout layout(FaustDeviceLayout::PageMode::Groups);

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

TEST_CASE("Faust instrument slots select the shared grouped layout", "[faust][layout]") {
    DeviceSlotTraits traits;
    traits.isFaustInstrument = true;

    auto layout = createDeviceSlotParamLayout(traits);
    auto* faustLayout = dynamic_cast<FaustDeviceLayout*>(layout.get());
    REQUIRE(faustLayout != nullptr);
    REQUIRE(faustLayout->pageMode() == FaustDeviceLayout::PageMode::Groups);
}
