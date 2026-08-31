#include <catch2/catch_test_macros.hpp>

#include "../magda/daw/core/DeviceUiContext.hpp"

using namespace magda;

namespace {

struct DummyParameterController final : DeviceParameterController {
    std::vector<ParameterInfo> parameters() const override {
        return {};
    }

    const ParameterInfo* findParameterByIndex(int) const override {
        return nullptr;
    }

    void setParameterNormalised(int, float) override {}
};

struct DummyStateController final : DeviceStateController {
    juce::var getStateValue(const juce::Identifier&) const override {
        return {};
    }

    void setStateValue(const juce::Identifier&, const juce::var&) override {}
};

struct DummyCommandController final : DeviceCommandController {
    juce::var executeCommand(const juce::Identifier&, const juce::var& = {}) override {
        return true;
    }
};

struct DummyTelemetrySource final : DeviceTelemetrySource {
    juce::String telemetryKey() const override {
        return "dummy";
    }
};

DeviceInfo makeDevice(DeviceId id) {
    DeviceInfo device;
    device.id = id;
    device.name = "Test Device";
    return device;
}

}  // namespace

TEST_CASE("BasicDeviceUiContext invalidates lifetime token and clears adapters",
          "[device-ui-context][lifetime]") {
    BasicDeviceUiContext context(makeDevice(42), ChainNodePath::topLevelDevice(1, 42));
    auto token = context.lifetimeToken();

    context.setParameterController(std::make_shared<DummyParameterController>());
    context.setStateController(std::make_shared<DummyStateController>());
    context.setCommandController(std::make_shared<DummyCommandController>());
    context.setTelemetrySource(std::make_shared<DummyTelemetrySource>());

    REQUIRE(context.isValid());
    REQUIRE(token->isValid());
    REQUIRE(context.parameters() != nullptr);
    REQUIRE(context.state() != nullptr);
    REQUIRE(context.commands() != nullptr);
    REQUIRE(context.telemetry("dummy") != nullptr);

    context.invalidate();

    REQUIRE_FALSE(context.isValid());
    REQUIRE_FALSE(token->isValid());
    REQUIRE(context.parameters() == nullptr);
    REQUIRE(context.state() == nullptr);
    REQUIRE(context.commands() == nullptr);
    REQUIRE(context.telemetry("dummy") == nullptr);
}

TEST_CASE("BasicDeviceUiContext derives device id from path once bound", "[device-ui-context]") {
    BasicDeviceUiContext context(makeDevice(42));

    REQUIRE(context.deviceId() == 42);
    REQUIRE_FALSE(context.isValid());

    context.setPath(ChainNodePath::topLevelDevice(7, 99));

    REQUIRE(context.deviceId() == 99);
    REQUIRE(context.path() == ChainNodePath::topLevelDevice(7, 99));
    REQUIRE(context.isValid());
}
