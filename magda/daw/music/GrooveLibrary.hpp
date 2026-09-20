#pragma once

/**
 * @file GrooveLibrary.hpp
 * @brief The groove templates a project can quantize to (#2757).
 */

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

namespace magda {

/** @brief A groove as MAGDA states it, before any engine's own representation. */
struct GrooveTemplateData {
    juce::String name;
    int notesPerBeat = 2;
    bool parameterized = true;
    std::vector<float> latenessProportions;
};

/**
 * @brief Named grooves, read by the clip inspector and written by the groove API.
 *
 * Tracktion's GrooveTemplateManager owns the library its own quantize reads, so until the
 * fork goes (#2557) the engine registers the pair below rather than this owning a library.
 */
class GrooveLibrary {
  public:
    using Names = std::function<juce::StringArray()>;
    using Upsert = std::function<bool(const GrooveTemplateData&)>;

    static GrooveLibrary& getInstance();

    GrooveLibrary(const GrooveLibrary&) = delete;
    GrooveLibrary& operator=(const GrooveLibrary&) = delete;

    void setBackend(Names names, Upsert upsert) {
        names_ = std::move(names);
        upsert_ = std::move(upsert);
    }
    void forgetBackend() {
        names_ = nullptr;
        upsert_ = nullptr;
    }

    juce::StringArray names() const {
        return names_ ? names_() : juce::StringArray{};
    }

    /// False when nothing backs the library, or the groove names no lateness to apply.
    bool upsert(const GrooveTemplateData& groove) const {
        return upsert_ ? upsert_(groove) : false;
    }

  private:
    GrooveLibrary() = default;

    Names names_;
    Upsert upsert_;
};

}  // namespace magda
