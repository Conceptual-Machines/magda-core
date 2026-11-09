// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

namespace magda::remote {

/// The committed copy of the document, relative to the repository root.
inline constexpr const char* OPENRPC_DOCUMENT_PATH = "manual/docs/reference/magda-openrpc.json";

/**
 * @brief An OpenRPC 1.3 document for the WebSocket transport.
 *
 * Built from @p description, a `system.describe` result, so the two catalogues cannot disagree.
 */
juce::var openRpcDocument(const juce::var& description);

/** @brief The registry's document as committed: two-space indent, LF line ends, final newline. */
juce::String openRpcText();

}  // namespace magda::remote
