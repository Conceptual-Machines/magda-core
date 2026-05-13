#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <string>

#include "../../core/ClipTypes.hpp"

namespace magda {

class ClipEngineIdMap {
  public:
    using Snapshot = std::map<ClipId, std::string>;

    bool contains(ClipId clipId) const;
    std::optional<std::string> getEngineId(ClipId clipId) const;
    std::optional<ClipId> getClipId(const std::string& engineId) const;

    void set(ClipId clipId, std::string engineId);
    void erase(ClipId clipId);
    Snapshot snapshot() const;

  private:
    mutable juce::CriticalSection lock_;
    std::map<ClipId, std::string> clipIdToEngineId_;
    std::map<std::string, ClipId> engineIdToClipId_;
};

}  // namespace magda
