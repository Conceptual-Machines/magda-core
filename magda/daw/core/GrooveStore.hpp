#pragma once

/**
 * @file GrooveStore.hpp
 * @brief The groove library's persistence (#2761).
 */

#include <juce_core/juce_core.h>

#include <vector>

#include "../music/GrooveLibrary.hpp"

namespace magda {

/**
 * @brief The groove list, kept in MAGDA's own file as a GROOVETEMPLATES element.
 *
 * With no file yet, the list is imported once from the "GrooveTemplates" key of the legacy
 * Settings.xml and saved. A list that is empty is seeded with the shipped grooves, the
 * parameterized set when it has no parameterized groove, then the two basic swings.
 */
class GrooveStore {
  public:
    GrooveStore(juce::File grooveFile, const juce::File& legacySettingsFile = {});

    const std::vector<GrooveTemplateData>& grooves() const {
        return grooves_;
    }

    /**
     * @brief Replace the groove named @p groove, or add it, and save the list.
     *
     * Canonicalised: the name trimmed, cut to 32
     * characters and given a " (N)" suffix past a clash, and the groove parameterized.
     */
    bool upsert(const GrooveTemplateData& groove);

  private:
    void save() const;

    juce::File grooveFile_;
    std::vector<GrooveTemplateData> grooves_;
};

}  // namespace magda
