// Mel spectrogram preprocessing for CLAP (issue #768).
//
// CLAP's ONNX audio encoder takes a (batch, 1, time_frames, n_mels) tensor of
// log-mel features, not raw audio. The mel parameters here mirror Hugging
// Face's ClapFeatureExtractor config so the C++ runtime and the Python
// prototype produce equivalent inputs to the same model.

#pragma once

#include <cstdint>
#include <vector>

namespace magda::media {

// HTSAT-CLAP feature extractor parameters (from
// https://huggingface.co/laion/clap-htsat-unfused/blob/main/preprocessor_config.json).
struct MelConfig {
    int sampleRate = 48000;
    int nFft = 1024;
    int hopLength = 480;  // 10 ms at 48 kHz
    int nMels = 64;
    float fMin = 50.0F;
    float fMax = 14000.0F;
    int targetSamples = 480000;  // 10 s chunk, what the model expects
};

// Build the mel filterbank as a (n_mels, n_fft/2 + 1) matrix in row-major
// order. Triangular filters spaced on the HTK mel scale; same construction
// as torchaudio / librosa with htk=True, norm=None.
//
// Exposed for testing parity against reference implementations.
std::vector<float> buildMelFilterbank(const MelConfig& cfg);

// Compute log-mel spectrogram from a single chunk of `targetSamples` mono
// samples at `cfg.sampleRate`. Output is `n_mels * num_time_frames` floats
// in row-major order matching torch tensor `[batch=1, channels=1, time, mels]`
// after transposition. The caller assembles batch tensors from these.
//
// Implementation: Hann-windowed STFT -> power spectrum -> mel filterbank
// -> log(eps + x). Time frames are `targetSamples / hopLength + 1` for the
// canonical CLAP 10-s window (1001 frames).
std::vector<float> computeLogMel(const float* mono, int numSamples, const MelConfig& cfg);

}  // namespace magda::media
