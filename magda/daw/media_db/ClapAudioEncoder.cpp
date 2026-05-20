#include "ClapAudioEncoder.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "MelSpectrogram.hpp"

namespace magda::media {

struct ClapAudioEncoder::Impl {
    Ort::Env env;
    Ort::SessionOptions sessionOptions;
    Ort::Session session{nullptr};
    Ort::MemoryInfo memoryInfo;
    std::string inputName;
    std::string outputName;
    MelConfig melCfg;
    int outputDim = 512;
    std::string modelId;

    explicit Impl(const std::filesystem::path& modelPath)
        : env(ORT_LOGGING_LEVEL_WARNING, "magda-clap"),
          memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
        // Keep CLAP inference predictable on older CPUs. The app already
        // schedules embedding work on its own background thread; letting ORT
        // create extra per-session worker pools can oversubscribe small
        // machines and make cancellation feel worse.
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetInterOpNumThreads(1);
        session = Ort::Session(env, modelPath.c_str(), sessionOptions);

        Ort::AllocatorWithDefaultOptions allocator;

        auto inName = session.GetInputNameAllocated(0, allocator);
        inputName = inName.get();
        auto outName = session.GetOutputNameAllocated(0, allocator);
        outputName = outName.get();

        // Output is [batch, dim]; the last dim is the embedding size.
        auto outShape = session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (!outShape.empty() && outShape.back() > 0) {
            outputDim = static_cast<int>(outShape.back());
        }

        modelId = modelPath.stem().string();
    }
};

// ---- ctors / dtor ---------------------------------------------------------

ClapAudioEncoder::ClapAudioEncoder(const std::filesystem::path& modelPath) {
    if (!std::filesystem::exists(modelPath)) {
        throw ClapEncoderError("CLAP audio model not found: " + modelPath.string());
    }
    try {
        impl_ = std::make_unique<Impl>(modelPath);
    } catch (const Ort::Exception& e) {
        throw ClapEncoderError(std::string("Ort failed to load model: ") + e.what());
    }
}

ClapAudioEncoder::~ClapAudioEncoder() = default;
ClapAudioEncoder::ClapAudioEncoder(ClapAudioEncoder&&) noexcept = default;
ClapAudioEncoder& ClapAudioEncoder::operator=(ClapAudioEncoder&&) noexcept = default;

// ---- getters --------------------------------------------------------------

int ClapAudioEncoder::dim() const noexcept {
    return impl_ ? impl_->outputDim : 512;
}

int ClapAudioEncoder::sampleRate() const noexcept {
    return impl_ ? impl_->melCfg.sampleRate : 48000;
}

const std::string& ClapAudioEncoder::modelId() const noexcept {
    static const std::string kEmpty;
    return impl_ ? impl_->modelId : kEmpty;
}

void ClapAudioEncoder::setModelId(std::string id) {
    if (impl_) {
        impl_->modelId = std::move(id);
    }
}

// ---- embed ----------------------------------------------------------------

std::vector<float> ClapAudioEncoder::embed(const float* mono, int numSamples) {
    if (!impl_ || mono == nullptr || numSamples <= 0) {
        return {};
    }
    const auto& cfg = impl_->melCfg;
    const int chunkSamples = cfg.targetSamples;
    const int numChunks = std::max(1, (numSamples + chunkSamples - 1) / chunkSamples);
    const int nFrames = cfg.targetSamples / cfg.hopLength + 1;

    std::vector<float> accum(impl_->outputDim, 0.0F);

    for (int c = 0; c < numChunks; ++c) {
        const int offset = c * chunkSamples;
        const int chunkLen = std::min(chunkSamples, numSamples - offset);

        // mel comes back as [n_mels, n_frames]; the model wants
        // [batch=1, channels=1, n_frames, n_mels]. Transpose.
        const auto mel = computeLogMel(mono + offset, chunkLen, cfg);
        std::vector<float> input(static_cast<size_t>(cfg.nMels) * nFrames, 0.0F);
        for (int t = 0; t < nFrames; ++t) {
            for (int m = 0; m < cfg.nMels; ++m) {
                input[static_cast<size_t>(t) * cfg.nMels + m] =
                    mel[static_cast<size_t>(m) * nFrames + t];
            }
        }

        std::array<int64_t, 4> shape{1, 1, static_cast<int64_t>(nFrames),
                                     static_cast<int64_t>(cfg.nMels)};
        auto tensor = Ort::Value::CreateTensor<float>(impl_->memoryInfo, input.data(), input.size(),
                                                      shape.data(), shape.size());

        const char* inputNames[] = {impl_->inputName.c_str()};
        const char* outputNames[] = {impl_->outputName.c_str()};
        Ort::RunOptions runOpts;

        std::vector<Ort::Value> outputs;
        try {
            outputs = impl_->session.Run(runOpts, inputNames, &tensor, 1, outputNames, 1);
        } catch (const Ort::Exception& e) {
            throw ClapEncoderError(std::string("Ort inference failed: ") + e.what());
        }
        if (outputs.empty()) {
            throw ClapEncoderError("Ort produced no output");
        }

        const float* outData = outputs[0].GetTensorData<float>();
        float sumSq = 0.0F;
        for (int i = 0; i < impl_->outputDim; ++i) {
            sumSq += outData[i] * outData[i];
        }
        const float invNorm = sumSq > 0.0F ? 1.0F / std::sqrt(sumSq) : 0.0F;
        for (int i = 0; i < impl_->outputDim; ++i) {
            accum[i] += outData[i] * invNorm;
        }
    }

    // Renormalize the averaged embedding so cosine similarity is a dot product.
    float sumSq = 0.0F;
    for (float v : accum) {
        sumSq += v * v;
    }
    const float invNorm = sumSq > 0.0F ? 1.0F / std::sqrt(sumSq) : 0.0F;
    for (float& v : accum) {
        v *= invNorm;
    }
    return accum;
}

}  // namespace magda::media
