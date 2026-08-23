#pragma once

#include <sstream>
#include <string>

/**
 * @file DumpFormat.hpp
 * @brief Column formatting shared by the canonical dumps.
 *
 * Internal to the dumps. Three of them print the same fixed columns, and their
 * output is compared against checked-in goldens (#2076), so a column that
 * drifted in one and not the others would rewrite every golden and read as a
 * change to the thing being dumped.
 */

namespace magda::engine::dump_format {

/// Left-aligned in a fixed column.
inline std::string padded(std::string text, std::size_t width) {
    if (text.size() < width)
        text.append(width - text.size(), ' ');
    return text;
}

/// Right-aligned in a fixed column, for indices.
inline std::string rightAligned(int value, std::size_t width) {
    auto text = std::to_string(value);
    if (text.size() < width)
        text.insert(text.begin(), static_cast<long>(width - text.size()), ' ');
    return text;
}

/// Write one line, without the trailing spaces a padded last column leaves.
///
/// Trailing whitespace would not survive the repo's pre-commit hook, which
/// would rewrite every golden the first time one was committed and leave every
/// run afterwards comparing against a file it did not write.
inline void writeLine(std::ostringstream& out, std::string line) {
    while (!line.empty() && line.back() == ' ')
        line.pop_back();
    out << line << "\n";
}

}  // namespace magda::engine::dump_format
