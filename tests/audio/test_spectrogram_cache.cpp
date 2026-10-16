// The editor's spectrogram (#3065): computed in the background, drawn over any source range.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
#include <numbers>

#include "magda/daw/audio/SpectrogramCache.hpp"

using namespace magda;

namespace {

constexpr int kRate = 48000;

/** @brief One second of a 1 kHz tone, then @p silentSeconds of silence, in stereo. */
void writeToneThenSilence(const juce::File& file, double silentSeconds) {
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    REQUIRE(stream != nullptr);
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), kRate, 2, 24, {}, 0));
    REQUIRE(writer != nullptr);
    stream.release();

    const int n = static_cast<int>((1.0 + silentSeconds) * kRate);
    juce::AudioBuffer<float> buf(2, n);
    buf.clear();
    for (int i = 0; i < kRate; ++i) {
        const auto v =
            static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * i / kRate));
        buf.setSample(0, i, v);
        buf.setSample(1, i, v);
    }
    REQUIRE(writer->writeFromAudioSampleBuffer(buf, 0, n));
}

struct Counter : SpectrogramListener {
    int calls = 0;
    void spectrogramChanged(const juce::String&) override {
        ++calls;
    }
};

/** @brief A message loop and a scratch directory; the pool stops before the loop goes. */
struct SpectrogramFixture {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File dir;
    Counter counter;

    SpectrogramFixture() {
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("magda_spectrogram_" + juce::Uuid().toString());
        REQUIRE(dir.createDirectory());
        SpectrogramCache::getInstance().addListener(&counter);
    }

    ~SpectrogramFixture() {
        SpectrogramCache::getInstance().removeListener(&counter);
        SpectrogramCache::getInstance().stopBackgroundWork();
        dir.deleteRecursively();
    }

    static bool pumpUntil(const std::function<bool()>& done) {
        const auto deadline = juce::Time::getMillisecondCounter() + 10000;
        while (!done() && juce::Time::getMillisecondCounter() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        return done();
    }
};

/** @brief Draws seconds [@p start, @p end) of @p path 100 x kRows; true if anything was drawn. */
bool render(juce::Image& image, const juce::String& path, double start, double end) {
    image = juce::Image(juce::Image::ARGB, 100, SpectrogramCache::kRows, true);
    juce::Graphics g(image);
    return SpectrogramCache::getInstance().draw(g, image.getBounds(), path, start, end);
}

/** @brief The image row 1 kHz lands on (30 Hz to 20 kHz, high at the top). */
int rowOf1k() {
    const double r = SpectrogramCache::kRows * std::log(1000.0 / 30.0) / std::log(20000.0 / 30.0);
    return SpectrogramCache::kRows - 1 - static_cast<int>(r);
}

}  // namespace

TEST_CASE("A spectrogram is drawn once the background has read the file", "[spectrogram]") {
    SpectrogramFixture f;
    const auto file = f.dir.getChildFile("tone.wav");
    writeToneThenSilence(file, 1.0);
    const auto path = file.getFullPathName();

    juce::Image image;
    CHECK_FALSE(render(image, path, 0.0, 2.0));

    // Bright on the tone's row while it plays, the floor colour once it stops.
    REQUIRE(SpectrogramFixture::pumpUntil([&] {
        return render(image, path, 0.0, 2.0) &&
               image.getPixelAt(25, rowOf1k()).getBrightness() > 0.8f;
    }));
    CHECK(f.counter.calls > 0);
    CHECK(image.getPixelAt(75, rowOf1k()) == SpectrogramCache::colourAt(0.0f));
}

TEST_CASE("A zoomed-in view fills in with finer tiles", "[spectrogram]") {
    SpectrogramFixture f;
    const auto file = f.dir.getChildFile("tone.wav");
    writeToneThenSilence(file, 1.0);
    const auto path = file.getFullPathName();

    juce::Image image;
    REQUIRE(SpectrogramFixture::pumpUntil([&] { return render(image, path, 0.0, 2.0); }));

    // 100 px over 50 ms straddling the tone's end: half bright, half floor.
    REQUIRE(SpectrogramFixture::pumpUntil([&] {
        render(image, path, 0.975, 1.025);
        return image.getPixelAt(10, rowOf1k()).getBrightness() > 0.8f &&
               image.getPixelAt(90, rowOf1k()) == SpectrogramCache::colourAt(0.0f);
    }));
}

TEST_CASE("A file that cannot be read draws nothing", "[spectrogram]") {
    SpectrogramFixture f;
    const auto path = f.dir.getChildFile("missing.wav").getFullPathName();

    juce::Image image;
    CHECK_FALSE(render(image, path, 0.0, 1.0));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
    CHECK_FALSE(render(image, path, 0.0, 1.0));
}

TEST_CASE("Invalidating a file drops what was computed for it", "[spectrogram]") {
    SpectrogramFixture f;
    const auto file = f.dir.getChildFile("tone.wav");
    writeToneThenSilence(file, 1.0);
    const auto path = file.getFullPathName();

    juce::Image image;
    REQUIRE(SpectrogramFixture::pumpUntil([&] { return render(image, path, 0.0, 2.0); }));
    SpectrogramCache::getInstance().invalidateFile(path);
    CHECK_FALSE(render(image, path, 0.0, 2.0));
}
