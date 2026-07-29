#include <catch2/catch_test_macros.hpp>
#include <utility>

#include "audio/plugins/DeviceParameterHandle.hpp"
#include "audio/plugins/DevicePluginHandle.hpp"

namespace audio = magda::daw::audio;

TEST_CASE("DeviceParameterHandle delegates without exposing a host parameter type",
          "[device-sdk]") {
    struct Parameter {
        float base = 0.25f;
        float current = 0.5f;
    } parameter;

    const audio::DeviceParameterHandle handle{
        &parameter,
        [](const void* native) { return static_cast<const Parameter*>(native)->base; },
        [](const void* native) { return static_cast<const Parameter*>(native)->current; },
        [](void* native, float value) { static_cast<Parameter*>(native)->current = value; },
    };

    REQUIRE(handle);
    CHECK(handle.currentBaseValue() == 0.25f);
    CHECK(handle.currentValue() == 0.5f);

    handle.setValueFromHost(0.75f);
    CHECK(parameter.current == 0.75f);
}

TEST_CASE("DevicePluginPtr preserves adapter-managed ownership across copies and moves",
          "[device-sdk]") {
    struct Plugin {
        int references = 0;
    } plugin;

    const auto retain = [](void* native) { ++static_cast<Plugin*>(native)->references; };
    const auto release = [](void* native) { --static_cast<Plugin*>(native)->references; };

    {
        audio::DevicePluginPtr first(&plugin, retain, release);
        REQUIRE(first);
        CHECK(plugin.references == 1);

        {
            auto second = first;
            CHECK(plugin.references == 2);

            auto third = std::move(second);
            CHECK_FALSE(second);
            CHECK(third.ref().nativeHandle() == &plugin);
            CHECK(plugin.references == 2);
        }

        CHECK(plugin.references == 1);
        first.reset();
        CHECK(plugin.references == 0);
    }

    CHECK(plugin.references == 0);
}
