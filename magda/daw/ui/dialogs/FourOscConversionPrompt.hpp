#pragma once

/**
 * @file FourOscConversionPrompt.hpp
 * @brief Offering to convert a project's 4OSC devices (#2437).
 */

namespace magda::daw::ui {

/**
 * @brief Offer the conversion, if this project and this engine call for one.
 *
 * Does nothing unless the MAGDA engine is the one rendering, the project has
 * a 4OSC in it, and the prompt has not been switched off. Accepting writes
 * the project as it stands to a new file, converts, and saves.
 *
 * Call after a project opens. Asynchronous: the alert answers on the message
 * thread and the caller has already returned.
 */
void offerFourOscConversion();

}  // namespace magda::daw::ui
