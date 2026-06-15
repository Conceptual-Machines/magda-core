// See BasicPitchPostprocess.hpp. Faithful port of basic_pitch/note_creation.py
// (Apache-2.0, Copyright 2022-2024 Spotify AB).

#include "BasicPitchPostprocess.hpp"

#include <algorithm>
#include <cmath>

namespace magda::transcription::basicpitch {

namespace {

constexpr double kMagicAlignmentOffset = 0.0018;  // MAGIC_ALIGNMENT_OFFSET

double midiToHz(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

double hzToMidi(double hz) {
    return 69.0 + 12.0 * std::log2(hz / 440.0);
}

double midiPitchToContourBin(int pitchMidi) {
    const double pitchHz = midiToHz(static_cast<double>(pitchMidi));
    return 12.0 * kContoursBinsPerSemitone * std::log2(pitchHz / kAnnotationsBaseFrequency);
}

// constrain_frequency: zero out note/onset columns outside [min,max] Hz.
void constrainFrequency(std::vector<float>& onsets, std::vector<float>& frames, int nFrames,
                        double minFreqHz, double maxFreqHz) {
    int minIdx = 0;
    int maxIdx = kNoteFreqBins;
    if (minFreqHz > 0.0) {
        minIdx = static_cast<int>(std::lround(hzToMidi(minFreqHz) - kMidiOffset));
    }
    if (maxFreqHz > 0.0) {
        maxIdx = static_cast<int>(std::lround(hzToMidi(maxFreqHz) - kMidiOffset));
    }
    minIdx = std::clamp(minIdx, 0, kNoteFreqBins);
    maxIdx = std::clamp(maxIdx, 0, kNoteFreqBins);
    for (int t = 0; t < nFrames; ++t) {
        float* on = &onsets[static_cast<size_t>(t) * kNoteFreqBins];
        float* fr = &frames[static_cast<size_t>(t) * kNoteFreqBins];
        for (int f = 0; f < minIdx; ++f) {
            on[f] = 0.0F;
            fr[f] = 0.0F;
        }
        for (int f = maxIdx; f < kNoteFreqBins; ++f) {
            on[f] = 0.0F;
            fr[f] = 0.0F;
        }
    }
}

// get_infered_onsets: augment predicted onsets with positive frame-energy
// jumps (min over n_diff in {1,2}), rescaled to the onset matrix's max.
std::vector<float> getInferredOnsets(const std::vector<float>& onsets,
                                     const std::vector<float>& frames, int nFrames) {
    constexpr int nDiff = 2;
    const size_t n = static_cast<size_t>(nFrames) * kNoteFreqBins;
    std::vector<float> frameDiff(n, 0.0F);

    for (int t = 0; t < nFrames; ++t) {
        for (int f = 0; f < kNoteFreqBins; ++f) {
            const size_t idx = static_cast<size_t>(t) * kNoteFreqBins + f;
            float minVal = 0.0F;
            bool first = true;
            for (int d = 1; d <= nDiff; ++d) {
                const float prev =
                    (t - d >= 0) ? frames[idx - static_cast<size_t>(d) * kNoteFreqBins] : 0.0F;
                const float diff = frames[idx] - prev;
                if (first || diff < minVal) {
                    minVal = diff;
                    first = false;
                }
            }
            frameDiff[idx] = minVal;
        }
    }

    // Clamp negatives to 0 and zero the first nDiff rows.
    for (float& v : frameDiff) {
        if (v < 0.0F) {
            v = 0.0F;
        }
    }
    for (int t = 0; t < std::min(nDiff, nFrames); ++t) {
        for (int f = 0; f < kNoteFreqBins; ++f) {
            frameDiff[static_cast<size_t>(t) * kNoteFreqBins + f] = 0.0F;
        }
    }

    float maxOnset = 0.0F;
    for (float v : onsets) {
        maxOnset = std::max(maxOnset, v);
    }
    float maxDiff = 0.0F;
    for (float v : frameDiff) {
        maxDiff = std::max(maxDiff, v);
    }
    const float scale = (maxDiff > 0.0F) ? (maxOnset / maxDiff) : 0.0F;

    std::vector<float> result(n, 0.0F);
    for (size_t i = 0; i < n; ++i) {
        result[i] = std::max(onsets[i], frameDiff[i] * scale);
    }
    return result;
}

float frameMean(const std::vector<float>& frames, int startFrame, int endFrame, int freqIdx) {
    if (endFrame <= startFrame) {
        return 0.0F;
    }
    double sum = 0.0;
    for (int t = startFrame; t < endFrame; ++t) {
        sum += static_cast<double>(frames[static_cast<size_t>(t) * kNoteFreqBins + freqIdx]);
    }
    return static_cast<float>(sum / (endFrame - startFrame));
}

// get_pitch_bends for a single note. Returns per-frame bend in contour bins.
std::vector<int> pitchBendsForNote(const std::vector<float>& contour, int startFrame, int endFrame,
                                   int pitchMidi, int nBinsTolerance) {
    const int windowLength = nBinsTolerance * 2 + 1;
    // gaussian(window_length, std=5) centered at nBinsTolerance.
    std::vector<double> gaussian(static_cast<size_t>(windowLength));
    const double sigma = 5.0;
    for (int i = 0; i < windowLength; ++i) {
        const double x = i - nBinsTolerance;
        gaussian[static_cast<size_t>(i)] = std::exp(-0.5 * (x * x) / (sigma * sigma));
    }

    const int freqIdx = static_cast<int>(std::lround(midiPitchToContourBin(pitchMidi)));
    const int freqStartIdx = std::max(freqIdx - nBinsTolerance, 0);
    const int freqEndIdx = std::min(kContourFreqBins, freqIdx + nBinsTolerance + 1);

    // Gaussian slice [gStart, gEnd) aligns with [freqStartIdx, freqEndIdx).
    const int gStart = std::max(0, nBinsTolerance - freqIdx);
    const int gEnd = windowLength - std::max(0, freqIdx - (kContourFreqBins - nBinsTolerance - 1));
    const int pbShift = nBinsTolerance - std::max(0, nBinsTolerance - freqIdx);

    const int nCols = freqEndIdx - freqStartIdx;
    std::vector<int> bends;
    bends.reserve(static_cast<size_t>(std::max(0, endFrame - startFrame)));

    for (int t = startFrame; t < endFrame; ++t) {
        const float* row = &contour[static_cast<size_t>(t) * kContourFreqBins];
        int bestCol = 0;
        float bestVal = -1.0F;
        for (int c = 0; c < nCols; ++c) {
            const int gIdx = gStart + c;
            const double g = (gIdx >= 0 && gIdx < gEnd) ? gaussian[static_cast<size_t>(gIdx)] : 0.0;
            const float val = static_cast<float>(static_cast<double>(row[freqStartIdx + c]) * g);
            if (val > bestVal) {
                bestVal = val;
                bestCol = c;
            }
        }
        bends.push_back(bestCol - pbShift);
    }
    return bends;
}

}  // namespace

std::vector<NoteEvent> decodeNotes(const ModelOutput& output, const PostprocessParams& params) {
    const int nFrames = output.nFrames;
    std::vector<NoteEvent> events;
    if (nFrames <= 1) {
        return events;
    }

    const int minNoteLen = static_cast<int>(
        std::lround(params.minNoteLenMs / 1000.0 * (static_cast<double>(kSampleRate) / kFftHop)));
    const int energyTol = params.energyTol;
    const float frameThresh = params.frameThresh;

    // Local copies we may mutate (constrain_frequency edits in place).
    std::vector<float> frames = output.note;
    std::vector<float> onsets = output.onset;
    constrainFrequency(onsets, frames, nFrames, params.minFreqHz, params.maxFreqHz);

    if (params.inferOnsets) {
        onsets = getInferredOnsets(onsets, frames, nFrames);
    }

    // argrelmax along time (axis 0), strict local maxima, then threshold.
    // Collect (frame, freq) onset peaks, iterated backwards in time.
    std::vector<std::pair<int, int>> onsetPeaks;
    for (int t = 1; t < nFrames - 1; ++t) {
        for (int f = 0; f < kNoteFreqBins; ++f) {
            const size_t idx = static_cast<size_t>(t) * kNoteFreqBins + f;
            const float v = onsets[idx];
            if (v > onsets[idx - kNoteFreqBins] && v > onsets[idx + kNoteFreqBins] &&
                v >= params.onsetThresh) {
                onsetPeaks.emplace_back(t, f);
            }
        }
    }
    // numpy's argrelmax returns row-major (time-major) order; reversing gives
    // backwards-in-time, ties broken by descending freq - matches the port.
    std::reverse(onsetPeaks.begin(), onsetPeaks.end());

    // remaining_energy starts as a copy of the (constrained) frame matrix.
    std::vector<float> remaining = frames;
    auto re = [&](int t, int f) -> float& {
        return remaining[static_cast<size_t>(t) * kNoteFreqBins + f];
    };

    for (const auto& [noteStartIdx, freqIdx] : onsetPeaks) {
        if (noteStartIdx >= nFrames - 1) {
            continue;
        }
        int i = noteStartIdx + 1;
        int k = 0;
        while (i < nFrames - 1 && k < energyTol) {
            if (re(i, freqIdx) < frameThresh) {
                ++k;
            } else {
                k = 0;
            }
            ++i;
        }
        i -= k;  // back up to the last frame above threshold

        if (i - noteStartIdx <= minNoteLen) {
            continue;
        }

        for (int t = noteStartIdx; t < i; ++t) {
            re(t, freqIdx) = 0.0F;
            if (freqIdx < kMaxFreqIdx) {
                re(t, freqIdx + 1) = 0.0F;
            }
            if (freqIdx > 0) {
                re(t, freqIdx - 1) = 0.0F;
            }
        }

        NoteEvent ev;
        ev.startFrame = noteStartIdx;
        ev.endFrame = i;
        ev.pitch = freqIdx + kMidiOffset;
        ev.amplitude = frameMean(frames, noteStartIdx, i, freqIdx);
        events.push_back(ev);
    }

    if (params.melodiaTrick) {
        for (;;) {
            // argmax over remaining energy.
            int iMid = 0;
            int freqIdx = 0;
            float best = frameThresh;
            bool found = false;
            for (int t = 0; t < nFrames; ++t) {
                const float* row = &remaining[static_cast<size_t>(t) * kNoteFreqBins];
                for (int f = 0; f < kNoteFreqBins; ++f) {
                    if (row[f] > best) {
                        best = row[f];
                        iMid = t;
                        freqIdx = f;
                        found = true;
                    }
                }
            }
            if (!found) {
                break;
            }

            re(iMid, freqIdx) = 0.0F;

            // forward pass
            int i = iMid + 1;
            int k = 0;
            while (i < nFrames - 1 && k < energyTol) {
                if (re(i, freqIdx) < frameThresh) {
                    ++k;
                } else {
                    k = 0;
                }
                re(i, freqIdx) = 0.0F;
                if (freqIdx < kMaxFreqIdx) {
                    re(i, freqIdx + 1) = 0.0F;
                }
                if (freqIdx > 0) {
                    re(i, freqIdx - 1) = 0.0F;
                }
                ++i;
            }
            const int iEnd = i - 1 - k;

            // backward pass
            i = iMid - 1;
            k = 0;
            while (i > 0 && k < energyTol) {
                if (re(i, freqIdx) < frameThresh) {
                    ++k;
                } else {
                    k = 0;
                }
                re(i, freqIdx) = 0.0F;
                if (freqIdx < kMaxFreqIdx) {
                    re(i, freqIdx + 1) = 0.0F;
                }
                if (freqIdx > 0) {
                    re(i, freqIdx - 1) = 0.0F;
                }
                --i;
            }
            const int iStart = i + 1 + k;

            if (iEnd - iStart <= minNoteLen) {
                continue;
            }

            NoteEvent ev;
            ev.startFrame = iStart;
            ev.endFrame = iEnd;
            ev.pitch = freqIdx + kMidiOffset;
            ev.amplitude = frameMean(frames, iStart, iEnd, freqIdx);
            events.push_back(ev);
        }
    }

    if (params.includePitchBends) {
        for (NoteEvent& ev : events) {
            ev.bends = pitchBendsForNote(output.contour, ev.startFrame, ev.endFrame, ev.pitch,
                                         params.nBinsTolerance);
        }
    }

    return events;
}

double frameToTimeSeconds(int frame) {
    const double originalTime = static_cast<double>(frame) * kFftHop / kSampleRate;
    const double windowNumber = std::floor(static_cast<double>(frame) / kAnnotNFrames);
    const double windowOffset =
        (static_cast<double>(kFftHop) / kSampleRate) *
            (kAnnotNFrames - static_cast<double>(kAudioNSamples) / kFftHop) +
        kMagicAlignmentOffset;
    return originalTime - windowOffset * windowNumber;
}

}  // namespace magda::transcription::basicpitch
