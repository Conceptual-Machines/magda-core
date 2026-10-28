// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>

namespace magda {

/**
 * @brief A component's vertical needs (#3119): below minimum it clips, at ideal it has the room
 * its design intends. Zero in both means no preference.
 */
struct HeightRange {
    int minimum = 0;
    int ideal = 0;

    bool isEmpty() const noexcept {
        return minimum <= 0 && ideal <= 0;
    }

    /** @brief The same needs measured from a container that is @p chrome taller. */
    HeightRange plus(int chrome) const noexcept {
        return isEmpty() ? HeightRange{} : HeightRange{minimum + chrome, ideal + chrome};
    }

    /** @brief The tighter of two needs, for containers that hold both side by side. */
    HeightRange unite(const HeightRange& other) const noexcept {
        return {std::max(minimum, other.minimum), std::max(ideal, other.ideal)};
    }
};

}  // namespace magda
