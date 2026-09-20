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
 * @brief The named grooves, owned here rather than by whichever engine renders.
 *
 * Both engines read this: the fork compiles each into a tracktion::GrooveTemplate, and the
 * native engine compiles the same entries into an engine::GrooveTemplateSet at publish.
 * Neither is asked for the list, which is what let a groove exist under one engine and not
 * the other (#2757).
 *
 * The templates still persist through Tracktion's property storage, which is where the
 * shipped defaults are seeded from, so the fork imports them in at startup and is written
 * back to on every upsert. That half goes with the fork in #2557; the list does not.
 */
class GrooveLibrary {
  public:
    /// Told to the fork so its manager stays the persistence behind the list.
    using Writer = std::function<bool(const GrooveTemplateData&)>;

    static GrooveLibrary& getInstance();

    GrooveLibrary(const GrooveLibrary&) = delete;
    GrooveLibrary& operator=(const GrooveLibrary&) = delete;

    /** @brief Take @p grooves as the library, replacing whatever was there. */
    void import(std::vector<GrooveTemplateData> grooves);

    void setWriter(Writer writer) {
        writer_ = std::move(writer);
    }

    /// The engine is going away; the list it imported stays.
    void forgetWriter() {
        writer_ = nullptr;
    }

    juce::StringArray names() const;

    /** @brief Every groove, for an engine compiling them into its own representation. */
    const std::vector<GrooveTemplateData>& all() const {
        return grooves_;
    }

    const GrooveTemplateData* find(const juce::String& name) const;

    /**
     * @brief Add @p groove, or replace the one of that name. False when it names nothing
     * or carries no lateness to apply.
     */
    bool upsert(const GrooveTemplateData& groove);

  private:
    GrooveLibrary() = default;

    std::vector<GrooveTemplateData> grooves_;
    Writer writer_;
};

}  // namespace magda
