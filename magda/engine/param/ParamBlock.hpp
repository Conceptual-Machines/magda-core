#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <span>
#include <vector>

#include "core/ParameterUtils.hpp"

/**
 * @file ParamBlock.hpp
 * @brief What a device reads when it asks what a parameter is right now.
 *
 * A parameter is written by several things at once: the stored value a fader or
 * a control surface sets, the automation curve playing over it, the modifiers
 * linked to it. None of that reaches a device. What reaches a device is one
 * resolved value stream per parameter, in the parameter's own units, already
 * clamped and already quantised, and it is the only thing a device is allowed to
 * read. Nothing downstream of here can rediscover what automation and modulation
 * do to each other, because nothing downstream of here is shown them.
 *
 * The stream is segments rather than a scalar, and that is what lets "resolved
 * once per block" and "a ramp inside the block" both be true. A parameter that
 * does not move is one segment and costs a float; a parameter sweeping under
 * automation is a handful, and a device that asked for them reads a value per
 * sample without any of them asking where it came from.
 *
 * Resolution itself is ParamResolve.hpp. This is the shape the answer has.
 */

namespace magda::engine {

/**
 * @brief One stretch of a block over which a parameter moves linearly.
 *
 * The value at @ref startSample is @ref startValue, and it travels to
 * @ref endValue, which is the value at the first sample the next segment covers,
 * or one past the block's last sample for the final segment. Two segments in a
 * row therefore meet exactly, and a step is a segment whose start differs from
 * the previous one's end rather than a pair of points at one sample.
 *
 * Normalised positions, unclamped, wherever a segment appears: in an automation
 * lane on the way in, and in the resolved stream on the way out. A device still
 * reads Hz and dB, because ParamValues converts on the way past, and the two
 * facts have to go together.
 *
 * A segment that carried the parameter's own units could not be read correctly
 * at any sample but its ends. Interpolating between two converted endpoints is
 * only the same as converting the interpolated position when the scale is a
 * straight line: on a logarithmic cutoff sweeping its whole range, the halfway
 * point is 632 Hz through the scale and 10,010 Hz through the chord across it.
 * Leaving the values unclamped is the same argument one step further out. A ramp
 * that leaves the range partway through holds at the top from there on, and a
 * pair of clamped endpoints describes a slower ramp that arrives at the end
 * instead, which is a plateau the parameter never had.
 */
struct ParamSegment {
    int startSample = 0;
    float startValue = 0.0f;
    float endValue = 0.0f;
};

/**
 * @brief One parameter's resolved value over one block.
 *
 * A view over segments the resolver wrote; it owns nothing and outlives
 * nothing. Handed to a device inside its DeviceBlock and dead when the block is.
 *
 * The clamp and the scale are applied here rather than by the resolver, at the
 * position being read rather than at the ends of a segment, which is the only
 * place either can be applied correctly (see ParamSegment). The device sees the
 * parameter's own units and no sign of where the arithmetic happened.
 */
class ParamValues {
  public:
    ParamValues() = default;

    ParamValues(std::span<const ParamSegment> segments,
                const magda::ParameterUtils::ParameterDomain& domain, int numSamples)
        : segments_(segments), domain_(domain), numSamples_(numSamples) {}

    /**
     * @brief The parameter's value for the block.
     *
     * The value at its first sample, which is what the incumbent engine's
     * parameters take at a block boundary. A device that has not asked for
     * segments reads this and is no further from the fork than the fork is from
     * itself.
     */
    float value() const {
        return segments_.empty()
                   ? 0.0f
                   : magda::ParameterUtils::normalizedToReal(segments_.front().startValue, domain_);
    }

    /**
     * @brief The parameter's value at one sample of the block.
     *
     * For a device that asked for segment accuracy. Offsets outside the block
     * clamp to its ends rather than reading off either side of the segments.
     */
    float valueAt(int sampleOffset) const;

    /// Whether the parameter holds one value for the whole block. True for
    /// everything the port produces, since the fork settles parameters at block
    /// boundaries and nothing opts out of that during the port.
    ///
    /// Read off the stored positions rather than the values they convert to, so
    /// a stream that moves only outside the range says it moves. A device using
    /// this to skip a per-sample read is told to do the work it did not need,
    /// which is the direction that costs nothing but time.
    bool isConstant() const {
        return segments_.size() <= 1 &&
               (segments_.empty() || segments_.front().startValue == segments_.front().endValue);
    }

    /// No segments at all, which is a parameter nothing resolved. A device
    /// reading one is a bug upstream of it, not a value it should try to use.
    bool empty() const {
        return segments_.empty();
    }

    std::span<const ParamSegment> segments() const {
        return segments_;
    }

    int numSegments() const {
        return static_cast<int>(segments_.size());
    }

    int numSamples() const {
        return numSamples_;
    }

  private:
    std::span<const ParamSegment> segments_;
    magda::ParameterUtils::ParameterDomain domain_;
    int numSamples_ = 0;
};

/**
 * @brief The parameters of one device, in the order the device declared them.
 *
 * A device indexes its own parameters from zero, the way ParameterInfo's
 * paramIndex does, and never learns where in the table they sit. The window is
 * contiguous because a device's parameters are allocated together, which is what
 * makes this a pair of integers rather than a map.
 */
class DeviceParams {
  public:
    DeviceParams() = default;

    DeviceParams(std::span<const ParamSegment> segments, std::span<const int> counts,
                 std::span<const magda::ParameterUtils::ParameterDomain> domains, int stride,
                 int numSamples)
        : segments_(segments),
          counts_(counts),
          domains_(domains),
          stride_(stride),
          numSamples_(numSamples) {}

    int size() const {
        return static_cast<int>(counts_.size());
    }

    /// The device's parameter @p paramIndex. An index the device does not have
    /// is an empty view: the table is sized from the device's own specs when the
    /// plan is prepared, so this is a device asking for a parameter it never
    /// declared.
    ParamValues operator[](int paramIndex) const;

  private:
    std::span<const ParamSegment> segments_;
    std::span<const int> counts_;
    std::span<const magda::ParameterUtils::ParameterDomain> domains_;
    int stride_ = 0;
    int numSamples_ = 0;
};

/**
 * @brief Every parameter behind one plan, resolved for one block.
 *
 * One table, indexed by the parameter ids the plan hands out, with each
 * parameter's segments in a fixed-width slot of a flat arena. Fixed width
 * because the arena is allocated once, off the audio thread, and a parameter
 * that needed to grow mid-block would be an allocation on it.
 *
 * The width is a budget, like the MIDI one in EngineDevice.hpp, and a curve with
 * more breakpoints than fit is coarsened rather than cut off. What that means
 * differs by what the parameter is, and both readings keep the destination: a
 * continuous parameter rolls the tail into its last segment, which ramps to
 * where the curve ends, and a stepped one spends its last slot on the step the
 * curve ends on, losing the steps in between instead of the arrival.
 */
class ResolvedParams {
  public:
    /// Segments per parameter when nothing says otherwise. Sixteen breakpoints
    /// is a curve moving faster than a block at any usable block size, and a
    /// parameter that is not automated uses one of them.
    static constexpr int kDefaultSegmentCapacity = 16;

    /// Off the audio thread, before the first block. Every parameter starts
    /// empty; a block that resolves nothing hands out empty views rather than
    /// stale ones.
    void prepare(int numParams, int segmentCapacity = kDefaultSegmentCapacity);

    /// How many parameters the table holds.
    int size() const {
        return static_cast<int>(counts_.size());
    }

    int segmentCapacity() const {
        return stride_;
    }

    /// The block the table currently holds values for. Set by the resolver.
    int numSamples() const {
        return numSamples_;
    }

    /// On the audio thread, at the top of a block, before anything resolves
    /// into it. Empties every parameter and records the block length, so a
    /// parameter nobody resolved this block reads as empty rather than as
    /// whatever it was last block.
    void beginBlock(int numSamples);

    /// On the audio thread: the values @p param resolved to. Empty for a
    /// parameter the table does not have.
    ParamValues operator[](int param) const;

    /**
     * @brief What a link reading @p param as its source gets.
     *
     * The position the parameter opens the block at, clamped into range: a
     * macro is a source between nothing and everything whatever the arithmetic
     * that produced it did, and a source is read once per block because that is
     * what a modulation contribution is.
     *
     * Zero for a parameter that has not resolved yet, which the resolution
     * order exists to make unreachable: a source is resolved before whatever
     * reads it.
     */
    float sourceValue(int param) const;

    /// The window a device reads: @p count parameters from @p firstParam.
    DeviceParams device(int firstParam, int count) const;

    /// Where the resolver writes @p param's segments. Never null for a
    /// parameter the table has; @ref segmentCapacity() wide.
    ParamSegment* slotFor(int param);

    /// How many segments @p param currently holds, and what it holds after the
    /// resolver has written them.
    int segmentCount(int param) const;
    void setSegmentCount(int param, int count);

    /// The scale @p param's stored positions are read through. Written by the
    /// resolver from the parameter's spec, and travelling with the values
    /// rather than beside them, because a position without the scale that
    /// interprets it is not a value at all.
    void setDomain(int param, const magda::ParameterUtils::ParameterDomain& domain);

  private:
    std::vector<ParamSegment> segments_;
    std::vector<int> counts_;
    std::vector<magda::ParameterUtils::ParameterDomain> domains_;
    int stride_ = 0;
    int numSamples_ = 0;
};

}  // namespace magda::engine
