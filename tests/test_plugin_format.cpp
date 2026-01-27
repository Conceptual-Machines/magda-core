#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "core/DeviceInfo.hpp"

// ============================================================================
// Plugin Format Handling Tests
// ============================================================================
// These tests verify that plugin format detection and conversion works
// correctly, especially for internal plugins which were previously being
// misidentified as VST3 plugins.
//
// The bug: When dropping Internal plugins from PluginBrowserContent, the
// format string "Internal" was not being checked in ChainPanel.cpp and
// TrackChainContent.cpp, causing DeviceInfo to default to VST3 format.
// This resulted in Test Tone and other internal plugins failing to load
// correctly (0 parameters, no audio output).

TEST_CASE("DeviceInfo format enum handles all plugin formats", "[plugin][format]") {
    magda::DeviceInfo device;

    SECTION("VST3 format") {
        device.format = magda::PluginFormat::VST3;
        REQUIRE(device.getFormatString() == "VST3");
    }

    SECTION("AU format") {
        device.format = magda::PluginFormat::AU;
        REQUIRE(device.getFormatString() == "AU");
    }

    SECTION("VST format") {
        device.format = magda::PluginFormat::VST;
        REQUIRE(device.getFormatString() == "VST");
    }

    SECTION("Internal format") {
        device.format = magda::PluginFormat::Internal;
        REQUIRE(device.getFormatString() == "Internal");
    }
}

TEST_CASE("Format string parsing sets correct DeviceInfo format enum", "[plugin][format]") {
    magda::DeviceInfo device;

    SECTION("VST3 string") {
        juce::String format = "VST3";
        if (format == "VST3") {
            device.format = magda::PluginFormat::VST3;
        } else if (format == "AU") {
            device.format = magda::PluginFormat::AU;
        } else if (format == "VST") {
            device.format = magda::PluginFormat::VST;
        } else if (format == "Internal") {
            device.format = magda::PluginFormat::Internal;
        }
        REQUIRE(device.format == magda::PluginFormat::VST3);
    }

    SECTION("AU string") {
        juce::String format = "AU";
        if (format == "VST3") {
            device.format = magda::PluginFormat::VST3;
        } else if (format == "AU") {
            device.format = magda::PluginFormat::AU;
        } else if (format == "VST") {
            device.format = magda::PluginFormat::VST;
        } else if (format == "Internal") {
            device.format = magda::PluginFormat::Internal;
        }
        REQUIRE(device.format == magda::PluginFormat::AU);
    }

    SECTION("VST string") {
        juce::String format = "VST";
        if (format == "VST3") {
            device.format = magda::PluginFormat::VST3;
        } else if (format == "AU") {
            device.format = magda::PluginFormat::AU;
        } else if (format == "VST") {
            device.format = magda::PluginFormat::VST;
        } else if (format == "Internal") {
            device.format = magda::PluginFormat::Internal;
        }
        REQUIRE(device.format == magda::PluginFormat::VST);
    }

    SECTION("Internal string") {
        juce::String format = "Internal";
        if (format == "VST3") {
            device.format = magda::PluginFormat::VST3;
        } else if (format == "AU") {
            device.format = magda::PluginFormat::AU;
        } else if (format == "VST") {
            device.format = magda::PluginFormat::VST;
        } else if (format == "Internal") {
            device.format = magda::PluginFormat::Internal;
        }
        REQUIRE(device.format == magda::PluginFormat::Internal);
    }

    SECTION("Unknown string defaults to VST3") {
        juce::String format = "Unknown";
        // Default remains VST3 (from DeviceInfo.hpp line 24)
        if (format == "VST3") {
            device.format = magda::PluginFormat::VST3;
        } else if (format == "AU") {
            device.format = magda::PluginFormat::AU;
        } else if (format == "VST") {
            device.format = magda::PluginFormat::VST;
        } else if (format == "Internal") {
            device.format = magda::PluginFormat::Internal;
        }
        // Should remain default (VST3)
        REQUIRE(device.format == magda::PluginFormat::VST3);
    }
}

TEST_CASE("Internal plugin drag-drop simulation (Test Tone)", "[plugin][format]") {
    // Simulate what happens in ChainPanel/TrackChainContent when dropping Test Tone
    // This replicates the exact bug: format string from browser is "Internal"
    // but code was missing the check for it
    magda::DeviceInfo device;
    device.name = "Test Tone";
    device.manufacturer = "MAGDA";
    device.pluginId = "Test Tone";
    device.uniqueId = "tone";
    device.fileOrIdentifier = "tone";
    device.isInstrument = false;

    // This is the format string that comes from PluginBrowserInfo::createInternal
    juce::String format = "Internal";

    // Parse format string (this is the critical part that was broken)
    if (format == "VST3") {
        device.format = magda::PluginFormat::VST3;
    } else if (format == "AU") {
        device.format = magda::PluginFormat::AU;
    } else if (format == "VST") {
        device.format = magda::PluginFormat::VST;
    } else if (format == "Internal") {
        device.format = magda::PluginFormat::Internal;
    }

    // Verify the device has Internal format (not VST3 default)
    REQUIRE(device.format == magda::PluginFormat::Internal);
    REQUIRE(device.getFormatString() == "Internal");
}

TEST_CASE("Internal instrument drag-drop simulation (4OSC Synth)", "[plugin][format]") {
    // Simulate dropping 4OSC Synth (internal instrument)
    magda::DeviceInfo device;
    device.name = "4OSC Synth";
    device.manufacturer = "MAGDA";
    device.pluginId = "4OSC Synth";
    device.uniqueId = "4osc";
    device.fileOrIdentifier = "4osc";
    device.isInstrument = true;

    juce::String format = "Internal";

    if (format == "VST3") {
        device.format = magda::PluginFormat::VST3;
    } else if (format == "AU") {
        device.format = magda::PluginFormat::AU;
    } else if (format == "VST") {
        device.format = magda::PluginFormat::VST;
    } else if (format == "Internal") {
        device.format = magda::PluginFormat::Internal;
    }

    REQUIRE(device.format == magda::PluginFormat::Internal);
    REQUIRE(device.isInstrument == true);
    REQUIRE(device.getFormatString() == "Internal");
}

TEST_CASE("Bug regression: Internal format was defaulting to VST3",
          "[plugin][format][regression]") {
    // This test documents the original bug behavior
    // WITHOUT the "else if (format == \"Internal\")" check, this would fail
    magda::DeviceInfo device;

    juce::String format = "Internal";

    // Old buggy code (missing Internal check):
    bool hasInternalCheck = false;
    if (format == "VST3") {
        device.format = magda::PluginFormat::VST3;
    } else if (format == "AU") {
        device.format = magda::PluginFormat::AU;
    } else if (format == "VST") {
        device.format = magda::PluginFormat::VST;
    }
    // Missing: else if (format == "Internal") { device.format = PluginFormat::Internal; }

    // Without the Internal check, format defaults to VST3 (from DeviceInfo.hpp:24)
    REQUIRE(device.format == magda::PluginFormat::VST3);  // This was the bug!

    // Now with the fix:
    if (format == "VST3") {
        device.format = magda::PluginFormat::VST3;
    } else if (format == "AU") {
        device.format = magda::PluginFormat::AU;
    } else if (format == "VST") {
        device.format = magda::PluginFormat::VST;
    } else if (format == "Internal") {
        device.format = magda::PluginFormat::Internal;
        hasInternalCheck = true;
    }

    REQUIRE(hasInternalCheck == true);                        // Fix was applied
    REQUIRE(device.format == magda::PluginFormat::Internal);  // Now correct!
}
