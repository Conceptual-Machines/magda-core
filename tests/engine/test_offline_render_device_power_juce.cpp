#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

#include "JuceTestStateGuard.hpp"
#include "SharedTestEngine.hpp"
#include "magda/daw/audio/AudioBridge.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/OfflineRenderHelper.hpp"

namespace {

magda::DeviceInfo makeInternalDevice(const juce::String& name, const juce::String& pluginId) {
    magda::DeviceInfo device;
    device.name = name;
    device.format = magda::PluginFormat::Internal;
    device.pluginId = pluginId;
    return device;
}

}  // namespace

/**
 * @brief Powered-off devices must survive an offline render (#1880)
 *
 * The export prep used to enable every plugin on every audio track so the test
 * tone generator would sound offline. That blanket write rendered bypassed
 * devices into the file and left them running afterwards, while the UI power
 * button still read "off" because the model was never touched.
 */
class OfflineRenderDevicePowerTest final : public juce::UnitTest {
  public:
    OfflineRenderDevicePowerTest() : juce::UnitTest("Offline Render Device Power Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testBypassedDeviceStaysBypassed(); });
        magda::test::runWithCleanJuceState([this] { testChainPowerOffStaysOff(); });
    }

  private:
    void testBypassedDeviceStaysBypassed() {
        beginTest("Per-device power off survives render prep");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        auto* edit = wrapper.getEdit();
        expect(bridge != nullptr && edit != nullptr, "AudioBridge and Edit must exist");
        if (!bridge || !edit)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("Render power");
        const auto fxId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("FX Filter", "magda_filter"));
        const auto fxPath = magda::ChainNodePath::topLevelDevice(trackId, fxId);

        auto plugin = bridge->getPlugin(fxPath);
        expect(plugin != nullptr, "Internal FX should have a TE plugin");
        if (plugin == nullptr) {
            trackManager.clearAllTracks();
            return;
        }

        expect(plugin->isEnabled(), "Freshly added device should be enabled");

        // The power button in the device header / mixer mini chain goes through
        // this setter, which is what pushes enablement into the engine.
        trackManager.setDeviceInChainBypassedByPath(fxPath, true);
        expect(!plugin->isEnabled(), "Powering the device off should disable the TE plugin");

        magda::prepareEditForOfflineRender(*edit);

        expect(!plugin->isEnabled(),
               "Render prep must leave a powered-off device disabled (#1880)");

        auto* device = trackManager.getDeviceInChainByPath(fxPath);
        expect(device != nullptr && device->bypassed,
               "Render prep must not touch the model's bypassed flag");

        trackManager.clearAllTracks();
    }

    void testChainPowerOffStaysOff() {
        beginTest("Track chain power off survives render prep");

        auto& wrapper = magda::test::getSharedEngine();
        auto* bridge = wrapper.getAudioBridge();
        auto* edit = wrapper.getEdit();
        expect(bridge != nullptr && edit != nullptr, "AudioBridge and Edit must exist");
        if (!bridge || !edit)
            return;

        auto& trackManager = magda::TrackManager::getInstance();
        trackManager.clearAllTracks();
        trackManager.setAudioEngine(&wrapper);

        const auto trackId = trackManager.createTrack("Chain power render");
        const auto fxId =
            trackManager.addDeviceToTrack(trackId, makeInternalDevice("FX Filter", "magda_filter"));
        const auto fxPath = magda::ChainNodePath::topLevelDevice(trackId, fxId);

        auto plugin = bridge->getPlugin(fxPath);
        expect(plugin != nullptr, "Internal FX should have a TE plugin");
        if (plugin == nullptr) {
            trackManager.clearAllTracks();
            return;
        }

        trackManager.setChainEnabled(trackId, false);
        expect(!plugin->isEnabled(), "Chain power off should disable the TE plugin");

        magda::prepareEditForOfflineRender(*edit);

        expect(!plugin->isEnabled(), "Render prep must leave a powered-off chain disabled (#1880)");

        auto* device = trackManager.getDeviceInChainByPath(fxPath);
        expect(device != nullptr && !device->bypassed,
               "Chain power must not write the device's own bypassed flag");

        trackManager.clearAllTracks();
    }
};

static OfflineRenderDevicePowerTest offlineRenderDevicePowerTest;
