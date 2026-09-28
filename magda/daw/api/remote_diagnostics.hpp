#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

#include "../core/TypeIds.hpp"

namespace magda {
class AudioEngine;
namespace remote {
class MeterSource;

/** Message-thread, engine-neutral source for bounded diagnostic reads. */
class DiagnosticsSource {
  public:
    virtual ~DiagnosticsSource() = default;
    virtual juce::var health() = 0;
    virtual juce::var meters(const std::vector<TrackId>& trackIds) = 0;
    virtual void projectReplaced() = 0;
};

std::unique_ptr<DiagnosticsSource> makeLiveDiagnosticsSource(AudioEngine& engine,
                                                             std::shared_ptr<MeterSource> meters);
}  // namespace remote
}  // namespace magda
