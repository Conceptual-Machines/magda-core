// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VoiceControl.hpp"

#include <utility>

namespace magda::voice {

namespace {

std::function<void(Action)>& handler() {
    static std::function<void(Action)> instance;
    return instance;
}

}  // namespace

void setHandler(std::function<void(Action)> newHandler) {
    handler() = std::move(newHandler);
}

void trigger(Action action) {
    if (const auto& current = handler())
        current(action);
}

}  // namespace magda::voice
