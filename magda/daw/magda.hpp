#pragma once

/**
 * @file magda.hpp
 * @brief Main header for the MAGDA - Multi Agent Digital Audio system
 *
 * MAGDA is a Multi-Agent Interface for Creative Audio.
 * It enables multiple intelligent agents to collaboratively compose, arrange, and
 * manipulate music in real time through a unified API and server-based communication model.
 */

#include "command.hpp"
#include "interfaces/clip_interface.hpp"
#include "interfaces/mixer_interface.hpp"
#include "interfaces/prompt_interface.hpp"
#include "interfaces/track_interface.hpp"
#include "interfaces/transport_interface.hpp"

// Forward declaration
namespace magda {
class TracktionEngineWrapper;
}

/**
 * @brief Current version of MAGDA
 */
constexpr const char* MAGDA_VERSION = "0.1.0";

/**
 * @brief Initialize MAGDA
 * @return true if initialization was successful
 */
bool magda_initialize();

/**
 * @brief Shutdown MAGDA
 */
void magda_shutdown();

/**
 * @brief Get access to the global Tracktion Engine instance
 * @return Pointer to the engine wrapper, or nullptr if not initialized
 */
magda::TracktionEngineWrapper* magda_get_engine();
