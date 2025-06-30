#pragma once

/**
 * @file magda.hpp
 * @brief Main header for the Magda Multi-Agent Generic DAW API
 * 
 * Magda is an experimental framework for building AI-driven Digital Audio Workstations.
 * It enables multiple intelligent agents to collaboratively compose, arrange, and 
 * manipulate music in real time through a unified API and server-based communication model.
 */

#include "magda/core/mcp_server.hpp"
#include "magda/core/interfaces/transport_interface.hpp"
#include "magda/core/interfaces/track_interface.hpp"
#include "magda/core/interfaces/clip_interface.hpp"
#include "magda/core/interfaces/mixer_interface.hpp"
#include "magda/core/interfaces/prompt_interface.hpp"
#include "magda/core/agent.hpp"
#include "magda/core/command.hpp"

/**
 * @brief Current version of Magda
 */
constexpr const char* MAGDA_VERSION = "0.1.0";

/**
 * @brief Initialize the Magda system
 * @return true if initialization was successful
 */
bool magda_initialize();

/**
 * @brief Shutdown the Magda system
 */
void magda_shutdown(); 