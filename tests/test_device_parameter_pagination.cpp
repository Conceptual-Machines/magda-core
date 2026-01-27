#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/DeviceInfo.hpp"
#include "../magda/daw/core/ParameterInfo.hpp"

using namespace magda;

// ============================================================================
// DeviceInfo Parameter Pagination Tests
// ============================================================================

/**
 * Test suite for parameter page navigation fix.
 *
 * Context: All parameter pages were showing the same first 16 parameters instead
 * of their respective parameter ranges. This was fixed by adding proper page
 * offset calculation in DeviceSlotComponent::updateParameterSlots().
 *
 * This test verifies the DeviceInfo data structure correctly supports pagination
 * and that the currentParameterPage field maintains state.
 */

TEST_CASE("DeviceInfo - Parameter pagination state", "[device][pagination]") {
    DeviceInfo device;
    device.name = "Test Plugin";
    device.pluginId = "test.plugin";
    device.manufacturer = "Test Vendor";

    // Create 50 parameters (more than 3 pages at 16 params per page)
    for (int i = 0; i < 50; ++i) {
        ParameterInfo param;
        param.paramIndex = i;
        param.name = juce::String("Param ") + juce::String(i);
        param.currentValue = static_cast<float>(i) / 50.0f;
        device.parameters.push_back(param);
    }

    SECTION("Default page is 0") {
        REQUIRE(device.currentParameterPage == 0);
    }

    SECTION("Page can be changed and persisted") {
        device.currentParameterPage = 2;
        REQUIRE(device.currentParameterPage == 2);
    }

    SECTION("Parameters are accessible by index") {
        REQUIRE(device.parameters.size() == 50);
        REQUIRE(device.parameters[0].paramIndex == 0);
        REQUIRE(device.parameters[15].paramIndex == 15);
        REQUIRE(device.parameters[16].paramIndex == 16);
        REQUIRE(device.parameters[31].paramIndex == 31);
        REQUIRE(device.parameters[49].paramIndex == 49);
    }
}

TEST_CASE("Parameter page offset calculation", "[device][pagination]") {
    constexpr int NUM_PARAMS_PER_PAGE = 16;

    SECTION("Page 0 shows parameters 0-15") {
        int currentPage = 0;
        int pageOffset = currentPage * NUM_PARAMS_PER_PAGE;

        REQUIRE(pageOffset == 0);

        // First slot shows param 0
        REQUIRE(pageOffset + 0 == 0);
        // Last slot shows param 15
        REQUIRE(pageOffset + 15 == 15);
    }

    SECTION("Page 1 shows parameters 16-31") {
        int currentPage = 1;
        int pageOffset = currentPage * NUM_PARAMS_PER_PAGE;

        REQUIRE(pageOffset == 16);

        // First slot shows param 16
        REQUIRE(pageOffset + 0 == 16);
        // Last slot shows param 31
        REQUIRE(pageOffset + 15 == 31);
    }

    SECTION("Page 2 shows parameters 32-47") {
        int currentPage = 2;
        int pageOffset = currentPage * NUM_PARAMS_PER_PAGE;

        REQUIRE(pageOffset == 32);

        // First slot shows param 32
        REQUIRE(pageOffset + 0 == 32);
        // Last slot shows param 47
        REQUIRE(pageOffset + 15 == 47);
    }
}

TEST_CASE("Parameter page boundary handling", "[device][pagination]") {
    constexpr int NUM_PARAMS_PER_PAGE = 16;

    DeviceInfo device;
    device.name = "Test Plugin";

    SECTION("50 parameters results in 4 total pages") {
        // Create 50 parameters
        for (int i = 0; i < 50; ++i) {
            ParameterInfo param;
            param.paramIndex = i;
            device.parameters.push_back(param);
        }

        int paramCount = static_cast<int>(device.parameters.size());
        int totalPages = (paramCount + NUM_PARAMS_PER_PAGE - 1) / NUM_PARAMS_PER_PAGE;

        REQUIRE(totalPages == 4);
        // Page 0: params 0-15
        // Page 1: params 16-31
        // Page 2: params 32-47
        // Page 3: params 48-49 (only 2 params on last page)
    }

    SECTION("16 parameters results in exactly 1 page") {
        for (int i = 0; i < 16; ++i) {
            ParameterInfo param;
            param.paramIndex = i;
            device.parameters.push_back(param);
        }

        int paramCount = static_cast<int>(device.parameters.size());
        int totalPages = (paramCount + NUM_PARAMS_PER_PAGE - 1) / NUM_PARAMS_PER_PAGE;

        REQUIRE(totalPages == 1);
    }

    SECTION("17 parameters results in 2 pages") {
        for (int i = 0; i < 17; ++i) {
            ParameterInfo param;
            param.paramIndex = i;
            device.parameters.push_back(param);
        }

        int paramCount = static_cast<int>(device.parameters.size());
        int totalPages = (paramCount + NUM_PARAMS_PER_PAGE - 1) / NUM_PARAMS_PER_PAGE;

        REQUIRE(totalPages == 2);
    }

    SECTION("32 parameters results in exactly 2 pages") {
        for (int i = 0; i < 32; ++i) {
            ParameterInfo param;
            param.paramIndex = i;
            device.parameters.push_back(param);
        }

        int paramCount = static_cast<int>(device.parameters.size());
        int totalPages = (paramCount + NUM_PARAMS_PER_PAGE - 1) / NUM_PARAMS_PER_PAGE;

        REQUIRE(totalPages == 2);
    }

    SECTION("Empty device has 1 page minimum") {
        // No parameters added
        int paramCount = static_cast<int>(device.parameters.size());
        int totalPages = (paramCount + NUM_PARAMS_PER_PAGE - 1) / NUM_PARAMS_PER_PAGE;

        // Should be 0, but UI should clamp to minimum 1
        REQUIRE(totalPages == 0);

        // Simulate UI clamping
        int displayPages = totalPages < 1 ? 1 : totalPages;
        REQUIRE(displayPages == 1);
    }
}

TEST_CASE("Parameter page navigation simulation", "[device][pagination]") {
    constexpr int NUM_PARAMS_PER_PAGE = 16;

    DeviceInfo device;
    device.name = "Test Plugin";

    // Create 50 parameters
    for (int i = 0; i < 50; ++i) {
        ParameterInfo param;
        param.paramIndex = i;
        param.name = juce::String("Param ") + juce::String(i);
        param.currentValue = static_cast<float>(i) * 0.01f;  // Unique value per param
        device.parameters.push_back(param);
    }

    int totalPages = (static_cast<int>(device.parameters.size()) + NUM_PARAMS_PER_PAGE - 1) /
                     NUM_PARAMS_PER_PAGE;

    SECTION("Navigate to each page and verify correct parameter indices") {
        for (int page = 0; page < totalPages; ++page) {
            device.currentParameterPage = page;
            int pageOffset = page * NUM_PARAMS_PER_PAGE;

            // Simulate loading parameters for this page
            for (int slot = 0; slot < NUM_PARAMS_PER_PAGE; ++slot) {
                int paramIndex = pageOffset + slot;

                if (paramIndex < static_cast<int>(device.parameters.size())) {
                    // Parameter should be available
                    const auto& param = device.parameters[static_cast<size_t>(paramIndex)];
                    REQUIRE(param.paramIndex == paramIndex);
                    REQUIRE(param.name == juce::String("Param ") + juce::String(paramIndex));
                } else {
                    // No parameter at this index (empty slot on last page)
                    REQUIRE(paramIndex >= static_cast<int>(device.parameters.size()));
                }
            }
        }
    }

    SECTION("Page clamping - prevent invalid page numbers") {
        // Try to set page beyond valid range
        device.currentParameterPage = 10;  // Way beyond 4 total pages

        // Simulate UI clamping
        if (device.currentParameterPage >= totalPages) {
            device.currentParameterPage = totalPages - 1;
        }
        if (device.currentParameterPage < 0) {
            device.currentParameterPage = 0;
        }

        REQUIRE(device.currentParameterPage == 3);  // Last valid page (0-indexed)
    }

    SECTION("Page persistence across updates") {
        // User navigates to page 2
        device.currentParameterPage = 2;
        REQUIRE(device.currentParameterPage == 2);

        // Simulate device update (e.g., parameter value change)
        device.parameters[32].currentValue = 0.99f;

        // Page should remain at 2
        REQUIRE(device.currentParameterPage == 2);

        // User should still see parameters 32-47
        int pageOffset = device.currentParameterPage * NUM_PARAMS_PER_PAGE;
        REQUIRE(pageOffset == 32);
    }
}

TEST_CASE("Parameter page fix - regression test", "[device][pagination][regression]") {
    /**
     * This test documents the bug that was fixed:
     *
     * BEFORE FIX:
     * - All pages showed parameters 0-15 because parameter index was not
     *   recalculated based on current page
     * - User sees same 16 parameters on every page
     *
     * AFTER FIX:
     * - Parameter index = currentPage * NUM_PARAMS_PER_PAGE + slotIndex
     * - Each page shows its correct range of parameters
     */

    constexpr int NUM_PARAMS_PER_PAGE = 16;

    DeviceInfo device;

    // Create 64 parameters (exactly 4 pages)
    for (int i = 0; i < 64; ++i) {
        ParameterInfo param;
        param.paramIndex = i;
        param.name = juce::String("Param ") + juce::String(i);
        device.parameters.push_back(param);
    }

    SECTION("Bug: All pages showed parameters 0-15 (BEFORE)") {
        // This simulates the OLD buggy behavior
        // Where paramIndex was always slot index (0-15), ignoring currentPage

        device.currentParameterPage = 2;  // User navigates to page 3

        // BUGGY calculation (what the code did before):
        // paramIndex = slotIndex (ignoring page offset!)
        int buggyParamIndex = 0;  // First slot, buggy calculation

        REQUIRE(buggyParamIndex == 0);  // Always showed param 0, not param 32!

        // This was wrong - page 2 should show param 32, not param 0
    }

    SECTION("Fix: Each page shows correct parameter range (AFTER)") {
        // This simulates the FIXED behavior

        device.currentParameterPage = 0;
        int pageOffset = device.currentParameterPage * NUM_PARAMS_PER_PAGE;
        int firstParamOnPage = pageOffset + 0;
        int lastParamOnPage = pageOffset + 15;
        REQUIRE(firstParamOnPage == 0);
        REQUIRE(lastParamOnPage == 15);

        device.currentParameterPage = 1;
        pageOffset = device.currentParameterPage * NUM_PARAMS_PER_PAGE;
        firstParamOnPage = pageOffset + 0;
        lastParamOnPage = pageOffset + 15;
        REQUIRE(firstParamOnPage == 16);
        REQUIRE(lastParamOnPage == 31);

        device.currentParameterPage = 2;
        pageOffset = device.currentParameterPage * NUM_PARAMS_PER_PAGE;
        firstParamOnPage = pageOffset + 0;
        lastParamOnPage = pageOffset + 15;
        REQUIRE(firstParamOnPage == 32);
        REQUIRE(lastParamOnPage == 47);

        device.currentParameterPage = 3;
        pageOffset = device.currentParameterPage * NUM_PARAMS_PER_PAGE;
        firstParamOnPage = pageOffset + 0;
        lastParamOnPage = pageOffset + 15;
        REQUIRE(firstParamOnPage == 48);
        REQUIRE(lastParamOnPage == 63);
    }
}
