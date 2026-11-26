// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace magda {

/**
 * @brief Load ggml's CPU and GPU backends from the app's folder, once per process.
 *
 * Shared by llama.cpp and whisper.cpp, which run on the same ggml: loading twice would register
 * each backend twice. A no-op where ggml links its backends in (macOS).
 */
void loadGgmlBackends();

}  // namespace magda
