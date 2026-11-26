// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ggml_backends.hpp"

#include <ggml-backend.h>

#include <mutex>

namespace magda {

void loadGgmlBackends() {
#ifdef GGML_BACKEND_DL
    static std::once_flag loaded;
    std::call_once(loaded, [] { ggml_backend_load_all(); });
#endif
}

}  // namespace magda
