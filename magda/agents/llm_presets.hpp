#pragma once

#include <map>
#include <string>
#include <vector>

#include "../daw/core/Config.hpp"

namespace magda {

struct LLMPreset {
    std::string id;
    std::string displayName;
    std::map<std::string, Config::AgentLLMConfig> agents;
};

/** Built-in preset definitions. */
const std::vector<LLMPreset>& getBuiltInPresets();

/** Find a preset by id. Returns nullptr if not found. */
const LLMPreset* findPreset(const std::string& id);

}  // namespace magda
