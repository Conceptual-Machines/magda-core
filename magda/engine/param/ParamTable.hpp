#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

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
 * Its value is the model's own for now, which is a constant for the duration of
 * a block and a constant for the duration of a publish. The engines that make
 * it move arrive in #2119 and #2120; what is settled here is that a modifier is
 * a source with an identity, so a link can name one and the resolution order
 * can account for one.
 */
struct ParamModifier {
    /// The modifier itself: its owner's scope and its id, with no parameter
    /// index (see ParamKey::modId).
    ParamKey key;

    /// Its output, 0 to 1, as the model last saw it.
    float value = 0.0f;
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
     * @brief The order parameters resolve in.
     *
     * A permutation of every ParamId, arranged so that anything a parameter
     * reads has already been resolved when it is. A macro driving a macro
     * driving a device parameter therefore needs no second pass and no fixed
     * point, and the audio thread walks a vector.
     */
    std::vector<ParamId> order;

    /// The most links any one parameter has, which is how much room the block
    /// resolver needs to gather one parameter's contributions.
    int maxLinksPerParam = 0;

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

    /// Where @p device's parameters are, or a window of nothing.
    DeviceWindow windowFor(const DeviceKey& device) const {
        const auto found = deviceWindows.find(device);
        return found == deviceWindows.end() ? DeviceWindow{} : found->second;
    }
};

}  // namespace magda::engine
