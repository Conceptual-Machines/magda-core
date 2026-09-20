#include <catch2/catch_test_macros.hpp>

#include "magda/daw/core/ChainNodePath.hpp"
#include "magda/daw/engine/PluginService.hpp"

namespace {

class RecordingStateProvider final : public magda::PluginStateProvider {
  public:
    void captureAllPluginStates() override {
        ++captureAllCalls;
    }

    void capturePluginStateAt(const magda::ChainNodePath& devicePath) override {
        ++captureOneCalls;
        lastCaptured = devicePath;
    }

    void applyPluginStateAt(const magda::ChainNodePath& devicePath) override {
        ++applyOneCalls;
        lastApplied = devicePath;
    }

    int captureAllCalls = 0;
    int captureOneCalls = 0;
    int applyOneCalls = 0;
    magda::ChainNodePath lastCaptured;
    magda::ChainNodePath lastApplied;
};

class ScopedStateProvider {
  public:
    explicit ScopedStateProvider(RecordingStateProvider& provider) : provider_(provider) {
        magda::PluginService::getInstance().useStateProvider(provider_);
    }

    ~ScopedStateProvider() {
        magda::PluginService::getInstance().forgetStateProvider(provider_);
    }

    ScopedStateProvider(const ScopedStateProvider&) = delete;
    ScopedStateProvider& operator=(const ScopedStateProvider&) = delete;

  private:
    RecordingStateProvider& provider_;
};

}  // namespace

TEST_CASE("PluginService sends state operations to the current renderer",
          "[plugin][state-service]") {
    auto& service = magda::PluginService::getInstance();
    RecordingStateProvider first;
    ScopedStateProvider firstRegistration(first);

    const auto firstPath = magda::ChainNodePath::topLevelDevice(2, 7);
    service.captureAllPluginStates();
    service.capturePluginStateAt(firstPath);

    CHECK(first.captureAllCalls == 1);
    CHECK(first.captureOneCalls == 1);
    CHECK(first.lastCaptured == firstPath);

    {
        RecordingStateProvider second;
        ScopedStateProvider secondRegistration(second);
        const auto secondPath = magda::ChainNodePath::topLevelDevice(3, 9);

        service.captureAllPluginStates();
        service.applyPluginStateAt(secondPath);

        CHECK(second.captureAllCalls == 1);
        CHECK(second.applyOneCalls == 1);
        CHECK(second.lastApplied == secondPath);
        CHECK(first.captureAllCalls == 1);
    }

    const auto restoredPath = magda::ChainNodePath::topLevelDevice(4, 11);
    service.applyPluginStateAt(restoredPath);

    CHECK(first.applyOneCalls == 1);
    CHECK(first.lastApplied == restoredPath);
}
