#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/AutomationTypes.hpp"
#include "magda/daw/core/ChainNodePath.hpp"
#include "magda/daw/core/ClipInfo.hpp"
#include "magda/daw/core/ClipTypes.hpp"
#include "magda/daw/core/ControlTarget.hpp"
#include "magda/daw/core/DeviceInfo.hpp"
#include "magda/daw/core/ModInfo.hpp"
#include "magda/daw/core/ParameterInfo.hpp"
#include "magda/daw/core/TimeStretchModes.hpp"
#include "magda/daw/core/TrackInfo.hpp"
#include "magda/daw/core/TrackTypes.hpp"

// -----------------------------------------------------------------------------
// Pinned persisted enum ordinals (#2079)
//
// Every enum below is written into a .mgd project or a .mps preset as its
// integer value. An INSERTED enumerator renumbers everything after it, and a
// file saved before the insert then loads as a different thing: a linear fade
// becomes convex, a 1/4 LFO becomes 1/8, an Aux track becomes a Master. Nothing
// complains, because the integer is still in range.
//
// audio/EngineEnumPins.cpp pins the subset that mirrors a Tracktion enum, in the
// other direction: it holds MAGDA's numbers equal to the engine's so the bridges
// can stay casts. That says nothing about an insert on MAGDA's side, which is
// what this file is for. Appending stays legal; every other edit fails here.
//
// The static_asserts are the gate - they fail the build, so the value cannot
// reach a user's project. The test case re-states them at runtime so a failure
// report names the enum rather than only a file and line.
// -----------------------------------------------------------------------------

namespace magda {
namespace {

// --- Clips -------------------------------------------------------------------

static_assert(static_cast<int>(ClipType::Audio) == 0);
static_assert(static_cast<int>(ClipType::MIDI) == 1);

static_assert(static_cast<int>(ClipView::Arrangement) == 0);
static_assert(static_cast<int>(ClipView::Session) == 1);

static_assert(static_cast<int>(LaunchMode::Trigger) == 0);
static_assert(static_cast<int>(LaunchMode::Toggle) == 1);

static_assert(static_cast<int>(LaunchQuantize::None) == 0);
static_assert(static_cast<int>(LaunchQuantize::EightBars) == 1);
static_assert(static_cast<int>(LaunchQuantize::FourBars) == 2);
static_assert(static_cast<int>(LaunchQuantize::TwoBars) == 3);
static_assert(static_cast<int>(LaunchQuantize::OneBar) == 4);
static_assert(static_cast<int>(LaunchQuantize::HalfBar) == 5);
static_assert(static_cast<int>(LaunchQuantize::QuarterBar) == 6);
static_assert(static_cast<int>(LaunchQuantize::EighthBar) == 7);
static_assert(static_cast<int>(LaunchQuantize::SixteenthBar) == 8);

static_assert(static_cast<int>(FollowAction::None) == 0);
static_assert(static_cast<int>(FollowAction::PlayNext) == 1);
static_assert(static_cast<int>(FollowAction::PlayPrevious) == 2);
static_assert(static_cast<int>(FollowAction::PlayRandom) == 3);
static_assert(static_cast<int>(FollowAction::Stop) == 4);
static_assert(static_cast<int>(FollowAction::PlayAgain) == 5);

static_assert(static_cast<int>(FadeCurve::Linear) == 1);
static_assert(static_cast<int>(FadeCurve::Convex) == 2);
static_assert(static_cast<int>(FadeCurve::Concave) == 3);
static_assert(static_cast<int>(FadeCurve::SCurve) == 4);

static_assert(static_cast<int>(MidiCurveType::Step) == 0);
static_assert(static_cast<int>(MidiCurveType::Linear) == 1);
static_assert(static_cast<int>(MidiCurveType::Bezier) == 2);

// Not an enum, but the same contract: sparse on purpose, because the values are
// the engine's stretcher modes and the gaps are modes MAGDA does not offer.
static_assert(time_stretch_mode::kDisabled == 0);
static_assert(time_stretch_mode::kSoundTouchNormal == 3);
static_assert(time_stretch_mode::kSoundTouchBetter == 4);
static_assert(time_stretch_mode::kSignalsmith == 15);

// --- Tracks ------------------------------------------------------------------

static_assert(static_cast<int>(TrackType::Media) == 0);
static_assert(static_cast<int>(TrackType::Group) == 3);
static_assert(static_cast<int>(TrackType::Aux) == 4);
static_assert(static_cast<int>(TrackType::Master) == 5);
static_assert(static_cast<int>(TrackType::MultiOut) == 6);
static_assert(static_cast<int>(TrackType::Chord) == 7);

static_assert(static_cast<int>(InputMonitorMode::Off) == 0);
static_assert(static_cast<int>(InputMonitorMode::In) == 1);
static_assert(static_cast<int>(InputMonitorMode::Auto) == 2);

static_assert(static_cast<int>(TrackPlaybackMode::Arrangement) == 0);
static_assert(static_cast<int>(TrackPlaybackMode::Session) == 1);

// --- Devices and parameters --------------------------------------------------

static_assert(static_cast<int>(PluginFormat::VST3) == 0);
static_assert(static_cast<int>(PluginFormat::AU) == 1);
static_assert(static_cast<int>(PluginFormat::LV2) == 2);
static_assert(static_cast<int>(PluginFormat::Internal) == 3);

static_assert(static_cast<int>(DeviceType::Instrument) == 0);
static_assert(static_cast<int>(DeviceType::Effect) == 1);
static_assert(static_cast<int>(DeviceType::MIDI) == 2);
static_assert(static_cast<int>(DeviceType::Analysis) == 3);

static_assert(static_cast<int>(SidechainConfig::Type::None) == 0);
static_assert(static_cast<int>(SidechainConfig::Type::Audio) == 1);
static_assert(static_cast<int>(SidechainConfig::Type::MIDI) == 2);

static_assert(static_cast<int>(ParameterScale::Linear) == 0);
static_assert(static_cast<int>(ParameterScale::Logarithmic) == 1);
static_assert(static_cast<int>(ParameterScale::Exponential) == 2);
static_assert(static_cast<int>(ParameterScale::Discrete) == 3);
static_assert(static_cast<int>(ParameterScale::Boolean) == 4);
static_assert(static_cast<int>(ParameterScale::FaderDB) == 5);

static_assert(static_cast<int>(DisplayFormat::Default) == 0);
static_assert(static_cast<int>(DisplayFormat::Decibels) == 1);
static_assert(static_cast<int>(DisplayFormat::Pan) == 2);
static_assert(static_cast<int>(DisplayFormat::Percent) == 3);
static_assert(static_cast<int>(DisplayFormat::MidiNote) == 4);
static_assert(static_cast<int>(DisplayFormat::Beats) == 5);
static_assert(static_cast<int>(DisplayFormat::BarsBeats) == 6);

// --- Control targets ---------------------------------------------------------

static_assert(static_cast<int>(ControlTarget::Kind::PluginParam) == 0);
static_assert(static_cast<int>(ControlTarget::Kind::DeviceMacro) == 1);
static_assert(static_cast<int>(ControlTarget::Kind::ModParam) == 2);
static_assert(static_cast<int>(ControlTarget::Kind::TrackVolume) == 3);
static_assert(static_cast<int>(ControlTarget::Kind::TrackPan) == 4);
static_assert(static_cast<int>(ControlTarget::Kind::SendLevel) == 5);
static_assert(static_cast<int>(ControlTarget::Kind::Tempo) == 6);

static_assert(static_cast<int>(ChainStepType::Rack) == 0);
static_assert(static_cast<int>(ChainStepType::Chain) == 1);
static_assert(static_cast<int>(ChainStepType::Device) == 2);
static_assert(static_cast<int>(ChainStepType::Segment) == 3);

// A Segment step carries the section in `ChainPathStep::id`, and
// AutomationModSerializer writes that integer straight out. Pinning the step
// TYPE without pinning the section would leave an insert here free to redirect
// every saved post-FX and mixer-analysis path onto a different section.
static_assert(static_cast<int>(ChainSegment::Fx) == 0);
static_assert(static_cast<int>(ChainSegment::PostFx) == 1);
static_assert(static_cast<int>(ChainSegment::MixerAnalysis) == 2);

// --- Automation --------------------------------------------------------------

static_assert(static_cast<int>(AutomationLaneType::Absolute) == 0);
static_assert(static_cast<int>(AutomationLaneType::ClipBased) == 1);

static_assert(static_cast<int>(AutomationCurveType::Linear) == 0);
static_assert(static_cast<int>(AutomationCurveType::Bezier) == 1);
static_assert(static_cast<int>(AutomationCurveType::Step) == 2);
static_assert(static_cast<int>(AutomationCurveType::HardCorner) == 3);

// --- Modifiers ---------------------------------------------------------------

static_assert(static_cast<int>(ModType::LFO) == 0);
static_assert(static_cast<int>(ModType::Envelope) == 1);
static_assert(static_cast<int>(ModType::Random) == 2);
static_assert(static_cast<int>(ModType::Follower) == 3);

static_assert(static_cast<int>(LFOWaveform::Sine) == 0);
static_assert(static_cast<int>(LFOWaveform::Triangle) == 1);
static_assert(static_cast<int>(LFOWaveform::Square) == 2);
static_assert(static_cast<int>(LFOWaveform::Saw) == 3);
static_assert(static_cast<int>(LFOWaveform::ReverseSaw) == 4);
static_assert(static_cast<int>(LFOWaveform::Custom) == 5);

// Where a modifier listens in its source's chain (#2120). Written as an
// integer, so an insert would move every modifier in every saved project to
// the other point: a follower would start keying off the head of the chain.
static_assert(static_cast<int>(ModTapPoint::PreFx) == 0);
static_assert(static_cast<int>(ModTapPoint::PostFader) == 1);

static_assert(static_cast<int>(LFOTriggerMode::Free) == 0);
static_assert(static_cast<int>(LFOTriggerMode::Transport) == 1);
static_assert(static_cast<int>(LFOTriggerMode::MIDI) == 2);
static_assert(static_cast<int>(LFOTriggerMode::Audio) == 3);

static_assert(static_cast<int>(CurvePreset::Triangle) == 0);
static_assert(static_cast<int>(CurvePreset::Sine) == 1);
static_assert(static_cast<int>(CurvePreset::RampUp) == 2);
static_assert(static_cast<int>(CurvePreset::RampDown) == 3);
static_assert(static_cast<int>(CurvePreset::SCurve) == 4);
static_assert(static_cast<int>(CurvePreset::Exponential) == 5);
static_assert(static_cast<int>(CurvePreset::Logarithmic) == 6);
static_assert(static_cast<int>(CurvePreset::Custom) == 7);

// Musical divisions, whose integers are the division itself rather than a
// position in a list. Reordering the declarations is harmless; changing one of
// these numbers is not.
static_assert(static_cast<int>(SyncDivision::Whole) == 1);
static_assert(static_cast<int>(SyncDivision::Half) == 2);
static_assert(static_cast<int>(SyncDivision::Quarter) == 4);
static_assert(static_cast<int>(SyncDivision::Eighth) == 8);
static_assert(static_cast<int>(SyncDivision::Sixteenth) == 16);
static_assert(static_cast<int>(SyncDivision::ThirtySecond) == 32);
static_assert(static_cast<int>(SyncDivision::DottedHalf) == 3);
static_assert(static_cast<int>(SyncDivision::DottedQuarter) == 6);
static_assert(static_cast<int>(SyncDivision::DottedEighth) == 12);
static_assert(static_cast<int>(SyncDivision::DottedSixteenth) == 24);
static_assert(static_cast<int>(SyncDivision::DottedThirtySecond) == 48);
static_assert(static_cast<int>(SyncDivision::TripletHalf) == 33);
static_assert(static_cast<int>(SyncDivision::TripletQuarter) == 66);
static_assert(static_cast<int>(SyncDivision::TripletEighth) == 132);
static_assert(static_cast<int>(SyncDivision::TripletSixteenth) == 264);
static_assert(static_cast<int>(SyncDivision::TripletThirtySecond) == 528);
static_assert(static_cast<int>(SyncDivision::TwoBars) == 200);
static_assert(static_cast<int>(SyncDivision::FourBars) == 400);
static_assert(static_cast<int>(SyncDivision::EightBars) == 800);
static_assert(static_cast<int>(SyncDivision::SixteenBars) == 1600);

static_assert(static_cast<int>(ModRateType::Hertz) == 0);
static_assert(static_cast<int>(ModRateType::SixteenBars) == 1);
static_assert(static_cast<int>(ModRateType::EightBars) == 2);
static_assert(static_cast<int>(ModRateType::FourBars) == 3);
static_assert(static_cast<int>(ModRateType::TwoBars) == 4);
static_assert(static_cast<int>(ModRateType::Bar) == 5);
static_assert(static_cast<int>(ModRateType::DottedHalf) == 6);
static_assert(static_cast<int>(ModRateType::Half) == 7);
static_assert(static_cast<int>(ModRateType::TripletHalf) == 8);
static_assert(static_cast<int>(ModRateType::DottedQuarter) == 9);
static_assert(static_cast<int>(ModRateType::Quarter) == 10);
static_assert(static_cast<int>(ModRateType::TripletQuarter) == 11);
static_assert(static_cast<int>(ModRateType::DottedEighth) == 12);
static_assert(static_cast<int>(ModRateType::Eighth) == 13);
static_assert(static_cast<int>(ModRateType::TripletEighth) == 14);
static_assert(static_cast<int>(ModRateType::DottedSixteenth) == 15);
static_assert(static_cast<int>(ModRateType::Sixteenth) == 16);
static_assert(static_cast<int>(ModRateType::TripletSixteenth) == 17);
static_assert(static_cast<int>(ModRateType::DottedThirtySecond) == 18);
static_assert(static_cast<int>(ModRateType::ThirtySecond) == 19);
static_assert(static_cast<int>(ModRateType::TripletThirtySecond) == 20);
static_assert(static_cast<int>(ModRateType::DottedSixtyFourth) == 21);
static_assert(static_cast<int>(ModRateType::SixtyFourth) == 22);
static_assert(static_cast<int>(ModRateType::TripletSixtyFourth) == 23);

struct Pin {
    const char* enumName;
    const char* enumeratorName;
    int expected;
    int actual;
};

#define PIN(EnumType, Enumerator, Value)                                                           \
    Pin {                                                                                          \
        #EnumType, #Enumerator, Value, static_cast<int>(EnumType::Enumerator)                      \
    }

/// The same pins as values, so a mismatch reports the enumerator by name. A
/// build with an inserted value never gets here - the static_asserts above fail
/// first - but a value edited to a wrong-but-still-compiling number does.
std::vector<Pin> allPins() {
    return {
        PIN(ClipType, Audio, 0),
        PIN(ClipType, MIDI, 1),
        PIN(ClipView, Arrangement, 0),
        PIN(ClipView, Session, 1),
        PIN(LaunchMode, Trigger, 0),
        PIN(LaunchMode, Toggle, 1),
        PIN(LaunchQuantize, None, 0),
        PIN(LaunchQuantize, EightBars, 1),
        PIN(LaunchQuantize, FourBars, 2),
        PIN(LaunchQuantize, TwoBars, 3),
        PIN(LaunchQuantize, OneBar, 4),
        PIN(LaunchQuantize, HalfBar, 5),
        PIN(LaunchQuantize, QuarterBar, 6),
        PIN(LaunchQuantize, EighthBar, 7),
        PIN(LaunchQuantize, SixteenthBar, 8),
        PIN(FollowAction, None, 0),
        PIN(FollowAction, PlayNext, 1),
        PIN(FollowAction, PlayPrevious, 2),
        PIN(FollowAction, PlayRandom, 3),
        PIN(FollowAction, Stop, 4),
        PIN(FollowAction, PlayAgain, 5),
        PIN(FadeCurve, Linear, 1),
        PIN(FadeCurve, Convex, 2),
        PIN(FadeCurve, Concave, 3),
        PIN(FadeCurve, SCurve, 4),
        PIN(MidiCurveType, Step, 0),
        PIN(MidiCurveType, Linear, 1),
        PIN(MidiCurveType, Bezier, 2),
        PIN(TrackType, Media, 0),
        PIN(TrackType, Group, 3),
        PIN(TrackType, Aux, 4),
        PIN(TrackType, Master, 5),
        PIN(TrackType, MultiOut, 6),
        PIN(TrackType, Chord, 7),
        PIN(InputMonitorMode, Off, 0),
        PIN(InputMonitorMode, In, 1),
        PIN(InputMonitorMode, Auto, 2),
        PIN(TrackPlaybackMode, Arrangement, 0),
        PIN(TrackPlaybackMode, Session, 1),
        PIN(PluginFormat, VST3, 0),
        PIN(PluginFormat, AU, 1),
        PIN(PluginFormat, LV2, 2),
        PIN(PluginFormat, Internal, 3),
        PIN(DeviceType, Instrument, 0),
        PIN(DeviceType, Effect, 1),
        PIN(DeviceType, MIDI, 2),
        PIN(DeviceType, Analysis, 3),
        PIN(SidechainConfig::Type, None, 0),
        PIN(SidechainConfig::Type, Audio, 1),
        PIN(SidechainConfig::Type, MIDI, 2),
        PIN(ParameterScale, Linear, 0),
        PIN(ParameterScale, Logarithmic, 1),
        PIN(ParameterScale, Exponential, 2),
        PIN(ParameterScale, Discrete, 3),
        PIN(ParameterScale, Boolean, 4),
        PIN(ParameterScale, FaderDB, 5),
        PIN(DisplayFormat, Default, 0),
        PIN(DisplayFormat, Decibels, 1),
        PIN(DisplayFormat, Pan, 2),
        PIN(DisplayFormat, Percent, 3),
        PIN(DisplayFormat, MidiNote, 4),
        PIN(DisplayFormat, Beats, 5),
        PIN(DisplayFormat, BarsBeats, 6),
        PIN(ControlTarget::Kind, PluginParam, 0),
        PIN(ControlTarget::Kind, DeviceMacro, 1),
        PIN(ControlTarget::Kind, ModParam, 2),
        PIN(ControlTarget::Kind, TrackVolume, 3),
        PIN(ControlTarget::Kind, TrackPan, 4),
        PIN(ControlTarget::Kind, SendLevel, 5),
        PIN(ControlTarget::Kind, Tempo, 6),
        PIN(ChainStepType, Rack, 0),
        PIN(ChainStepType, Chain, 1),
        PIN(ChainStepType, Device, 2),
        PIN(ChainStepType, Segment, 3),
        PIN(ChainSegment, Fx, 0),
        PIN(ChainSegment, PostFx, 1),
        PIN(ChainSegment, MixerAnalysis, 2),
        PIN(AutomationLaneType, Absolute, 0),
        PIN(AutomationLaneType, ClipBased, 1),
        PIN(AutomationCurveType, Linear, 0),
        PIN(AutomationCurveType, Bezier, 1),
        PIN(AutomationCurveType, Step, 2),
        PIN(AutomationCurveType, HardCorner, 3),
        PIN(ModType, LFO, 0),
        PIN(ModType, Envelope, 1),
        PIN(ModType, Random, 2),
        PIN(ModType, Follower, 3),
        PIN(ModTapPoint, PreFx, 0),
        PIN(ModTapPoint, PostFader, 1),
        PIN(LFOWaveform, Sine, 0),
        PIN(LFOWaveform, Triangle, 1),
        PIN(LFOWaveform, Square, 2),
        PIN(LFOWaveform, Saw, 3),
        PIN(LFOWaveform, ReverseSaw, 4),
        PIN(LFOWaveform, Custom, 5),
        PIN(LFOTriggerMode, Free, 0),
        PIN(LFOTriggerMode, Transport, 1),
        PIN(LFOTriggerMode, MIDI, 2),
        PIN(LFOTriggerMode, Audio, 3),
        PIN(CurvePreset, Triangle, 0),
        PIN(CurvePreset, Sine, 1),
        PIN(CurvePreset, RampUp, 2),
        PIN(CurvePreset, RampDown, 3),
        PIN(CurvePreset, SCurve, 4),
        PIN(CurvePreset, Exponential, 5),
        PIN(CurvePreset, Logarithmic, 6),
        PIN(CurvePreset, Custom, 7),
        PIN(ModRateType, Hertz, 0),
        PIN(ModRateType, Bar, 5),
        PIN(ModRateType, Quarter, 10),
        PIN(ModRateType, Eighth, 13),
        PIN(ModRateType, Sixteenth, 16),
        PIN(ModRateType, TripletSixtyFourth, 23),
        PIN(SyncDivision, Whole, 1),
        PIN(SyncDivision, Quarter, 4),
        PIN(SyncDivision, Sixteenth, 16),
        PIN(SyncDivision, SixteenBars, 1600),
    };
}

#undef PIN

}  // namespace

TEST_CASE("Persisted enum ordinals match their pins", "[serialization][migration][enums]") {
    for (const auto& pin : allPins()) {
        INFO(juce::String(pin.enumName) + "::" + pin.enumeratorName + " is " +
             juce::String(pin.actual) + ", pinned at " + juce::String(pin.expected) +
             ". These integers are in every project file saved so far: append new "
             "values at the end, never insert or renumber.");
        CHECK(pin.actual == pin.expected);
    }
}

TEST_CASE("Time stretch mode constants match their pins", "[serialization][migration][enums]") {
    CHECK(time_stretch_mode::kDisabled == 0);
    CHECK(time_stretch_mode::kSoundTouchNormal == 3);
    CHECK(time_stretch_mode::kSoundTouchBetter == 4);
    CHECK(time_stretch_mode::kSignalsmith == 15);
}

}  // namespace magda
