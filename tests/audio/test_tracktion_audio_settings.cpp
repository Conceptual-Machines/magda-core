#include <catch2/catch_test_macros.hpp>
#include <numeric>

#include "magda/daw/audio/io/TracktionAudioSettings.hpp"
#include "magda/daw/core/Config.hpp"

/// @file Tracktion's saved audio interface, read for AudioIOService's first run (#2746).

namespace {

/** @brief @p values written as Tracktion's PropertyStorage writes Settings.xml, then read back. */
std::optional<magda::AudioIOSettings> migrate(const juce::String& values) {
    const juce::TemporaryFile file(".xml");
    REQUIRE(file.getFile().replaceWithText(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>\n" + values +
        "\n</PROPERTIES>\n"));
    return magda::readTracktionAudioSettings(file.getFile());
}

std::vector<int> firstChannels(int count) {
    std::vector<int> channels(static_cast<std::size_t>(count));
    std::iota(channels.begin(), channels.end(), 0);
    return channels;
}

const juce::String kAllOn = juce::String::repeatedString("1", 256);

}  // namespace

TEST_CASE("Tracktion's defaults migrate as the masks it had", "[audio-io][2746]") {
    // As a MAGDA that never touched Audio Settings leaves it: JUCE on default channels, which
    // MAGDA asked for 256 of, and every wave device on.
    const auto settings = migrate(R"(
      <VALUE name="audio_device_setup">
        <DEVICESETUP deviceType="CoreAudio" audioOutputDeviceName="M4" audioInputDeviceName="M4"
                     audioDeviceRate="44100.0" audioDeviceBufferSize="512">
          <MIDIINPUT name="M4" identifier="-2017238280"/>
        </DEVICESETUP>
      </VALUE>
      <VALUE name="audiosettings_CoreAudio">
        <AUDIODEVICE outEnabled=")" +
                                  kAllOn + R"(" inEnabled=")" + kAllOn +
                                  R"(" monoChansOut="0" stereoChansIn="0"/>
      </VALUE>)");

    REQUIRE(settings.has_value());
    CHECK(settings->backend == "CoreAudio");
    CHECK(settings->inputInterface == "M4");
    CHECK(settings->outputInterface == "M4");
    CHECK(settings->sampleRate == 44100.0);
    CHECK(settings->bufferSize == 512);
    CHECK(settings->inputChannels == firstChannels(256));
    CHECK(settings->outputChannels == firstChannels(256));
}

TEST_CASE("Channels switched off in Tracktion stay off", "[audio-io][2746]") {
    // Masks are written most significant bit first: "1100" is channels 2 and 3.
    const auto settings = migrate(R"(
      <VALUE name="audio_device_setup">
        <DEVICESETUP deviceType="Fake" audioOutputDeviceName="Out" audioInputDeviceName="In"
                     audioDeviceRate="48000.0" audioDeviceBufferSize="256"
                     audioDeviceInChans="111" audioDeviceOutChans="1111"/>
      </VALUE>
      <VALUE name="audiosettings_Fake">
        <AUDIODEVICE outEnabled="1100" inEnabled="1101" monoChansOut="0" stereoChansIn="0"/>
      </VALUE>
      <VALUE name="audiosettings_Other">
        <AUDIODEVICE outEnabled="1" inEnabled="1" monoChansOut="0" stereoChansIn="0"/>
      </VALUE>)");

    REQUIRE(settings.has_value());
    CHECK(settings->inputInterface == "In");
    CHECK(settings->outputInterface == "Out");
    CHECK(settings->sampleRate == 48000.0);
    CHECK(settings->bufferSize == 256);
    CHECK(settings->inputChannels == std::vector<int>{0, 2});
    CHECK(settings->outputChannels == std::vector<int>{2, 3});
}

TEST_CASE("A backend with one interface for both directions names it for both",
          "[audio-io][2746]") {
    const auto settings = migrate(R"(
      <VALUE name="audio_device_setup">
        <DEVICESETUP deviceType="ASIO" audioDeviceName="Babyface"
                     audioDeviceInChans="0" audioDeviceOutChans="11"/>
      </VALUE>)");

    REQUIRE(settings.has_value());
    CHECK(settings->inputInterface == "Babyface");
    CHECK(settings->outputInterface == "Babyface");
    CHECK(settings->inputChannels.empty());
    CHECK(settings->outputChannels == std::vector<int>{0, 1});
}

TEST_CASE("An interface Tracktion never opened migrates nothing", "[audio-io][2746]") {
    CHECK_FALSE(migrate(R"(<VALUE name="cpu" val="8"/>)").has_value());
    CHECK_FALSE(magda::readTracktionAudioSettings(juce::File()).has_value());

    const auto noInput = migrate(R"(
      <VALUE name="audio_device_setup">
        <DEVICESETUP deviceType="CoreAudio" audioOutputDeviceName="Speakers" audioInputDeviceName=""/>
      </VALUE>)");
    REQUIRE(noInput.has_value());
    CHECK(noInput->inputChannels.empty());
    CHECK(noInput->sampleRate == 0.0);
    CHECK(noInput->bufferSize == 0);
}

TEST_CASE("The chosen interface round-trips through config.json", "[audio-io][2746][config]") {
    auto& config = magda::Config::getInstance();
    const magda::AudioIOSettings chosen{.backend = "CoreAudio",
                                        .inputInterface = "M4",
                                        .outputInterface = "M4",
                                        .sampleRate = 48000.0,
                                        .bufferSize = 256,
                                        .inputChannels = {4, 5},
                                        .outputChannels = {0, 1}};

    config.setAudioIO(chosen);
    config.save();
    config.setAudioIO(std::nullopt);
    config.load();
    CHECK(config.getAudioIO() == chosen);

    config.setAudioIO(std::nullopt);
    config.save();
}
