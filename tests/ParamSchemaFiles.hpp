// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

/**
 * @brief The frozen parameter schema files (#1887): core's, then each extension's.
 *
 * An extension's devices are frozen in its own repository, so core's file never names them.
 */
inline juce::StringArray paramSchemaLines() {
    juce::StringArray files;
    files.add(MAGDA_DEVICE_PARAM_SCHEMA_FILE);
    files.addTokens(MAGDA_EXTENSION_PARAM_SCHEMA_FILES, "|", "");
    files.removeEmptyStrings();

    juce::StringArray lines;
    for (const auto& path : files)
        lines.addLines(juce::File(path).loadFileAsString());
    return lines;
}
