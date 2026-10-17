#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <memory>

namespace magda {

/**
 * @brief Plays a preview at another tempo without changing its pitch.
 *
 * Wraps the file's reader source; positions and length stay in the file's frames,
 * so a transport and a waveform playhead read it like the unstretched file.
 * A rate of 1 passes the input through untouched.
 */
class PreviewStretchSource : public juce::PositionableAudioSource {
  public:
    PreviewStretchSource(juce::PositionableAudioSource* input, int numChannels);
    ~PreviewStretchSource() override;

    /** @brief File frames consumed per output frame; 1 bypasses. Safe from any thread. */
    void setRate(double rate);
    double getRate() const {
        return rate_.load(std::memory_order_relaxed);
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

    void setNextReadPosition(juce::int64 newPosition) override;
    juce::int64 getNextReadPosition() const override;
    juce::int64 getTotalLength() const override;
    bool isLooping() const override;
    void setLooping(bool shouldLoop) override;

    /// Fastest and slowest rates the buffers are sized for; setRate clamps to these.
    static constexpr double kMinRate = 0.25;
    static constexpr double kMaxRate = 4.0;

  private:
    struct Engine;

    void readInput(int numSamples);

    juce::PositionableAudioSource* input_;
    int numChannels_;
    std::unique_ptr<Engine> engine_;
    juce::AudioBuffer<float> inputBuffer_;
    int maxBlock_ = 0;

    std::atomic<double> rate_{1.0};
    /// Input frames read past the audible position: the priming window.
    juce::int64 lead_ = 0;
    /// Next block must prime before it plays: after a seek, or on the switch to stretching.
    bool needsPrime_ = true;
    bool wasStretching_ = false;
    double inputCarry_ = 0.0;
};

}  // namespace magda
