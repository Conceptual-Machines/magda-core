#pragma once

/**
 * @file FourOscConversionPrompt.hpp
 * @brief Offering to convert a project's 4OSC devices (#2437).
 */

namespace magda::daw::ui {

/**
 * @brief Offer the conversion, if this project and this engine call for one.
 *
 * Offers a separate v1 copy for a project saved in v0, regardless of the old
 * engine-prompt preference. Otherwise offers the existing engine migration.
 * Accepting preserves the source and its media, and converts 4OSC to Poly Synth
 * when the MAGDA engine is rendering.
 *
 * Call after a project opens. Asynchronous: the alert answers on the message
 * thread and the caller has already returned.
 */
void offerFourOscConversion();

}  // namespace magda::daw::ui
