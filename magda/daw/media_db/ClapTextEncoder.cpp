#include "ClapTextEncoder.hpp"

#if defined(MAGDA_HAVE_CLAP) && MAGDA_HAVE_CLAP

#include <onnxruntime_cxx_api.h>

#include <array>
#include <cmath>
#include <vector>

namespace magda::media {

struct ClapTextEncoder::Impl {
    Ort::Env env;
    Ort::SessionOptions sessionOptions;
    Ort::Session session{nullptr};
    Ort::MemoryInfo memoryInfo;
    std::vector<std::string> inputNames;  // [input_ids, attention_mask] order
    std::string outputName;
    int outputDim = 512;
    std::string modelId;

    explicit Impl(const std::filesystem::path& modelPath)
        : env(ORT_LOGGING_LEVEL_WARNING, "magda-clap-text"),
          memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
        // Match the audio encoder: keep one predictable ORT worker per model
        // session instead of stacking internal ORT pools on top of app-level
        // background jobs.
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetInterOpNumThreads(1);
        session = Ort::Session(env, modelPath.c_str(), sessionOptions);

        Ort::AllocatorWithDefaultOptions allocator;

        const size_t nInputs = session.GetInputCount();
        inputNames.reserve(nInputs);
        for (size_t i = 0; i < nInputs; ++i) {
            inputNames.emplace_back(session.GetInputNameAllocated(i, allocator).get());
        }

        auto outName = session.GetOutputNameAllocated(0, allocator);
        outputName = outName.get();

        auto outShape = session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (!outShape.empty() && outShape.back() > 0) {
            outputDim = static_cast<int>(outShape.back());
        }

        modelId = modelPath.stem().string();
    }
};

ClapTextEncoder::ClapTextEncoder(const std::filesystem::path& modelPath) {
    if (!std::filesystem::exists(modelPath)) {
        throw ClapTextEncoderError("CLAP text model not found: " + modelPath.string());
    }
    try {
        impl_ = std::make_unique<Impl>(modelPath);
    } catch (const Ort::Exception& e) {
        throw ClapTextEncoderError(std::string("Ort failed to load model: ") + e.what());
    }
    if (impl_->inputNames.size() < 2) {
        throw ClapTextEncoderError(
            "CLAP text model has unexpected input arity (expected input_ids + "
            "attention_mask, got " +
            std::to_string(impl_->inputNames.size()) + ")");
    }
}

ClapTextEncoder::~ClapTextEncoder() = default;
ClapTextEncoder::ClapTextEncoder(ClapTextEncoder&&) noexcept = default;
ClapTextEncoder& ClapTextEncoder::operator=(ClapTextEncoder&&) noexcept = default;

int ClapTextEncoder::dim() const noexcept {
    return impl_ ? impl_->outputDim : 512;
}

const std::string& ClapTextEncoder::modelId() const noexcept {
    static const std::string kEmpty;
    return impl_ ? impl_->modelId : kEmpty;
}

void ClapTextEncoder::setModelId(std::string id) {
    if (impl_) {
        impl_->modelId = std::move(id);
    }
}

std::vector<float> ClapTextEncoder::embedTokens(const std::vector<int64_t>& inputIds,
                                                const std::vector<int64_t>& attentionMask) {
    if (!impl_) {
        return {};
    }
    if (inputIds.size() != attentionMask.size() || inputIds.empty()) {
        throw ClapTextEncoderError("inputIds and attentionMask must be the same non-zero length");
    }

    const int64_t seqLen = static_cast<int64_t>(inputIds.size());
    const std::array<int64_t, 2> shape{1, seqLen};

    // ORT's input order matches what session.GetInputNameAllocated returned;
    // for CLAP/RoBERTa this is [input_ids, attention_mask]. We don't reorder
    // by name because both ints are 64-bit and order is contract.
    std::vector<int64_t> idsBuf = inputIds;
    std::vector<int64_t> maskBuf = attentionMask;

    auto idsTensor = Ort::Value::CreateTensor<int64_t>(impl_->memoryInfo, idsBuf.data(),
                                                       idsBuf.size(), shape.data(), shape.size());
    auto maskTensor = Ort::Value::CreateTensor<int64_t>(impl_->memoryInfo, maskBuf.data(),
                                                        maskBuf.size(), shape.data(), shape.size());

    std::array<Ort::Value, 2> inputs{std::move(idsTensor), std::move(maskTensor)};
    std::array<const char*, 2> inputNames{impl_->inputNames[0].c_str(),
                                          impl_->inputNames[1].c_str()};
    std::array<const char*, 1> outputNames{impl_->outputName.c_str()};
    Ort::RunOptions runOpts;

    std::vector<Ort::Value> outputs;
    try {
        outputs = impl_->session.Run(runOpts, inputNames.data(), inputs.data(), inputs.size(),
                                     outputNames.data(), outputNames.size());
    } catch (const Ort::Exception& e) {
        throw ClapTextEncoderError(std::string("Ort inference failed: ") + e.what());
    }
    if (outputs.empty()) {
        throw ClapTextEncoderError("Ort produced no output");
    }

    const float* outData = outputs[0].GetTensorData<float>();
    std::vector<float> result(outData, outData + impl_->outputDim);

    // The exported text encoder already L2-normalizes via the wrapper from
    // onnx_export.py, but renormalize defensively in case the model is
    // ever swapped for a variant that doesn't.
    float sumSq = 0.0F;
    for (float v : result) {
        sumSq += v * v;
    }
    const float invNorm = sumSq > 0.0F ? 1.0F / std::sqrt(sumSq) : 0.0F;
    for (float& v : result) {
        v *= invNorm;
    }
    return result;
}

}  // namespace magda::media

#else  // MAGDA_HAVE_CLAP

// Stub implementation for builds without ONNX Runtime (currently Intel macOS).
// MediaDbContext's encoder factory catches the ctor throw and leaves the
// encoder pointer null; downstream callers already null-check, so embedTokens
// is unreachable in practice.
// NOLINTBEGIN(readability-convert-member-functions-to-static,performance-unnecessary-value-param,hicpp-named-parameter)
namespace magda::media {

struct ClapTextEncoder::Impl {};

ClapTextEncoder::ClapTextEncoder(const std::filesystem::path& /*modelPath*/) {
    throw ClapTextEncoderError("CLAP backend not available on this build");
}
ClapTextEncoder::~ClapTextEncoder() = default;
ClapTextEncoder::ClapTextEncoder(ClapTextEncoder&&) noexcept = default;
ClapTextEncoder& ClapTextEncoder::operator=(ClapTextEncoder&&) noexcept = default;

int ClapTextEncoder::dim() const noexcept { return 512; }

const std::string& ClapTextEncoder::modelId() const noexcept {
    static const std::string kEmpty;
    return kEmpty;
}
void ClapTextEncoder::setModelId(std::string /*id*/) {}

std::vector<float> ClapTextEncoder::embedTokens(const std::vector<int64_t>& /*inputIds*/,
                                                const std::vector<int64_t>& /*attentionMask*/) {
    return {};
}

}  // namespace magda::media
// NOLINTEND(readability-convert-member-functions-to-static,performance-unnecessary-value-param,hicpp-named-parameter)

#endif  // MAGDA_HAVE_CLAP
