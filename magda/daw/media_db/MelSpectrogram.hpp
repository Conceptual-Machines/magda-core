// Mel spectrogram preprocessing for CLAP (issue #768).
//
// CLAP's ONNX audio encoder takes a (batch, 1, time_frames, n_mels) tensor of
// log-mel features, not raw audio. The mel parameters here mirror Hugging
// Face's ClapFeatureExtractor config so the C++ runtime and the Python
// prototype produce equivalent inputs to the same model.

#pragma once

#include "magda/sdk/analysis/LogMel.hpp"

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

/// HTK mel scale, power spectrum, zero-padded centred frames, log(x + 1e-10) (magda-sdk
/// docs/measurement.md). A chunk is padded to targetSamples, so targetSamples / hopLength + 1
/// frames.
inline sdk::LogMelConfig clapLogMelConfig(const MelConfig& cfg) {
    sdk::LogMelConfig config;
    config.sampleRate = cfg.sampleRate;
    config.fftSize = cfg.nFft;
    config.hopSize = cfg.hopLength;
    config.numMels = cfg.nMels;
    config.fMin = cfg.fMin;
    config.fMax = cfg.fMax;
    config.scale = sdk::MelScale::Htk;
    config.spectrum = sdk::MelSpectrum::Power;
    config.normaliseWindow = true;
    config.padding = sdk::MelPadding::Zero;
    config.compression = sdk::MelCompression::Log;
    config.logOffset = 1e-10;
    return config;
}

}  // namespace magda::media
