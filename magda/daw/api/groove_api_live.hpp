#pragma once

#include "groove_api.hpp"

namespace magda {

class GrooveApiLive final : public GrooveApi {
  public:
    bool upsertTemplate(const juce::String& name, int notesPerBeat, bool parameterized,
                        const std::vector<float>& latenessProportions) override;
    juce::StringArray getTemplateNames() const override;
};

}  // namespace magda
