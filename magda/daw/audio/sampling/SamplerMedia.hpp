#pragma once

/**
 * @file SamplerMedia.hpp
 * @brief Every sample file a project's samplers play, and how to repoint one (#2757).
 */

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace magda {

/** @brief One sampler's file, with the call that repoints it when the file has moved. */
struct SamplerMediaReference {
    juce::File source;
    std::function<void(const juce::File&)> replace;
};

/**
 * @brief What a project collect or a missing-media pass walks. Message thread.
 *
 * The samplers live in whichever engine renders them, so that engine registers the walk
 * and this answers empty when none has.
 */
class SamplerMedia {
  public:
    using Provider = std::function<std::vector<SamplerMediaReference>()>;

    static SamplerMedia& getInstance();

    SamplerMedia(const SamplerMedia&) = delete;
    SamplerMedia& operator=(const SamplerMedia&) = delete;

    void setProvider(Provider provider) {
        provider_ = std::move(provider);
    }
    void forgetProvider() {
        provider_ = nullptr;
    }

    std::vector<SamplerMediaReference> references() const {
        return provider_ ? provider_() : std::vector<SamplerMediaReference>{};
    }

  private:
    SamplerMedia() = default;

    Provider provider_;
};

}  // namespace magda
