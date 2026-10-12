#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../../magda/daw/ui/dialogs/ExportAudioDialog.hpp"
#include "../../magda/daw/ui/dialogs/ExportFormats.hpp"

using namespace magda;

// ============================================================================
// ExportRange Enum Tests
// ============================================================================

TEST_CASE("ExportRange - Enum values exist", "[export][dialog]") {
    // Verify all expected export range options are available
    auto entireSong = ExportAudioDialog::ExportRange::EntireSong;
    auto timeSelection = ExportAudioDialog::ExportRange::TimeSelection;
    auto loopRegion = ExportAudioDialog::ExportRange::LoopRegion;

    REQUIRE(static_cast<int>(entireSong) != static_cast<int>(timeSelection));
    REQUIRE(static_cast<int>(entireSong) != static_cast<int>(loopRegion));
    REQUIRE(static_cast<int>(timeSelection) != static_cast<int>(loopRegion));
}

// ============================================================================
// Settings Structure Tests
// ============================================================================

TEST_CASE("ExportAudioDialog::Settings - Default construction", "[export][dialog]") {
    ExportAudioDialog::Settings settings;

    // Should have default values (now properly initialized)
    REQUIRE(settings.format.isEmpty());
    REQUIRE(settings.sampleRate == Catch::Approx(48000.0));
    REQUIRE(settings.normalize == false);
    REQUIRE(settings.exportRange == ExportAudioDialog::ExportRange::EntireSong);
    REQUIRE(settings.outputFile == juce::File());
}

TEST_CASE("ExportAudioDialog::Settings - Custom values", "[export][dialog]") {
    ExportAudioDialog::Settings settings;
    settings.format = "WAV";
    settings.bitDepth = 16;
    settings.sampleRate = 48000.0;
    settings.normalize = true;
    settings.exportRange = ExportAudioDialog::ExportRange::LoopRegion;
    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto testFile = tempDir.getChildFile("test.wav");
    settings.outputFile = testFile;

    REQUIRE(settings.format == "WAV");
    REQUIRE(settings.bitDepth == 16);
    REQUIRE(settings.sampleRate == Catch::Approx(48000.0));
    REQUIRE(settings.normalize == true);
    REQUIRE(settings.exportRange == ExportAudioDialog::ExportRange::LoopRegion);
    REQUIRE(settings.outputFile.getFullPathName() == testFile.getFullPathName());
}

// ============================================================================
// Format Tests
// ============================================================================

TEST_CASE("File extension mapping - WAV", "[export][format]") {
    REQUIRE(exportExtensionFor("WAV") == ".wav");
}

TEST_CASE("File extension mapping - FLAC, Opus and MP3", "[export][format]") {
    REQUIRE(exportExtensionFor("FLAC") == ".flac");
    REQUIRE(exportExtensionFor("OPUS") == ".opus");
    REQUIRE(exportExtensionFor("MP3") == ".mp3");
}

TEST_CASE("File extension mapping - Default fallback", "[export][format]") {
    REQUIRE(exportExtensionFor("Unknown") == ".wav");
    REQUIRE(exportExtensionFor("") == ".wav");
}

TEST_CASE("Bit depths each format offers", "[export][format][3034]") {
    REQUIRE(exportBitDepthsFor("WAV") == std::vector<int>{16, 24, 32});
    REQUIRE(exportBitDepthsFor("FLAC") == std::vector<int>{16, 24});
    REQUIRE(exportBitDepthsFor("OPUS").empty());
    REQUIRE(exportBitDepthsFor("MP3").empty());
}

TEST_CASE("Saved format names from before the depth had its own field", "[export][format][3034]") {
    REQUIRE(normaliseExportFormat("WAV16") == "WAV");
    REQUIRE(normaliseExportFormat("WAV24") == "WAV");
    REQUIRE(normaliseExportFormat("WAV32") == "WAV");
    REQUIRE(normaliseExportFormat("FLAC") == "FLAC");
    REQUIRE(normaliseExportFormat("OPUS") == "OPUS");
    REQUIRE(normaliseExportFormat("MP3") == "MP3");
    REQUIRE(normaliseExportFormat("") == "WAV");
}

TEST_CASE("Render format mapping", "[export][format][2955]") {
    REQUIRE(exportRenderFormatFor("WAV") == OfflineRenderFormat::Wav);
    REQUIRE(exportRenderFormatFor("FLAC") == OfflineRenderFormat::Flac);
    REQUIRE(exportRenderFormatFor("OPUS") == OfflineRenderFormat::Opus);
    REQUIRE(exportRenderFormatFor("Unknown") == OfflineRenderFormat::Wav);

    REQUIRE(exportRenderFormatFor("MP3") == OfflineRenderFormat::Mp3);

    REQUIRE(isLossyExportFormat("OPUS"));
    REQUIRE(isLossyExportFormat("MP3"));
    REQUIRE_FALSE(isLossyExportFormat("FLAC"));
}

TEST_CASE("MP3 bitrate choices", "[export][format][3034]") {
    // A CBR item is its bitrate; a VBR item is the level, with no bitrate.
    const auto cbr = mp3QualityForItemId(192);
    CHECK(cbr.bitrateKbps == 192);
    CHECK_FALSE(cbr.vbrQuality.has_value());

    const auto vbr = mp3QualityForItemId(kDefaultMp3ItemId);
    CHECK(vbr.bitrateKbps == 0);
    CHECK(vbr.vbrQuality == 2);

    // Every CBR choice is below the VBR ids, so the two never collide.
    for (const auto kbps : kMp3CbrBitratesKbps)
        CHECK(kbps < kMp3VbrItemIdBase);
}

// ============================================================================
// Sample Rate Tests
// ============================================================================

TEST_CASE("Sample rate - Common values", "[export][samplerate]") {
    ExportAudioDialog::Settings settings;

    // Test common sample rates
    settings.sampleRate = 44100.0;
    REQUIRE(settings.sampleRate == Catch::Approx(44100.0));

    settings.sampleRate = 48000.0;
    REQUIRE(settings.sampleRate == Catch::Approx(48000.0));

    settings.sampleRate = 96000.0;
    REQUIRE(settings.sampleRate == Catch::Approx(96000.0));

    settings.sampleRate = 192000.0;
    REQUIRE(settings.sampleRate == Catch::Approx(192000.0));
}

// ============================================================================
// Normalization Tests
// ============================================================================

TEST_CASE("Normalization - Boolean flag", "[export][normalization]") {
    ExportAudioDialog::Settings settings;

    settings.normalize = false;
    REQUIRE_FALSE(settings.normalize);

    settings.normalize = true;
    REQUIRE(settings.normalize);
}

// ============================================================================
// Export Range Tests
// ============================================================================

TEST_CASE("Export range - EntireSong", "[export][range]") {
    ExportAudioDialog::Settings settings;
    settings.exportRange = ExportAudioDialog::ExportRange::EntireSong;
    REQUIRE(settings.exportRange == ExportAudioDialog::ExportRange::EntireSong);
}

TEST_CASE("Export range - TimeSelection", "[export][range]") {
    ExportAudioDialog::Settings settings;
    settings.exportRange = ExportAudioDialog::ExportRange::TimeSelection;
    REQUIRE(settings.exportRange == ExportAudioDialog::ExportRange::TimeSelection);
}

TEST_CASE("Export range - LoopRegion", "[export][range]") {
    ExportAudioDialog::Settings settings;
    settings.exportRange = ExportAudioDialog::ExportRange::LoopRegion;
    REQUIRE(settings.exportRange == ExportAudioDialog::ExportRange::LoopRegion);
}

// ============================================================================
// Format + Sample Rate Combinations
// ============================================================================

TEST_CASE("Valid format/sample rate combinations", "[export][integration]") {
    ExportAudioDialog::Settings settings;

    settings.format = "WAV";
    settings.sampleRate = 96000.0;
    REQUIRE(exportExtensionFor(settings.format) == ".wav");
    REQUIRE(exportRenderFormatFor(settings.format) == OfflineRenderFormat::Wav);

    settings.format = "FLAC";
    settings.sampleRate = 192000.0;
    REQUIRE(exportExtensionFor(settings.format) == ".flac");
    REQUIRE(exportRenderFormatFor(settings.format) == OfflineRenderFormat::Flac);
}

// ============================================================================
// File Path Tests
// ============================================================================

TEST_CASE("Output file paths", "[export][file]") {
    ExportAudioDialog::Settings settings;

    settings.outputFile = juce::File("/Users/test/Desktop/export.wav");
    REQUIRE(settings.outputFile.getFileName() == "export.wav");
    REQUIRE(settings.outputFile.getFileExtension() == ".wav");

    settings.outputFile = juce::File("/tmp/audio/mixdown.flac");
    REQUIRE(settings.outputFile.getFileName() == "mixdown.flac");
    REQUIRE(settings.outputFile.getFileExtension() == ".flac");
}
