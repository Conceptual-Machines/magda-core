#pragma once

#include <functional>

/**
 * @file MagdaEnginePrompt.hpp
 * @brief Offering the MAGDA engine at launch.
 */

namespace magda::daw::ui {

/**
 * @brief Ask whether to use the MAGDA engine, then call @p then.
 *
 * Runs before the engine is built, so a yes takes effect on this launch. Calls
 * @p then straight away when there is nothing to ask: "Don't show again" was
 * ticked, already on the MAGDA engine, MAGDA_AUDIO_ENGINE set, or no native
 * engine built.
 */
void offerMagdaEngineAtLaunch(std::function<void()> then);

}  // namespace magda::daw::ui
