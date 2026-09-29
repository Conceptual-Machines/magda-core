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
 * @brief The "GrooveTemplates" key of the legacy Settings.xml, kept in the format v0 wrote
 * so existing groove lists still load.
 *
 * Seeded with the shipped grooves when the file holds none, the
 * parameterized set when the list has no parameterized groove, then the two basic swings.
 */
class GrooveStore {
  public:
    explicit GrooveStore(juce::File settingsFile);

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

    juce::File settingsFile_;
    std::vector<GrooveTemplateData> grooves_;
};

}  // namespace magda
