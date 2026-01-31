#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/ui/dialogs/ExportAudioDialog.hpp"

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
    settings.format = "WAV24";
    settings.sampleRate = 48000.0;
    settings.normalize = true;
    settings.exportRange = ExportAudioDialog::ExportRange::LoopRegion;
    settings.outputFile = juce::File("/tmp/test.wav");

    REQUIRE(settings.format == "WAV24");
    REQUIRE(settings.sampleRate == Catch::Approx(48000.0));
    REQUIRE(settings.normalize == true);
    REQUIRE(settings.exportRange == ExportAudioDialog::ExportRange::LoopRegion);
    REQUIRE(settings.outputFile.getFullPathName() == "/tmp/test.wav");
}

// ============================================================================
// Format Tests (via mock MainWindow methods)
// ============================================================================

// Helper class to test MainWindow methods without creating actual window
class ExportTestHelper {
  public:
    static juce::String getFileExtensionForFormat(const juce::String& format) {
        if (format.startsWith("WAV"))
            return ".wav";
        else if (format == "FLAC")
            return ".flac";
        return ".wav";  // Default
    }

    static int getBitDepthForFormat(const juce::String& format) {
        if (format == "WAV16")
            return 16;
        if (format == "WAV24")
            return 24;
        if (format == "WAV32")
            return 32;
        if (format == "FLAC")
            return 24;  // FLAC default
        return 16;      // Default
    }
};

TEST_CASE("File extension mapping - WAV formats", "[export][format]") {
    REQUIRE(ExportTestHelper::getFileExtensionForFormat("WAV16") == ".wav");
    REQUIRE(ExportTestHelper::getFileExtensionForFormat("WAV24") == ".wav");
    REQUIRE(ExportTestHelper::getFileExtensionForFormat("WAV32") == ".wav");
}

TEST_CASE("File extension mapping - FLAC format", "[export][format]") {
    REQUIRE(ExportTestHelper::getFileExtensionForFormat("FLAC") == ".flac");
}

TEST_CASE("File extension mapping - Default fallback", "[export][format]") {
    REQUIRE(ExportTestHelper::getFileExtensionForFormat("Unknown") == ".wav");
    REQUIRE(ExportTestHelper::getFileExtensionForFormat("") == ".wav");
}

TEST_CASE("Bit depth mapping - WAV formats", "[export][format]") {
    REQUIRE(ExportTestHelper::getBitDepthForFormat("WAV16") == 16);
    REQUIRE(ExportTestHelper::getBitDepthForFormat("WAV24") == 24);
    REQUIRE(ExportTestHelper::getBitDepthForFormat("WAV32") == 32);
}

TEST_CASE("Bit depth mapping - FLAC format", "[export][format]") {
    REQUIRE(ExportTestHelper::getBitDepthForFormat("FLAC") == 24);
}

TEST_CASE("Bit depth mapping - Default fallback", "[export][format]") {
    REQUIRE(ExportTestHelper::getBitDepthForFormat("Unknown") == 16);
    REQUIRE(ExportTestHelper::getBitDepthForFormat("") == 16);
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

    // WAV16 @ 44.1kHz
    settings.format = "WAV16";
    settings.sampleRate = 44100.0;
    REQUIRE(ExportTestHelper::getBitDepthForFormat(settings.format) == 16);
    REQUIRE(ExportTestHelper::getFileExtensionForFormat(settings.format) == ".wav");

    // WAV24 @ 48kHz
    settings.format = "WAV24";
    settings.sampleRate = 48000.0;
    REQUIRE(ExportTestHelper::getBitDepthForFormat(settings.format) == 24);

    // WAV32 @ 96kHz
    settings.format = "WAV32";
    settings.sampleRate = 96000.0;
    REQUIRE(ExportTestHelper::getBitDepthForFormat(settings.format) == 32);

    // FLAC @ 192kHz
    settings.format = "FLAC";
    settings.sampleRate = 192000.0;
    REQUIRE(ExportTestHelper::getBitDepthForFormat(settings.format) == 24);
    REQUIRE(ExportTestHelper::getFileExtensionForFormat(settings.format) == ".flac");
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
