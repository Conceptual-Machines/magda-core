#include <catch2/catch_test_macros.hpp>
#include <memory>

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
    explicit ScopedStateProvider(RecordingStateProvider& provider)
        : provider_(provider),
          previous_(magda::PluginService::getInstance().useStateProvider(provider_)) {}

    ~ScopedStateProvider() {
        auto& service = magda::PluginService::getInstance();
        service.forgetStateProvider(provider_);
        if (previous_ != nullptr)
            service.useStateProvider(*previous_);
    }

    ScopedStateProvider(const ScopedStateProvider&) = delete;
    ScopedStateProvider& operator=(const ScopedStateProvider&) = delete;

  private:
    RecordingStateProvider& provider_;
    magda::PluginStateProvider* previous_ = nullptr;
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

TEST_CASE("PluginService state operations are silent without a renderer",
          "[plugin][state-service]") {
    auto& service = magda::PluginService::getInstance();
    RecordingStateProvider placeholder;
    ScopedStateProvider registration(placeholder);
    service.forgetStateProvider(placeholder);

    const auto path = magda::ChainNodePath::topLevelDevice(5, 13);
    service.captureAllPluginStates();
    service.capturePluginStateAt(path);
    service.applyPluginStateAt(path);

    CHECK(placeholder.captureAllCalls == 0);
    CHECK(placeholder.captureOneCalls == 0);
    CHECK(placeholder.applyOneCalls == 0);
}

TEST_CASE("PluginService never falls back to a replaced renderer", "[plugin][state-service]") {
    auto& service = magda::PluginService::getInstance();
    RecordingStateProvider first;
    ScopedStateProvider firstRegistration(first);
    RecordingStateProvider second;

    CHECK(service.useStateProvider(second) == &first);
    service.forgetStateProvider(second);
    service.captureAllPluginStates();

    CHECK(first.captureAllCalls == 0);
    CHECK(second.captureAllCalls == 0);

    // A provider also clears itself if its owner is destroyed without an explicit shutdown.
    auto temporary = std::make_unique<RecordingStateProvider>();
    CHECK(service.useStateProvider(*temporary) == nullptr);
    temporary.reset();
    service.captureAllPluginStates();
    CHECK(first.captureAllCalls == 0);
}
