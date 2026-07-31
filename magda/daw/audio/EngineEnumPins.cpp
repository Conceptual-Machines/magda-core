#include <tracktion_engine/tracktion_engine.h>

#include "core/ClipInfo.hpp"
#include "core/ClipTypes.hpp"
#include "core/ModInfo.hpp"
#include "core/TimeStretchModes.hpp"

/**
 * Compile-time pins between MAGDA's persisted enums and the engine enums they
 * currently equal (#1887).
 *
 * MAGDA owns the numbers written into project files. Several of them were
 * originally chosen to match Tracktion's enums so the bridge could be a plain
 * cast, and the bridges still are casts - which is only safe while the two
 * agree. These asserts are that agreement, stated once.
 *
 * If a Tracktion bump breaks one of these, the fix is NEVER to renumber the
 * MAGDA side: every existing project file is keyed on it. Replace the cast at
 * the bridge with an explicit mapping and delete the corresponding assert.
 *
 * MAGDA-owned enums with no engine counterpart (LaunchQuantize, FollowAction)
 * are documented alongside these in docs/architecture/persisted-enums.md but
 * need no assert - nothing external can move them.
 */

namespace magda {
namespace {

namespace te = tracktion::engine;

// Time-stretch mode (ClipInfo::timeStretchMode) -> te::TimeStretcher::Mode
static_assert(time_stretch_mode::kDisabled == te::TimeStretcher::disabled);
static_assert(time_stretch_mode::kSoundTouchNormal == te::TimeStretcher::soundtouchNormal);
static_assert(time_stretch_mode::kSoundTouchBetter == te::TimeStretcher::soundtouchBetter);
static_assert(time_stretch_mode::kSignalsmith == te::TimeStretcher::signalsmith);

// Fade curve (ClipInfo::fadeInType / fadeOutType) -> tracktion::AudioFadeCurve::Type
static_assert(static_cast<int>(FadeCurve::Linear) == tracktion::AudioFadeCurve::linear);
static_assert(static_cast<int>(FadeCurve::Convex) == tracktion::AudioFadeCurve::convex);
static_assert(static_cast<int>(FadeCurve::Concave) == tracktion::AudioFadeCurve::concave);
static_assert(static_cast<int>(FadeCurve::SCurve) == tracktion::AudioFadeCurve::sCurve);

// Modifier rate type (tempo-synced Rate parameter) -> te::ModifierCommon::RateType
static_assert(static_cast<int>(ModRateType::Hertz) == te::ModifierCommon::hertz);
static_assert(static_cast<int>(ModRateType::SixteenBars) == te::ModifierCommon::sixteenBars);
static_assert(static_cast<int>(ModRateType::EightBars) == te::ModifierCommon::eightBars);
static_assert(static_cast<int>(ModRateType::FourBars) == te::ModifierCommon::fourBars);
static_assert(static_cast<int>(ModRateType::TwoBars) == te::ModifierCommon::twoBars);
static_assert(static_cast<int>(ModRateType::Bar) == te::ModifierCommon::bar);
static_assert(static_cast<int>(ModRateType::DottedHalf) == te::ModifierCommon::halfD);
static_assert(static_cast<int>(ModRateType::Half) == te::ModifierCommon::half);
static_assert(static_cast<int>(ModRateType::TripletHalf) == te::ModifierCommon::halfT);
static_assert(static_cast<int>(ModRateType::DottedQuarter) == te::ModifierCommon::quarterD);
static_assert(static_cast<int>(ModRateType::Quarter) == te::ModifierCommon::quarter);
static_assert(static_cast<int>(ModRateType::TripletQuarter) == te::ModifierCommon::quarterT);
static_assert(static_cast<int>(ModRateType::DottedEighth) == te::ModifierCommon::eighthD);
static_assert(static_cast<int>(ModRateType::Eighth) == te::ModifierCommon::eighth);
static_assert(static_cast<int>(ModRateType::TripletEighth) == te::ModifierCommon::eighthT);
static_assert(static_cast<int>(ModRateType::DottedSixteenth) == te::ModifierCommon::sixteenthD);
static_assert(static_cast<int>(ModRateType::Sixteenth) == te::ModifierCommon::sixteenth);
static_assert(static_cast<int>(ModRateType::TripletSixteenth) == te::ModifierCommon::sixteenthT);
static_assert(static_cast<int>(ModRateType::DottedThirtySecond) ==
              te::ModifierCommon::thirtySecondD);
static_assert(static_cast<int>(ModRateType::ThirtySecond) == te::ModifierCommon::thirtySecond);
static_assert(static_cast<int>(ModRateType::TripletThirtySecond) ==
              te::ModifierCommon::thirtySecondT);
static_assert(static_cast<int>(ModRateType::DottedSixtyFourth) == te::ModifierCommon::sixtyFourthD);
static_assert(static_cast<int>(ModRateType::SixtyFourth) == te::ModifierCommon::sixtyFourth);
static_assert(static_cast<int>(ModRateType::TripletSixtyFourth) ==
              te::ModifierCommon::sixtyFourthT);

}  // namespace
}  // namespace magda
