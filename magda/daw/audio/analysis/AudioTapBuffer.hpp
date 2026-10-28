// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <magda/sdk/tap/SampleRing.hpp>

namespace magda::daw::audio {

/// The audio-to-UI history ring the analysis devices feed: the SDK's.
using AudioTapBuffer = engine::SampleRing;

}  // namespace magda::daw::audio
