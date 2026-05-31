#include <juce_core/juce_core.h>

#include <cstdlib>

#include "magda/daw/core/AppPaths.hpp"
#include "magda/daw/core/Config.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/ui/dialogs/ParameterConfigDialog.hpp"

namespace {

void setEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unsetEnv(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

struct DataDirScope {
    DataDirScope() {
        if (const char* value = std::getenv("MAGDA_DATA_DIR"))
            previousDataDir = juce::String::fromUTF8(value);

        juce::File tempRoot(
            std::getenv("TMPDIR") != nullptr
                ? juce::String::fromUTF8(std::getenv("TMPDIR"))
                : juce::File::getSpecialLocation(juce::File::tempDirectory).getFullPathName());
        root = tempRoot.getNonexistentChildFile("magda-param-config-test", {});
        root.createDirectory();
        setEnv("MAGDA_DATA_DIR", root.getFullPathName().toRawUTF8());
        magda::Config::getInstance().setDataDir({});
        magda::paths::resolve();
    }

    ~DataDirScope() {
        if (previousDataDir.isEmpty())
            unsetEnv("MAGDA_DATA_DIR");
        else
            setEnv("MAGDA_DATA_DIR", previousDataDir.toRawUTF8());

        magda::Config::getInstance().setDataDir({});
        magda::paths::resolve();
        root.deleteRecursively();
    }

    juce::String previousDataDir;
    juce::File root;
};

bool writeParameterConfig(const juce::String& uniqueId) {
    auto configDir = magda::paths::pluginConfigsDir();
    if (!configDir.createDirectory())
        return false;

    auto configFile =
        configDir.getChildFile(uniqueId.replaceCharacters(":/\\,; ", "______") + ".xml");

    juce::XmlElement root("ParameterConfig");
    root.setAttribute("pluginId", uniqueId);
    auto* params = root.createNewChildElement("Parameters");
    for (int i = 0; i < 3; ++i) {
        auto* param = params->createNewChildElement("Param");
        param->setAttribute("index", i);
        param->setAttribute("name", juce::String("Param ") + juce::String(i));
        param->setAttribute("visible", false);
        param->setAttribute("mini", i == 1 || i == 2);
        param->setAttribute("unit", "%");
        param->setAttribute("scale", "linear");
        param->setAttribute("min", 0.0);
        param->setAttribute("max", 1.0);
        param->setAttribute("center", 0.5);
    }
    return configFile.replaceWithText(root.toString());
}

magda::DeviceInfo makeConfiguredDevice(const juce::String& uniqueId) {
    magda::DeviceInfo device;
    device.name = "Configured";
    device.pluginId = "configured-plugin";
    device.uniqueId = uniqueId;
    for (int i = 0; i < 3; ++i) {
        magda::ParameterInfo param;
        param.paramIndex = 10 + i;
        param.name = juce::String("Param ") + juce::String(i);
        param.currentValue = 0.25f * static_cast<float>(i);
        device.parameters.push_back(param);
    }
    return device;
}

class DeviceChangeListener final : public magda::TrackManagerListener {
  public:
    void tracksChanged() override {}
    void trackDevicesChanged(magda::TrackId trackId) override {
        changedTrackIds.push_back(trackId);
    }

    std::vector<magda::TrackId> changedTrackIds;
};

}  // namespace

class ParameterConfigDialogTest final : public juce::UnitTest {
  public:
    ParameterConfigDialogTest() : juce::UnitTest("Parameter Config Dialog", "magda") {}

    void runTest() override {
        beginTest("Plugin-browser Mini FX config is applied to live mixer devices");

        DataDirScope dataDir;
        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();

        const juce::String uniqueId = "test-plugin-browser-mini-fx";
        expect(writeParameterConfig(uniqueId), "Test parameter config should be writable");

        auto probe = makeConfiguredDevice(uniqueId);
        expect(magda::daw::ui::ParameterConfigDialog::applyConfigToDevice(uniqueId, probe),
               "Saved config should be readable by unique ID");
        expect(probe.miniMixerParameters == std::vector<int>({1, 2}),
               "Saved config should decode Mini FX parameter indices");

        const auto trackId = trackManager.createTrack("Track");
        const auto deviceId =
            trackManager.addDeviceToTrack(trackId, makeConfiguredDevice(uniqueId));
        const auto path = magda::ChainNodePath::topLevelDevice(trackId, deviceId);

        auto* live = trackManager.getDeviceInChainByPath(path);
        expect(live != nullptr, "Live device should exist");
        if (live == nullptr)
            return;

        expect(live->miniMixerParameters.empty(),
               "Device starts without a per-instance Mini FX selection");

        DeviceChangeListener listener;
        trackManager.addListener(&listener);
        magda::daw::ui::ParameterConfigDialog::refreshLiveDevicesForParameterConfigForTest(
            uniqueId);
        trackManager.removeListener(&listener);

        expect(live->miniMixerParameters == std::vector<int>({1, 2}),
               "Saved Mini FX selection should be copied onto the live device");
        expect(!listener.changedTrackIds.empty() && listener.changedTrackIds.front() == trackId,
               "Applying the config should notify the mixer to rebuild stale mini rows");

        trackManager.clearAllTracks();
    }
};

static ParameterConfigDialogTest parameterConfigDialogTest;
