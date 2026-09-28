#pragma once

#include <functional>

/**
 * @file MagdaEnginePrompt.hpp
 * @brief Offering the MAGDA engine on first launch.
 */

namespace magda::daw::ui {

/**
 * @brief Ask once whether to use the MAGDA engine, then call @p then.
 *
 * Runs before the engine is built, so a yes takes effect on this launch. Calls
 * @p then straight away when there is nothing to ask: already asked, already
 * on the MAGDA engine, MAGDA_AUDIO_ENGINE set, or no native engine built.
 */
void offerMagdaEngineOnFirstLaunch(std::function<void()> then);

}  // namespace magda::daw::ui
