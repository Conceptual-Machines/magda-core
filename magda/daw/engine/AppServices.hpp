#pragma once

/**
 * @file AppServices.hpp
 * @brief What the app brings up once, whichever engine renders (#2761).
 */

namespace magda::app_services {

/// Whether this run has no devices and no UI: asked for, or MAGDA_HEADLESS set.
bool isHeadless(bool asked);

/**
 * @brief Load the config and controller profiles, name device parameters, and hook
 *        project save and load.
 *
 * Runs once per process however many times it is called: the app calls it, and so does
 * each engine, so an engine a test builds on its own still has them.
 */
void bringUp();

}  // namespace magda::app_services
