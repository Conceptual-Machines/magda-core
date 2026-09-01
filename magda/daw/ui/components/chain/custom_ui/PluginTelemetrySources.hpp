#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "audio/plugins/LevelsPlugin.hpp"
#include "audio/plugins/OscilloscopePlugin.hpp"
#include "audio/plugins/SpectrumAnalyzerPlugin.hpp"
#include "audio/plugins/mutable/MutableCloudsPlugin.hpp"
#include "audio/plugins/tracktion/TracktionMagdaDevicePlugin.hpp"
#include "custom_ui/TelemetrySources.hpp"

namespace magda::daw::ui {

namespace te = tracktion::engine;

class OscilloscopePluginTelemetrySource final : public OscilloscopeTelemetrySource {
  public:
    explicit OscilloscopePluginTelemetrySource(te::Plugin::Ptr plugin)
        : plugin_(std::move(plugin)) {}

    size_t writePosition() const override {
        auto* p = plugin();
        return p != nullptr ? p->getTapBuffer().writePosition() : 0;
    }

    size_t readLatest(float* dest, int numSamples) const override {
        auto* p = plugin();
        return p != nullptr ? p->getTapBuffer().readLatest(dest, numSamples) : 0;
    }

    double sampleRate() const override {
        auto* p = plugin();
        return p != nullptr ? p->getSampleRate() : 44100.0;
    }

    int traceColourIndex() const override {
        auto* p = plugin();
        return p != nullptr ? p->getTraceColourIndex() : 0;
    }

    void setTraceColourIndex(int index) override {
        if (auto* p = plugin())
            p->setTraceColourIndex(index);
    }

    float timebaseMs() const override {
        auto* p = plugin();
        return p != nullptr ? p->getTimebaseMs() : 10.0f;
    }

    void setTimebaseMs(float ms) override {
        if (auto* p = plugin())
            p->setTimebaseMs(ms);
    }

  private:
    daw::audio::OscilloscopePlugin* plugin() const {
        return daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::OscilloscopePlugin>(
            plugin_.get());
    }

    te::Plugin::Ptr plugin_;
};

class SpectrumPluginTelemetrySource final : public SpectrumTelemetrySource {
  public:
    explicit SpectrumPluginTelemetrySource(te::Plugin::Ptr plugin) : plugin_(std::move(plugin)) {}

    size_t writePosition() const override {
        auto* p = plugin();
        return p != nullptr ? p->getTapBuffer().writePosition() : 0;
    }

    size_t readLatest(float* dest, int numSamples) const override {
        auto* p = plugin();
        return p != nullptr ? p->getTapBuffer().readLatest(dest, numSamples) : 0;
    }

    double sampleRate() const override {
        auto* p = plugin();
        return p != nullptr ? p->getSampleRate() : 44100.0;
    }

    int traceColourIndex() const override {
        auto* p = plugin();
        return p != nullptr ? p->getTraceColourIndex() : 0;
    }

    void setTraceColourIndex(int index) override {
        if (auto* p = plugin())
            p->setTraceColourIndex(index);
    }

    int fftOrder() const override {
        auto* p = plugin();
        return p != nullptr ? p->getFftOrder() : 11;
    }

    void setFftOrder(int order) override {
        if (auto* p = plugin())
            p->setFftOrder(order);
    }

    float slopeDbPerOct() const override {
        auto* p = plugin();
        return p != nullptr ? p->getSlopeDbPerOct() : 4.5f;
    }

    void setSlopeDbPerOct(float slope) override {
        if (auto* p = plugin())
            p->setSlopeDbPerOct(slope);
    }

    float smoothing() const override {
        auto* p = plugin();
        return p != nullptr ? p->getSmoothing() : 0.5f;
    }

    void setSmoothing(float smoothing) override {
        if (auto* p = plugin())
            p->setSmoothing(smoothing);
    }

  private:
    daw::audio::SpectrumAnalyzerPlugin* plugin() const {
        return daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::SpectrumAnalyzerPlugin>(
            plugin_.get());
    }

    te::Plugin::Ptr plugin_;
};

class LevelsPluginTelemetrySource final : public LevelsTelemetrySource {
  public:
    explicit LevelsPluginTelemetrySource(te::Plugin::Ptr plugin) : plugin_(std::move(plugin)) {}

    void setActive(bool active) override {
        if (auto* p = plugin())
            p->setActive(active);
    }

    void requestReset() override {
        if (auto* p = plugin())
            p->requestReset();
    }

    daw::audio::TrackMeasurementSnapshot snapshot() const override {
        auto* p = plugin();
        return p != nullptr ? p->getSnapshot() : daw::audio::TrackMeasurementSnapshot{};
    }

  private:
    daw::audio::LevelsPlugin* plugin() const {
        return dynamic_cast<daw::audio::LevelsPlugin*>(plugin_.get());
    }

    te::Plugin::Ptr plugin_;
};

class NimbusPluginTelemetrySource final : public NimbusTelemetrySource {
  public:
    explicit NimbusPluginTelemetrySource(te::Plugin::Ptr plugin) : plugin_(std::move(plugin)) {}

    size_t inputEnvelopeWritePosition() const override {
        auto* p = plugin();
        return p != nullptr ? p->inputEnvelopeTap().writePosition() : 0;
    }

    size_t readInputEnvelope(float* dest, int numSamples) const override {
        auto* p = plugin();
        return p != nullptr ? p->inputEnvelopeTap().readLatest(dest, numSamples) : 0;
    }

  private:
    daw::audio::MutableCloudsPlugin* plugin() const {
        return daw::audio::tracktion_adapter::deviceFromPlugin<daw::audio::MutableCloudsPlugin>(
            plugin_.get());
    }

    te::Plugin::Ptr plugin_;
};

}  // namespace magda::daw::ui
