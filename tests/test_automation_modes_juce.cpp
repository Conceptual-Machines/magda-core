#include <juce_core/juce_core.h>

#include "SharedTestEngine.hpp"
#include "magda/daw/audio/automation/AutomationRecordingEngine.hpp"

using namespace magda;

// Tests that construct an AutomationRecordingEngine and therefore need a
// te::Edit via the shared engine. Lives in magda_juce_tests so the JUCE/TE
// global state has the right teardown plumbing for CI.
class AutomationModesTest final : public juce::UnitTest {
  public:
    AutomationModesTest() : juce::UnitTest("Automation Modes Tests", "magda") {}

    void runTest() override {
        testSetModeRoundTrip();
        testSetWriteEnabledShim();
        testIsWriteEnabledForAllModes();
    }

  private:
    void testSetModeRoundTrip() {
        beginTest("setMode / getMode round-trips every value");

        auto& engine = magda::test::getSharedEngine();
        AutomationRecordingEngine rec(*engine.getEdit());

        expect(rec.getMode() == AutomationMode::Off);
        rec.setMode(AutomationMode::Write);
        expect(rec.getMode() == AutomationMode::Write);
        rec.setMode(AutomationMode::Touch);
        expect(rec.getMode() == AutomationMode::Touch);
        rec.setMode(AutomationMode::Latch);
        expect(rec.getMode() == AutomationMode::Latch);
        rec.setMode(AutomationMode::Off);
        expect(rec.getMode() == AutomationMode::Off);
    }

    void testSetWriteEnabledShim() {
        beginTest("setWriteEnabled is a thin shim over setMode (Off <-> Write)");

        auto& engine = magda::test::getSharedEngine();
        AutomationRecordingEngine rec(*engine.getEdit());

        rec.setWriteEnabled(true);
        expect(rec.getMode() == AutomationMode::Write);
        expect(rec.isWriteEnabled());

        rec.setWriteEnabled(false);
        expect(rec.getMode() == AutomationMode::Off);
        expect(!rec.isWriteEnabled());
    }

    void testIsWriteEnabledForAllModes() {
        beginTest("isWriteEnabled is true for every non-Off mode");
        // Important for the existing UI surface — the transport "armed"
        // indicator and AutomationManager::isWriteModeEnabled keep working
        // unchanged when the user picks Touch or Latch.

        auto& engine = magda::test::getSharedEngine();
        AutomationRecordingEngine rec(*engine.getEdit());

        rec.setMode(AutomationMode::Off);
        expect(!rec.isWriteEnabled());
        rec.setMode(AutomationMode::Write);
        expect(rec.isWriteEnabled());
        rec.setMode(AutomationMode::Touch);
        expect(rec.isWriteEnabled());
        rec.setMode(AutomationMode::Latch);
        expect(rec.isWriteEnabled());
    }
};

static AutomationModesTest automationModesTestInstance;
