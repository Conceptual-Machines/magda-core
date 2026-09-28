#pragma once

/**
 * @file GrooveStore.hpp
 * @brief The groove library's persistence with no Tracktion engine behind it (#2761).
 */

#include <juce_core/juce_core.h>

#include <vector>

#include "../music/GrooveLibrary.hpp"

namespace magda {

/**
 * @brief The "GrooveTemplates" key of Tracktion's Settings.xml, kept as its
 * GrooveTemplateManager keeps it, so either engine reads the list the other wrote.
 *
 * Seeded as Tracktion seeds it: the shipped grooves when the file holds none, the
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
     * Canonicalised as Tracktion's updateTemplate does: the name trimmed, cut to 32
     * characters and given a " (N)" suffix past a clash, and the groove parameterized.
     */
    bool upsert(const GrooveTemplateData& groove);

  private:
    void save() const;

    juce::File settingsFile_;
    std::vector<GrooveTemplateData> grooves_;
};

}  // namespace magda
