#pragma once

#include <magda/sdk/mod/ModAdsr.hpp>
#include <magda/sdk/mod/ModFollower.hpp>
#include <magda/sdk/mod/ModLfo.hpp>
#include <magda/sdk/mod/ModRandom.hpp>
#include <magda/sdk/mod/ModTypes.hpp>

#include "exec/RenderContext.hpp"

/**
 * @file ModBridge.hpp
 * @brief Where the engine's block meets the SDK's modulator cores (#2932).
 *
 * The cores live in magda-sdk and read a ModBlock; the tempo map is the
 * engine's, so it resolves the bar quantities here, once per block.
 */

namespace magda::engine {

using sdk::AdsrSettings;
using sdk::AdsrStage;
using sdk::AdsrState;
using sdk::FollowerSettings;
using sdk::FollowerState;
using sdk::LfoRate;
using sdk::LfoSettings;
using sdk::LfoState;
using sdk::ModKind;
using sdk::ModSync;
using sdk::ModTiming;
using sdk::RandomSettings;
using sdk::RandomShape;
using sdk::RandomState;

using sdk::adsrSegmentAt;
using sdk::adsrStageSeconds;
using sdk::advanceAdsr;
using sdk::advanceFollower;
using sdk::advanceLfo;
using sdk::advanceRandom;
using sdk::barFractionOf;
using sdk::cycleBeats;
using sdk::detectFollowerSource;
using sdk::laneValueFromRateType;
using sdk::lfoShapeAt;
using sdk::rateTypeFromLaneValue;
using sdk::restartAdsr;
using sdk::restartLfo;
using sdk::restartRandom;
using sdk::seedRandom;

/** @brief The tempo and signature @p block opens on, read off its map. */
ModTiming modTimingFor(const BlockInfo& block, double sampleRate);

/**
 * @brief Where @p block opens, in bars (bar-grid based, not beats / bar-length).
 *
 * Signature changes require this instead of simple division
 * (docs/architecture/modifier-lfo.md).
 */
double modBarPosition(const BlockInfo& block, const ModTiming& timing);

/**
 * @brief How many bars @p block covers, for a modifier advancing its own ramp.
 *
 * Handles blocks that span a tempo or signature change (#2340).
 */
double modBarsElapsed(const BlockInfo& block, const ModTiming& timing);

/** @brief @p block reduced to what the SDK cores read. */
sdk::ModBlock modBlockFor(const BlockInfo& block, const ModTiming& timing);

}  // namespace magda::engine
