// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <magda/sdk/sequencer/TimeBend.hpp>
#include <optional>
#include <vector>

namespace magda {

/** Groove-template operations exposed without leaking the audio engine. */
class GrooveApi {
  public:
    virtual ~GrooveApi() = default;

    virtual bool upsertTemplate(const juce::String& name, int notesPerBeat, bool parameterized,
                                const std::vector<float>& latenessProportions) = 0;
    virtual juce::StringArray getTemplateNames() const = 0;
    /// The time bend the groove named @p name plays in the project's meter, at @p strength;
    /// nullopt for a name no groove has (#3044).
    virtual std::optional<sdk::sequencer::TimeBend> timeBend(const juce::String& name,
                                                             float strength) const = 0;
};

}  // namespace magda
