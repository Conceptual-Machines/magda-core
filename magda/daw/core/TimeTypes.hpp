// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace magda {

struct BeatPosition {
    double value = 0.0;
};

struct BeatDuration {
    double value = 0.0;
};

struct BeatRange {
    BeatPosition start;
    BeatPosition end;

    bool isValid() const {
        return end.value > start.value;
    }
};

}  // namespace magda
