#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>
#include <string>

#include "core/Config.hpp"
#include "media_db/MediaDbContext.hpp"

namespace magda::test {

/**
 * @brief Points the media DB at a temp directory for the life of the object.
 *
 * A test that indexes a file needs somewhere to put the row, and the real
 * library under the user's data dir is shared with whatever else is running.
 */
class TempMediaDb {
  public:
    TempMediaDb() {
        previousDir_ = magda::Config::getInstance().getMediaDbDir();
        dir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("magda_media_db_" +
                                 juce::String(juce::Random::getSystemRandom().nextInt(1000000)));
        dir_.createDirectory();
        magda::Config::getInstance().setMediaDbDir(dir_.getFullPathName().toStdString());
        magda::media::MediaDbContext::getInstance().resetForReopen();
    }

    ~TempMediaDb() {
        magda::media::MediaDbContext::getInstance().resetForReopen();
        magda::Config::getInstance().setMediaDbDir(previousDir_);
        magda::media::MediaDbContext::getInstance().resetForReopen();
        dir_.deleteRecursively();
    }

    TempMediaDb(const TempMediaDb&) = delete;
    TempMediaDb& operator=(const TempMediaDb&) = delete;

    [[nodiscard]] juce::File dir() const {
        return dir_;
    }

  private:
    juce::File dir_;
    std::string previousDir_;
};

/**
 * @brief Write a sine long enough that the indexer calls it a loop.
 *
 * Under two seconds it is a one-shot, and the indexer's policy clears a
 * one-shot's BPM — nothing musical should be read off one's length.
 */
inline juce::File writeTestWav(const juce::File& dir, const juce::String& name,
                               double seconds = 4.0) {
    const auto file = dir.getChildFile(name);
    juce::AudioBuffer<float> buffer(1, static_cast<int>(seconds * 44100.0));
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        buffer.setSample(
            0, i, 0.25F * std::sin(2.0F * 3.14159265F * 220.0F * static_cast<float>(i) / 44100.0F));
    }

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), 44100.0, 1, 16, {}, 0));
    if (writer != nullptr) {
        stream.release();
        writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }
    return file;
}

}  // namespace magda::test
