#include "io/CapturedInsert.hpp"

#include <algorithm>
#include <cmath>

namespace magda::engine {

namespace {

/// How much timeline one capture holds, in seconds.
///
/// A number rather than a growing buffer, because the alternative is an
/// allocation on the audio thread: the live pass writes from the audio thread
/// and a vector that grew there would be exactly the thing the rest of this
/// engine spends its time not doing. Ten minutes at any rate anybody renders
/// at, which is longer than the arrangements this covers and cheap enough that
/// the ceiling is not what anyone hits first.
constexpr double kCaptureSeconds = 600.0;

}  // namespace

std::int64_t CapturedInsert::timelineSampleOf(const BlockInfo& block) const {
    return static_cast<std::int64_t>(std::llround(block.startSeconds * sampleRate_));
}

CapturedInsert::CapturedInsert(Mode mode, int numChannels, EngineInsert* live)
    : mode_(mode), numChannels_(std::max(1, numChannels)), live_(live) {}

void CapturedInsert::prepare(const RenderContext& context) {
    if (live_ != nullptr)
        live_->prepare(context);

    sampleRate_ = context.sampleRate;

    const auto capacity =
        static_cast<std::size_t>(std::max(0.0, context.sampleRate * kCaptureSeconds));

    // Only while capturing. A bounce is handed a capture that already exists and
    // must not clear it, which is the one way this class could quietly turn a
    // render into silence.
    if (mode_ != Mode::Capturing)
        return;

    samples_.assign(static_cast<std::size_t>(numChannels_), std::vector<float>(capacity, 0.0f));
    captured_ = 0;
}

void CapturedInsert::reset() {
    if (live_ != nullptr)
        live_->reset();
}

int CapturedInsert::latencySamples() const {
    if (mode_ == Mode::Playing)
        return 0;
    return live_ != nullptr ? live_->latencySamples() : 0;
}

bool CapturedInsert::covers(std::int64_t first, std::int64_t count) const {
    return first >= 0 && count >= 0 && first + count <= captured_;
}

void CapturedInsert::send(const BlockInfo& block, juce::dsp::AudioBlock<const float> audio,
                          const juce::MidiBuffer& midi) {
    // A bounce sends nothing. There is nothing on the other end that could
    // answer in time, and a send that went out anyway would put the render's
    // audio through somebody's monitors at whatever speed the render is going.
    if (mode_ == Mode::Playing || live_ == nullptr)
        return;

    live_->send(block, audio, midi);
}

void CapturedInsert::receive(const BlockInfo& block, juce::dsp::AudioBlock<float> audio,
                             juce::MidiBuffer& midi) {
    const auto numSamples = static_cast<std::int64_t>(audio.getNumSamples());
    const auto channels = static_cast<int>(audio.getNumChannels());

    if (mode_ == Mode::Capturing) {
        if (live_ != nullptr)
            live_->receive(block, audio, midi);
        else
            audio.clear();

        // Written down at the timeline position the block says it is at, so a
        // bounce visiting the timeline in different block sizes reads back what
        // was played at the same place rather than at the same call.
        //
        // From seconds rather than from a sample count, because seconds are
        // what the block carries and what recorded material is measured in
        // (BlockInfo::startSeconds). A capture indexed by how many blocks have
        // gone by would come back smeared the moment a bounce used a different
        // block size, which it always does.
        const auto first = timelineSampleOf(block);
        if (first < 0)
            return;

        for (int channel = 0; channel < channels && channel < numChannels_; ++channel) {
            auto& written = samples_[static_cast<std::size_t>(channel)];
            const auto room = static_cast<std::int64_t>(written.size()) - first;
            const auto count = std::min(numSamples, std::max<std::int64_t>(0, room));

            for (std::int64_t sample = 0; sample < count; ++sample)
                written[static_cast<std::size_t>(first + sample)] =
                    audio.getSample(channel, static_cast<int>(sample));
        }

        captured_ = std::max(captured_, first + numSamples);
        return;
    }

    // Playing. Silence outside what was captured: a bounce of a range nobody
    // played through the hardware has no answer, and the nearest thing captured
    // is not it.
    const auto first = timelineSampleOf(block);
    for (int channel = 0; channel < channels; ++channel) {
        const auto stored = channel < static_cast<int>(samples_.size())
                                ? &samples_[static_cast<std::size_t>(channel)]
                                : nullptr;

        for (std::int64_t sample = 0; sample < numSamples; ++sample) {
            const auto position = first + sample;
            const auto have = stored != nullptr && position >= 0 && position < captured_ &&
                              position < static_cast<std::int64_t>(stored->size());

            audio.setSample(channel, static_cast<int>(sample),
                            have ? (*stored)[static_cast<std::size_t>(position)] : 0.0f);
        }
    }

    midi.clear();
}

}  // namespace magda::engine
