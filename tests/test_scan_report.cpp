#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "engine/PluginScanCoordinator.hpp"

using Catch::Matchers::ContainsSubstring;

// ============================================================================
// PluginScanResult struct tests
// ============================================================================

TEST_CASE("PluginScanResult default values", "[scan-report]") {
    magda::PluginScanResult result;

    CHECK(result.pluginPath.isEmpty());
    CHECK(result.formatName.isEmpty());
    CHECK(result.success == false);
    CHECK(result.errorMessage.isEmpty());
    CHECK(result.durationMs == 0);
    CHECK(result.workerIndex == -1);
    CHECK(result.pluginNames.size() == 0);
}

TEST_CASE("PluginScanResult stores plugin data", "[scan-report]") {
    magda::PluginScanResult result;
    result.pluginPath = "/Library/Audio/Plug-Ins/VST3/Diva.vst3";
    result.formatName = "VST3";
    result.success = true;
    result.durationMs = 2100;
    result.workerIndex = 1;
    result.pluginNames.add("Diva");

    CHECK(result.pluginPath == "/Library/Audio/Plug-Ins/VST3/Diva.vst3");
    CHECK(result.formatName == "VST3");
    CHECK(result.success == true);
    CHECK(result.durationMs == 2100);
    CHECK(result.workerIndex == 1);
    CHECK(result.pluginNames.size() == 1);
    CHECK(result.pluginNames[0] == "Diva");
}

TEST_CASE("PluginScanResult stores multiple plugin names", "[scan-report]") {
    magda::PluginScanResult result;
    result.pluginPath = "/Library/Audio/Plug-Ins/VST3/MultiPlugin.vst3";
    result.success = true;
    result.pluginNames.add("Plugin A");
    result.pluginNames.add("Plugin B");
    result.pluginNames.add("Plugin C");

    CHECK(result.pluginNames.size() == 3);
    CHECK(result.pluginNames.joinIntoString(", ") == "Plugin A, Plugin B, Plugin C");
}

// ============================================================================
// Scan report file path test
// ============================================================================

TEST_CASE("getScanReportFile returns expected path", "[scan-report]") {
    magda::PluginScanCoordinator coordinator;
    auto reportFile = coordinator.getScanReportFile();

    CHECK(reportFile.getFileName() == "last_scan_report.txt");
    CHECK(reportFile.getParentDirectory().getFileName() == "MAGDA");
}

// ============================================================================
// Scan report content tests (using writeScanReport indirectly)
// ============================================================================
// Note: writeScanReport() is private. We test the report format by verifying
// the coordinator produces a valid report after a simulated scan lifecycle.
// Since we can't call startScan without a real format manager, we test
// the report file path and struct behavior, and verify the report is written
// after a real scan via integration testing.

TEST_CASE("PluginScanResult failure cases", "[scan-report]") {
    SECTION("crash result") {
        magda::PluginScanResult result;
        result.pluginPath = "/Library/Audio/Plug-Ins/VST3/Bad.vst3";
        result.success = false;
        result.errorMessage = "crash";
        result.durationMs = 3400;
        result.workerIndex = 2;

        CHECK_FALSE(result.success);
        CHECK(result.errorMessage == "crash");
        CHECK(result.pluginNames.size() == 0);
    }

    SECTION("timeout result") {
        magda::PluginScanResult result;
        result.pluginPath = "/Library/Audio/Plug-Ins/VST3/Stuck.vst3";
        result.success = false;
        result.errorMessage = "timeout (120s)";
        result.durationMs = 120000;
        result.workerIndex = 0;

        CHECK_FALSE(result.success);
        CHECK_THAT(result.errorMessage.toStdString(), ContainsSubstring("timeout"));
    }

    SECTION("error result") {
        magda::PluginScanResult result;
        result.pluginPath = "/Library/Audio/Plug-Ins/VST3/Empty.vst3";
        result.success = false;
        result.errorMessage = "No plugins found in file";
        result.durationMs = 800;

        CHECK_FALSE(result.success);
        CHECK(result.errorMessage == "No plugins found in file");
    }
}

TEST_CASE("PluginScanResult vector aggregation", "[scan-report]") {
    std::vector<magda::PluginScanResult> results;

    // Add some successes
    for (int i = 0; i < 5; ++i) {
        magda::PluginScanResult r;
        r.pluginPath = "/path/plugin" + juce::String(i) + ".vst3";
        r.formatName = "VST3";
        r.success = true;
        r.durationMs = 1000 + i * 500;
        r.workerIndex = i % 4;
        r.pluginNames.add("Plugin " + juce::String(i));
        results.push_back(r);
    }

    // Add some failures
    {
        magda::PluginScanResult r;
        r.pluginPath = "/path/crash.vst3";
        r.success = false;
        r.errorMessage = "crash";
        r.durationMs = 3000;
        r.workerIndex = 1;
        results.push_back(r);
    }
    {
        magda::PluginScanResult r;
        r.pluginPath = "/path/timeout.vst3";
        r.success = false;
        r.errorMessage = "timeout (120s)";
        r.durationMs = 120000;
        r.workerIndex = 2;
        results.push_back(r);
    }

    // Verify aggregation
    int successCount = 0;
    int failCount = 0;
    int totalPluginsFound = 0;
    for (const auto& r : results) {
        if (r.success) {
            successCount++;
            totalPluginsFound += r.pluginNames.size();
        } else {
            failCount++;
        }
    }

    CHECK(results.size() == 7);
    CHECK(successCount == 5);
    CHECK(failCount == 2);
    CHECK(totalPluginsFound == 5);
}
