#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "audio/plugins/LevelsPlugin.hpp"
#include "custom_ui/TelemetrySources.hpp"

namespace magda::daw::ui {

namespace te = tracktion::engine;

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

}  // namespace magda::daw::ui
