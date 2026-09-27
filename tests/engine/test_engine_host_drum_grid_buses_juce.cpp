#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include <algorithm>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/core/DrumGridPads.hpp"
#include "magda/daw/core/TrackManager.hpp"
#include "magda/daw/engine/host/EngineHost.hpp"

namespace {

magda::DeviceInfo drumGrid() {
    magda::DeviceInfo grid;
    grid.name = "Drum Grid";
    grid.pluginId = "drumgrid";
    grid.isInstrument = true;
    grid.deviceType = magda::DeviceType::Instrument;
    grid.format = magda::PluginFormat::Internal;
    grid.pads.reset(std::make_unique<magda::RackInfo>());
    return grid;
}

magda::DeviceInfo kick() {
    magda::DeviceInfo device;
    device.name = "Kick";
    device.pluginId = "magda_kick";
    device.isInstrument = true;
    device.deviceType = magda::DeviceType::Instrument;
    device.format = magda::PluginFormat::Internal;
    return device;
}

}  // namespace

class EngineHostDrumGridBusesTest final : public juce::UnitTest {
  public:
    EngineHostDrumGridBusesTest() : juce::UnitTest("Engine Host Drum Grid Buses", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { padBusesOpenAndCloseTracks(); });
    }

  private:
    void padBusesOpenAndCloseTracks() {
        beginTest("A pad sent to a bus gets a multi-out track, and loses it on the way back");
        auto& tracks = magda::TrackManager::getInstance();
        juce::AudioDeviceManager devices;
        magda::daw::engine_host::EngineHost host;
        host.start(devices);

        const auto trackId = tracks.createTrack("Drums");
        const auto gridId = tracks.addDeviceToTrack(trackId, drumGrid());
        const auto gridPath = magda::ChainNodePath::topLevelDevice(trackId, gridId);
        tracks.ensurePad(gridPath, 1);
        tracks.setPadDevice(gridPath, 1, kick());

        const auto children = [&tracks, trackId, gridId] {
            std::vector<const magda::TrackInfo*> found;
            for (const auto& track : tracks.getTracks())
                if (track.multiOutLink && track.multiOutLink->sourceTrackId == trackId &&
                    track.multiOutLink->sourceDeviceId == gridId)
                    found.push_back(&track);
            return found;
        };
        expect(children().empty());

        expect(tracks.setPadOutput(gridPath, 1, 3));
        const auto opened = children();
        expectEquals(static_cast<int>(opened.size()), 1);
        if (opened.size() == 1) {
            expectEquals(opened[0]->multiOutLink->outputPairIndex, 3);
            expect(opened[0]->name.startsWith("Drum Grid: "));
        }

        expect(tracks.setPadOutput(gridPath, 1, 0));
        expect(children().empty(), "Back on the main mix, the bus track closes");
    }
};

static EngineHostDrumGridBusesTest engineHostDrumGridBusesTest;
