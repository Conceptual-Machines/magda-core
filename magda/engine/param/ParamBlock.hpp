#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <span>
#include <vector>

#include "core/ParameterUtils.hpp"

/**
 * @file ParamBlock.hpp
 * @brief The resolved parameter values a device reads for one block.
 *
 * Stored value, automation and modulation are combined by ParamResolve.hpp into
 * one segment stream per parameter; a device reads only that stream.
 */

namespace magda::engine {

/**
 * @brief One stretch of a block over which a parameter moves linearly.
 *
 * @ref endValue is the value at the first sample of the next segment, or one
 * past the block for the last. Values are unclamped normalised positions: the
 * scale and the clamp are applied per sample by ParamValues, since converting or
 * clamping the endpoints first misreads a curved scale or a ramp that leaves the
 * range mid-block.
 */
struct ParamSegment {
    int startSample = 0;
    float startValue = 0.0f;
    float endValue = 0.0f;
};

/**
 * @brief One parameter's resolved value over one block.
 *
 * A non-owning view over the resolver's segments, valid for the block only.
 * Converts to the parameter's own units at the sample being read.
 */
class ParamValues {
  public:
    ParamValues() = default;

    ParamValues(std::span<const ParamSegment> segments,
                const magda::ParameterUtils::ParameterDomain& domain, int numSamples)
        : segments_(segments), domain_(domain), numSamples_(numSamples) {}

    /// The value at the block's first sample, in the parameter's own units.
    float value() const {
        return segments_.empty()
                   ? 0.0f
                   : magda::ParameterUtils::normalizedToReal(segments_.front().startValue, domain_);
    }

    /// The normalised position at the block's first sample, clamped to 0..1.
    /// For a hosted plugin, whose parameter is normalised by definition.
    float position() const {
        return segments_.empty() ? 0.0f : juce::jlimit(0.0f, 1.0f, segments_.front().startValue);
    }

    /// The value at @p sampleOffset, clamped to the block's ends.
    float valueAt(int sampleOffset) const;

    /// Whether the parameter holds one value for the whole block. Judged on the
    /// stored positions, so a ramp entirely outside the range still counts as moving.
    bool isConstant() const {
        return segments_.size() <= 1 &&
               (segments_.empty() || segments_.front().startValue == segments_.front().endValue);
    }

    /// No segments: nothing resolved this parameter.
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
 * @brief The parameters of one device, indexed by ParameterInfo::paramIndex.
 *
 * A contiguous window of the table; the device never sees table ids.
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

    /// The device's parameter @p paramIndex; empty for one it never declared.
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
 * A flat arena of fixed-width segment slots, allocated once off the audio
 * thread. A curve with more breakpoints than fit is coarsened, keeping its end
 * value: a continuous parameter ramps its last segment to it, a stepped one
 * lands its last slot on it.
 */
class ResolvedParams {
  public:
    /// Segments per parameter when nothing says otherwise.
    static constexpr int kDefaultSegmentCapacity = 16;

    /// Off the audio thread, before the first block. Every parameter starts empty.
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

    /// Audio thread, at the top of a block: empties every parameter and records
    /// the block length, so an unresolved parameter reads as empty, not stale.
    void beginBlock(int numSamples);

    /// Audio thread: the values @p param resolved to; empty for an unknown param.
    ParamValues operator[](int param) const;

    /// What a link reading @p param as its source gets: the clamped position at
    /// the block's first sample. Zero if it has not resolved yet.
    float sourceValue(int param) const;

    /// The window a device reads: @p count parameters from @p firstParam.
    DeviceParams device(int firstParam, int count) const;

    /// Where the resolver writes @p param's segments. Never null for a
    /// parameter the table has; @ref segmentCapacity() wide.
    ParamSegment* slotFor(int param);

    /// How many segments @p param holds.
    int segmentCount(int param) const;
    void setSegmentCount(int param, int count);

    /// The scale @p param's stored positions are read through.
    void setDomain(int param, const magda::ParameterUtils::ParameterDomain& domain);

  private:
    std::vector<ParamSegment> segments_;
    std::vector<int> counts_;
    std::vector<magda::ParameterUtils::ParameterDomain> domains_;
    int stride_ = 0;
    int numSamples_ = 0;
};

}  // namespace magda::engine
