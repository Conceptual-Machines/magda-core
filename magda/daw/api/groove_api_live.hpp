#pragma once

#include "groove_api.hpp"

namespace magda {

class GrooveApiLive final : public GrooveApi {
  public:
    bool upsertTemplate(const juce::String& name, int notesPerBeat, bool parameterized,
                        const std::vector<float>& latenessProportions) override;
    juce::StringArray getTemplateNames() const override;
    std::optional<sdk::sequencer::TimeBend> timeBend(const juce::String& name,
                                                     float strength) const override;
};

}  // namespace magda
