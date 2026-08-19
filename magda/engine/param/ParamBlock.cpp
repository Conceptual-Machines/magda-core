#include "param/ParamBlock.hpp"

#include <algorithm>

namespace magda::engine {

float ParamValues::valueAt(int sampleOffset) const {
    if (segments_.empty())
        return 0.0f;

    const auto sample = std::clamp(sampleOffset, 0, std::max(numSamples_ - 1, 0));

    // Linear rather than binary: a parameter carries a handful of segments and
    // the caller is walking the block in order, so the first one it looks at is
    // usually the one it wants.
    std::size_t index = 0;
    while (index + 1 < segments_.size() && segments_[index + 1].startSample <= sample)
        ++index;

    const auto& segment = segments_[index];
    const auto end = index + 1 < segments_.size() ? segments_[index + 1].startSample : numSamples_;
    const auto span = end - segment.startSample;
    if (span <= 0)
        return segment.startValue;

    const auto position =
        static_cast<float>(sample - segment.startSample) / static_cast<float>(span);
    return segment.startValue + (segment.endValue - segment.startValue) * position;
}

ParamValues DeviceParams::operator[](int paramIndex) const {
    if (paramIndex < 0 || paramIndex >= size())
        return {};

    const auto offset = static_cast<std::size_t>(paramIndex) * static_cast<std::size_t>(stride_);
    const auto count = static_cast<std::size_t>(counts_[static_cast<std::size_t>(paramIndex)]);
    return ParamValues{segments_.subspan(offset, count), numSamples_};
}

void ResolvedParams::prepare(int numParams, int segmentCapacity) {
    stride_ = std::max(segmentCapacity, 1);
    const auto count = static_cast<std::size_t>(std::max(numParams, 0));
    segments_.assign(count * static_cast<std::size_t>(stride_), ParamSegment{});
    counts_.assign(count, 0);
    numSamples_ = 0;
}

void ResolvedParams::beginBlock(int numSamples) {
    numSamples_ = numSamples;
    std::fill(counts_.begin(), counts_.end(), 0);
}

ParamValues ResolvedParams::operator[](int param) const {
    if (param < 0 || param >= size())
        return {};

    const auto offset = static_cast<std::size_t>(param) * static_cast<std::size_t>(stride_);
    const auto count = static_cast<std::size_t>(counts_[static_cast<std::size_t>(param)]);
    return ParamValues{std::span<const ParamSegment>{segments_}.subspan(offset, count),
                       numSamples_};
}

DeviceParams ResolvedParams::device(int firstParam, int count) const {
    if (firstParam < 0 || count <= 0 || firstParam + count > size())
        return {};

    const auto offset = static_cast<std::size_t>(firstParam) * static_cast<std::size_t>(stride_);
    const auto width = static_cast<std::size_t>(count) * static_cast<std::size_t>(stride_);
    return DeviceParams{std::span<const ParamSegment>{segments_}.subspan(offset, width),
                        std::span<const int>{counts_}.subspan(static_cast<std::size_t>(firstParam),
                                                              static_cast<std::size_t>(count)),
                        stride_, numSamples_};
}

ParamSegment* ResolvedParams::slotFor(int param) {
    if (param < 0 || param >= size())
        return nullptr;

    return segments_.data() + static_cast<std::size_t>(param) * static_cast<std::size_t>(stride_);
}

int ResolvedParams::segmentCount(int param) const {
    if (param < 0 || param >= size())
        return 0;

    return counts_[static_cast<std::size_t>(param)];
}

void ResolvedParams::setSegmentCount(int param, int count) {
    if (param < 0 || param >= size())
        return;

    counts_[static_cast<std::size_t>(param)] = std::clamp(count, 0, stride_);
}

}  // namespace magda::engine
