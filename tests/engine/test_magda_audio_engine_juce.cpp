#include <juce_core/juce_core.h>

#include <cmath>

#include "JuceTestStateGuard.hpp"
#include "magda/daw/api/magda_api.hpp"
#include "magda/daw/api/project_api.hpp"
#include "magda/daw/core/TempoMap.hpp"
#include "magda/daw/engine/MagdaAudioEngine.hpp"
#include "magda/daw/engine/TracktionEngineWrapper.hpp"
#include "magda/daw/project/ProjectInfo.hpp"

/**
 * What MagdaAudioEngine answers with no Edit behind it (#2579).
 *
 * The fork under this engine is services only. Both halves of that are
 * asserted: the questions the Edit used to answer, and the Edit's absence,
 * which is what makes answering them here necessary.
 */

namespace {

/// Tighter than any rounding these conversions do, looser than exact
/// double equality on a value that travelled through a tempo map.
constexpr double kTolerance = 1.0e-9;

class MagdaAudioEngineTest final : public juce::UnitTest {
  public:
    MagdaAudioEngineTest() : juce::UnitTest("Magda Audio Engine Tests", "magda") {}

    void runTest() override {
        magda::test::runWithCleanJuceState([this] { testAnswersWithoutAnEdit(); });
    }

  private:
    void testAnswersWithoutAnEdit() {
        beginTest("The engine answers the transport model itself, from no Edit");

        magda::MagdaAudioEngine engine{magda::AudioEngineOptions{.headless = true}};
        expect(engine.initialize(), "The engine comes up headless");

        expect(engine.fork().getEdit() == nullptr, "initialisePlayback() was never called");
        expect(engine.fork().getAudioBridge() == nullptr, "and so nothing mirrors the model into");

        expect(engine.getAudioBridge() == nullptr, "There is no bridge to hand out");
        expect(engine.getMidiBridge() != nullptr, "The MidiBridge is a service and survives");
        expect(engine.hasActiveEdit(), "An initialised engine has a project to play");

        const auto* map = engine.tempoMap();
        expect(map != nullptr, "The app converts beats and seconds through the host's map");
        if (map == nullptr)
            return;

        engine.setTempo(140.0);
        expect(std::abs(engine.getTempo() - 140.0) < kTolerance, "The tempo is the host's");
        expect(std::abs(map->bpmAt(0.0) - 140.0) < kTolerance, "and the map follows it");
        expect(std::abs(map->beatToTime(140.0) - 60.0) < kTolerance,
               "140 beats at 140 bpm is a minute");

        engine.setLooping(true);
        expect(engine.isLooping(), "The loop is the host's too");

        expect(engine.getMagdaApi().project().getCurrentProjectInfo().tempo > 0.0,
               "The engine's own MagdaApi answers");

        engine.shutdown();
    }
};

MagdaAudioEngineTest magdaAudioEngineTest;

}  // namespace
