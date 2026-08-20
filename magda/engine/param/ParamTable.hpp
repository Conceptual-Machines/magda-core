#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/AutomationInfo.hpp"
#include "core/ModInfo.hpp"
#include "param/ModAdsr.hpp"
#include "param/ModFollower.hpp"
#include "param/ModLfo.hpp"
#include "param/ModRandom.hpp"
#include "param/ParamKey.hpp"
#include "param/ParamSpec.hpp"

/**
 * @file ParamTable.hpp
 * @brief Every parameter behind one plan, and what writes each of them.
 *
 * The plan is topology and carries no values (RenderPlan.hpp). PlanValues is
 * what a mixer move can touch, resolved per op. This is the third of the three:
 * what a device reads, resolved per parameter, and it is where a parameter's
 * scale, its stored value and the links reaching it are settled against the
 * plan they belong to.
 *
 * Built off the audio thread, published with the values, and read on the audio
 * thread by ParamResolve.hpp, which turns it into the value streams devices
 * see. Immutable once published, like everything else that crosses that line.
 *
 * Fingerprinted against the plan for the same reason PlanValues is: parameters
 * are addressed by index, and a table resolved against a plan that has since
 * been swapped would put one device's cutoff on another.
 */

namespace magda::engine {

/** @brief Where one link's modulation comes from. */
struct ParamSourceRef {
    enum class Kind : std::uint8_t {
        Parameter,  ///< another parameter's own value, which is what a macro is
        Modifier,   ///< a modifier's output
    };

    Kind kind = Kind::Parameter;

    /// A ParamId, or an index into ParamTable::modifiers.
    int index = INVALID_PARAM_ID;

    bool operator==(const ParamSourceRef& o) const = default;
};

/** @brief One thing writing one parameter's modulation lane. */
struct ParamLink {
    ParamSourceRef source;

    /// Depth and polarity, per link rather than per source: one macro drives
    /// several parameters by different amounts and in different directions, and
    /// that is where MAGDA keeps them.
    float amount = 0.0f;
    bool bipolar = false;
};

/**
 * @brief One modifier instance, as the thing links read.
 *
 * A source with an identity, so a link can name one and the resolution order
 * can account for one, plus what it takes to run it.
 *
 * Four kinds and four settings structs, side by side rather than in a union or
 * a variant. Exactly one of them is live and the other three are dead weight,
 * which is about a hundred bytes per modifier: a project with a modifier on
 * every scope it could have one on spends a few tens of kilobytes on the
 * padding, once, off the audio thread. A variant would save that and cost a
 * discriminated read on the block path, and the table's whole design is that
 * what crosses to the audio thread is flat and copyable.
 */
struct ParamModifier {
    /// The modifier itself: its owner's scope and its id, with no parameter
    /// index (see ParamKey::modId).
    ParamKey key;

    /// Its output, 0 to 1, as the model last saw it. What a kind with no
    /// engine publishes, and what a block with no runtime behind it reads.
    float value = 0.0f;

    ModKind kind = ModKind::Lfo;

    /// A modifier the model has switched off contributes nothing at all: its
    /// links are not carried, because a bipolar link reading an output of zero
    /// would push its parameter down by the link's own depth, and the model's
    /// answer is that there is no modifier there.
    bool enabled = true;

    /// What the modifier is, for whichever @ref kind it is. The other three are
    /// left at their defaults and never read.
    LfoSettings lfo;
    AdsrSettings adsr;
    RandomSettings random;
    FollowerSettings follower;

    /**
     * @brief The track whose signal this modifier listens to, or
     *        INVALID_TRACK_ID.
     *
     * Set for every modifier that listens at all: a follower, which is nothing
     * but its source, and a MIDI- or audio-triggered one, which waits on that
     * track's notes or its level. A free-running or timeline-locked modifier
     * listens to nothing and says so by having none of this, which is what
     * keeps a plan from carrying a track's signal nobody reads.
     *
     * Which track it is is ModSources.hpp's rule, called by both compilers.
     * Carried on the modifier rather than in the plan because it is not
     * topology, and read by the executor to decide who is on the far end of
     * each modulation tap.
     */
    magda::TrackId source = magda::INVALID_TRACK_ID;

    /**
     * @brief The modifier's own Rate parameter, or INVALID_PARAM_ID.
     *
     * Carried only when something reaches it: a lane playing over the rate, or
     * a macro or another modifier driving it. The overwhelming majority of
     * modifiers have a rate nothing touches, and one parameter each for them
     * would be a table mostly made of numbers that never move.
     *
     * Its value is a frequency or a division ordinal depending on whether the
     * modifier is tempo synced, which is the same one lane the model's Rate
     * control writes (AutomationInfo's makeHzRateInfo / makeSyncDivisionInfo).
     * The LFO and the random walk share it; an envelope's stage lengths and a
     * follower's time constants are not rates and have none.
     */
    ParamId rate = INVALID_PARAM_ID;
};

/**
 * @brief One step of the order a block resolves in.
 *
 * Two kinds of thing resolve, and they depend on each other in both
 * directions: a parameter reads the modifiers linked to it, and a modifier
 * reads the Rate parameter driving it. One order over both is what lets a
 * macro drive an LFO's rate while the LFO drives a cutoff, in one pass, with
 * no second look and no fixed point.
 */
struct ParamStep {
    enum class Kind : std::uint8_t {
        Parameter,  ///< resolve the parameter at @ref index
        Modifier,   ///< advance the modifier at @ref index
    };

    Kind kind = Kind::Parameter;
    int index = 0;

    bool operator==(const ParamStep&) const = default;
};

/**
 * @brief The parameters of one plan, with their scales, values and links.
 *
 * Flat and index-parallel: a ParamId indexes keys, specs and base alike. Links
 * are stored once, in one vector, with each parameter's own in a half-open
 * range of it.
 */
struct ParamTable {
    /// The plan this was resolved against. Zero for a table belonging to no
    /// plan, which is what a test that resolves parameters on their own has.
    std::uint64_t planFingerprint = 0;

    std::vector<ParamKey> keys;
    std::vector<ParamSpec> specs;

    /// The stored value, normalised. What a knob, a control surface or a host
    /// write moves, and what the parameter is where no automation covers it.
    std::vector<float> base;

    /// Links reaching parameter i: links[linkOffsets[i], linkOffsets[i + 1]).
    /// One vector rather than one per parameter, because almost no parameter
    /// has a link and a vector each would be an allocation each.
    std::vector<ParamLink> links;
    std::vector<int> linkOffsets;

    std::vector<ParamModifier> modifiers;

    /**
     * @brief The curves drawn on the modifiers, in one arena.
     *
     * modCurvePoints[modCurveOffsets[i], modCurveOffsets[i + 1]) is the cycle
     * modifier i has drawn on it, and empty is a modifier playing a built-in
     * waveform, which is almost all of them. One arena rather than a vector
     * each, for the reason the links have one.
     */
    std::vector<magda::CurvePointData> modCurvePoints;
    std::vector<int> modCurveOffsets;

    /**
     * @brief The lane playing over each parameter, in beats.
     *
     * Breakpoints, as the model draws them: curvePoints[curveOffsets[i],
     * curveOffsets[i + 1]) is the curve over parameter i, and empty is a
     * parameter with no lane, a lane with nothing on it, or a lane the model is
     * not currently playing.
     *
     * Beats rather than samples, and not baked into segments here, because a
     * block is what decides which part of a curve is being asked about and how
     * long it lasts. A curve resolved into samples at publish time would be a
     * curve that had to be republished every time the tempo moved.
     *
     * A clip-based lane arrives already flattened: its loops unrolled, its gaps
     * held at the nearest clip edge, its overlaps resolved by the lane's own
     * precedence. That is the model's answer to what the lane is (#1087), and
     * the engine asks for it rather than working out a second one.
     */
    std::vector<magda::AutomationPoint> curvePoints;
    std::vector<int> curveOffsets;

    /**
     * @brief The order the block resolves in.
     *
     * Every parameter and every modifier, once each, arranged so that anything
     * a step reads has already been resolved when it is reached. A macro
     * driving an LFO's rate while the LFO drives a device parameter therefore
     * needs no second pass and no fixed point, and the audio thread walks a
     * vector.
     */
    std::vector<ParamStep> order;

    /// The most links any one parameter has, which is how much room the block
    /// resolver needs to gather one parameter's contributions.
    int maxLinksPerParam = 0;

    /**
     * @brief Identity of the layout: which parameter is at which ParamId.
     *
     * What the plan's fingerprint is to the op values, one level down. A table
     * is read by index, and by windows of indices, and two tables of the same
     * size can still put different parameters at the same address: a macro that
     * stopped being used in one scope and started being used in another leaves
     * the count alone and moves every device window after it. Anything holding
     * an index resolved against one table has to be able to tell that the other
     * is not it.
     *
     * Over the keys in order, and nothing else. Values, links and the
     * resolution order all change without moving a parameter, and a fingerprint
     * that moved with them would turn every knob into a structural edit.
     */
    std::uint64_t layoutFingerprint = 0;

    /**
     * @brief Identity of the modifier list: which modifier is at which index.
     *
     * What @ref layoutFingerprint is to the parameters, and needed separately
     * because modifiers are not parameters: a scope gaining a page of them
     * moves every modifier after it without touching a key, and the runtime
     * holding each one's phase is indexed the same way. Without it a values
     * publish would hand one LFO's phase to another.
     */
    std::uint64_t modifierFingerprint = 0;

    /** Where one device's parameters sit in the table. */
    struct DeviceWindow {
        ParamId first = 0;
        int count = 0;
    };

    /// Contiguous per device and indexed from zero by the device's own
    /// parameter index, which is what lets a device be handed a window.
    std::unordered_map<DeviceKey, DeviceWindow, DeviceKeyHash> deviceWindows;

    /// What the model asked for that this could not express, in the order it
    /// was met. Never a silent drop, exactly as the plan compiler's are.
    std::vector<std::string> diagnostics;

    /// Parameters by their key. Built with the table, because both the link
    /// resolution that fills it and the lane binding that will read it (#2118)
    /// ask the same question.
    std::unordered_map<ParamKey, ParamId, ParamKeyHash> byKey;

    int size() const {
        return static_cast<int>(keys.size());
    }

    /// The parameter @p key names, or INVALID_PARAM_ID.
    ParamId find(const ParamKey& key) const {
        const auto found = byKey.find(key);
        return found == byKey.end() ? INVALID_PARAM_ID : found->second;
    }

    /// The links reaching @p param, empty when nothing modulates it.
    std::span<const ParamLink> linksFor(ParamId param) const;

    /// The lane playing over @p param, empty when none is.
    std::span<const magda::AutomationPoint> curveFor(ParamId param) const;

    /// The cycle drawn on modifier @p modifier, empty when it plays a built-in
    /// waveform.
    std::span<const magda::CurvePointData> modCurveFor(int modifier) const;

    /// Where @p device's parameters are, or a window of nothing.
    DeviceWindow windowFor(const DeviceKey& device) const {
        const auto found = deviceWindows.find(device);
        return found == deviceWindows.end() ? DeviceWindow{} : found->second;
    }
};

/**
 * @brief The layout fingerprint of a sequence of keys.
 *
 * Exposed because the thing that has to agree about a layout is not always the
 * thing that built it: an executor prepared against one table meets another,
 * and the question it asks is whether the parameter at an index is still the
 * parameter that index was resolved for.
 */
std::uint64_t paramLayoutFingerprint(const std::vector<ParamKey>& keys);

/**
 * @brief The modifier fingerprint of a modifier list.
 *
 * Exposed for the same reason the layout's is: the thing holding a modifier's
 * phase was prepared against one list and has to be able to tell that the
 * table it meets is not it.
 */
std::uint64_t paramModifierFingerprint(const std::vector<ParamModifier>& modifiers);

}  // namespace magda::engine
