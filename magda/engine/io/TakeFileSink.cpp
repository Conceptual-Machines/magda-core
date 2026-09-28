#include "io/TakeFileSink.hpp"

#include <algorithm>
#include <utility>

namespace magda::engine {

TakeFileSink::TakeFileSink(juce::File directory, juce::String baseName, AudioFileSpec spec,
                           RenderContext context)
    : directory_(std::move(directory)),
      baseName_(std::move(baseName)),
      spec_(spec),
      context_(context) {
    directory_.createDirectory();
}

bool TakeFileSink::markPassEnd(std::int64_t takeSample) {
    const auto write = boundaryWrite_.load(std::memory_order_relaxed);
    const auto read = boundaryRead_.load(std::memory_order_acquire);

    if (write - read >= kBoundaryCapacity)
        return false;

    boundaries_[static_cast<std::size_t>(write % kBoundaryCapacity)] = takeSample;
    boundaryWrite_.store(write + 1, std::memory_order_release);
    return true;
}

std::optional<std::int64_t> TakeFileSink::peekBoundary() const {
    const auto read = boundaryRead_.load(std::memory_order_relaxed);

    if (boundaryWrite_.load(std::memory_order_acquire) == read)
        return {};

    return boundaries_[static_cast<std::size_t>(read % kBoundaryCapacity)];
}

void TakeFileSink::popBoundary() {
    boundaryRead_.store(boundaryRead_.load(std::memory_order_relaxed) + 1,
                        std::memory_order_release);
}

bool TakeFileSink::writeAudio(juce::dsp::AudioBlock<const float> audio, int numSamples) {
    auto offset = 0;

    while (offset < numSamples) {
        // Boundaries first: one already reached ends the pass before anything
        // more goes into it, and two in a row are a pass that captured nothing.
        for (auto boundary = peekBoundary(); boundary && *boundary <= written_;
             boundary = peekBoundary()) {
            closePass();
            popBoundary();
        }

        auto chunk = numSamples - offset;
        if (const auto boundary = peekBoundary(); boundary)
            chunk = static_cast<int>(std::min<std::int64_t>(chunk, *boundary - written_));

        if (!writeToPass(audio.getSubBlock(static_cast<std::size_t>(offset),
                                           static_cast<std::size_t>(chunk)),
                         chunk))
            return false;

        offset += chunk;
        written_ += chunk;
    }

    return true;
}

bool TakeFileSink::writeToPass(juce::dsp::AudioBlock<const float> audio, int numSamples) {
    if (failed_)
        return false;

    if (pass_ == nullptr) {
        passFile_ = nextFile();
        pass_ = AudioFileSink::create(passFile_, spec_, context_);

        if (pass_ == nullptr) {
            failed_ = true;
            return false;
        }
    }

    pass_->write(audio, numSamples);

    if (pass_->failed()) {
        failed_ = true;
        return false;
    }

    return true;
}

void TakeFileSink::closePass() {
    if (pass_ == nullptr)
        return;

    const auto samples = pass_->samplesWritten();
    const auto stored = pass_->close();

    pass_.reset();

    if (!stored) {
        failed_ = true;
        return;
    }

    if (samples > 0)
        passes_.push_back(
            {passFile_, samples,
             context_.sampleRate > 0.0 ? static_cast<double>(samples) / context_.sampleRate : 0.0});
}

void TakeFileSink::finish() {
    closePass();
}

juce::File TakeFileSink::nextFile() {
    for (auto index = nextIndex_;; ++index) {
        auto file = directory_.getChildFile(baseName_ + "_" + juce::String(index) +
                                            extensionFor(spec_.format));

        if (!file.exists()) {
            nextIndex_ = index + 1;
            return file;
        }
    }
}

}  // namespace magda::engine
