// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace magda::daw::audio {

/**
 * @brief The Spectrum Analyzer's FFT (#3185): Hann frames at 50% overlap,
 *        every frame since the last call power-averaged into one spectrum.
 *
 * Message thread. Output is per-bin power in dBFS: a full-scale sine centred
 * on a bin reads 0 dB.
 */
class OverlappedSpectrum {
  public:
    static constexpr int kMinOrder = 11;
    static constexpr int kMaxOrder = 13;

    void prepare(int fftOrder);
    /// Forget what was analysed, so the next call takes one fresh frame.
    void reset() {
        analysedTo_ = 0;
    }

    int fftSize() const {
        return fftSize_;
    }
    int numBins() const {
        return fftSize_ / 2;
    }

    /// How many samples to read so every frame ending since the last analyse()
    /// is covered, at most @p maxSamples. 0 when nothing new has been written.
    int samplesToRead(std::size_t writePosition, int maxSamples) const;

    /// Average the frames in @p samples, the latest @p numSamples ending at
    /// @p writePosition, into @p dbOut (numBins() values).
    void analyse(const float* samples, int numSamples, std::size_t writePosition,
                 std::vector<float>& dbOut);

  private:
    int fftOrder_ = 0;
    int fftSize_ = 0;
    std::unique_ptr<juce::dsp::FFT> fft_;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window_;
    std::vector<float> frame_;
    std::vector<float> power_;
    std::size_t analysedTo_ = 0;
};

/**
 * @brief One value per pixel column of a log-frequency axis from per-bin dB.
 *
 * A column covering bins takes their maximum; a column between bins takes a
 * cubic through its neighbours: never below them, and above only as far as a
 * tone between two bins can peak.
 */
void spectrumColumns(const std::vector<float>& binDb, double binHz, float minHz, float maxHz,
                     std::vector<float>& columnDb);

/// The nearest note and its offset, e.g. "A3 +12c" (octave 3 = middle C).
juce::String nearestNoteLabel(double hz);

}  // namespace magda::daw::audio
