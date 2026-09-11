#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <set>

#include "magda/daw/audio/FourOscMigration.hpp"
#include "magda/daw/audio/FourOscTranslation.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/TrackInfo.hpp"

/**
 * 4OSC to Poly Synth (#2437).
 *
 * 4OSC is Tracktion Engine's synth and is never ported, so a project holding
 * one opens on the native engine as the translation these cases describe.
 */

namespace {

using magda::DeviceInfo;
using magda::ParameterInfo;
using PolySynth = magda::daw::audio::compiled::MagdaPolySynthCompiledPlugin;

/// A 4OSC device as a project saves one: parameters under the ids TE gave
/// them, and wave shape, filter type and voice mode as ValueTree properties.
class FourOscPatch {
  public:
    FourOscPatch() {
        device_.id = magda::DeviceId{7};
        device_.name = "4OSC";
        device_.pluginId = "4osc";
        device_.deviceType = magda::DeviceType::Instrument;
        device_.isInstrument = true;
        device_.format = magda::PluginFormat::Internal;
    }

    FourOscPatch& parameter(const juce::String& name, float value) {
        ParameterInfo info;
        info.paramIndex = static_cast<int>(device_.parameters.size());
        info.name = name;
        info.currentValue = value;
        device_.parameters.push_back(std::move(info));
        return *this;
    }

    FourOscPatch& property(const juce::String& name, int value) {
        props_.set(name, value);
        return *this;
    }

    DeviceInfo build() {
        magda::device_state::Doc doc;
        doc.deviceType = "4osc";
        doc.root.props = props_;
        device_.pluginState = magda::device_state::encode(doc);
        return device_;
    }

  private:
    DeviceInfo device_;
    juce::NamedValueSet props_;
};

float slotValue(const DeviceInfo& device, int slot) {
    for (const auto& parameter : device.parameters)
        if (parameter.paramIndex == slot)
            return parameter.currentValue;

    FAIL("no slot " << slot);
    return 0.0f;
}

bool mentions(const std::vector<magda::daw::audio::FourOscGap>& gaps, const juce::String& text) {
    return std::ranges::any_of(
        gaps, [&text](const auto& gap) { return gap.control.containsIgnoreCase(text); });
}

int oscSlot(int osc, int offset) {
    return PolySynth::kOscBaseSlot + (osc - 1) * PolySynth::kOscSlotCount + offset;
}

}  // namespace

TEST_CASE("A 4OSC device is recognised by the id a project saves", "[core][4osc]") {
    CHECK(magda::daw::audio::isFourOscDevice(FourOscPatch{}.build()));

    DeviceInfo other;
    other.pluginId = PolySynth::xmlTypeName;
    CHECK_FALSE(magda::daw::audio::isFourOscDevice(other));
}

TEST_CASE("The translation keeps the device's identity", "[core][4osc]") {
    // The same DeviceId in the same slot, so every macro link, modifier and
    // automation lane that names this device still names it.
    const auto patch = FourOscPatch{}.build();
    const auto translated = magda::daw::audio::translateFourOsc(patch);

    CHECK(translated.device.id == patch.id);
    CHECK(translated.device.pluginId == juce::String(PolySynth::xmlTypeName));
    CHECK(translated.device.isInstrument);
}

TEST_CASE("An oscillator's wave, tune and level carry over", "[core][4osc]") {
    const auto patch = FourOscPatch{}
                           .property("waveShape1", 5)  // square
                           .parameter("tune1", 7.0f)
                           .parameter("fineTune1", -25.0f)
                           .parameter("level1", -6.0f)
                           .build();

    const auto translated = magda::daw::audio::translateFourOsc(patch);

    CHECK(slotValue(translated.device, PolySynth::kOscEnableBaseSlot) == 1.0f);
    CHECK(slotValue(translated.device, oscSlot(1, 0)) == 2.0f);  // Poly Synth's Square
    CHECK(slotValue(translated.device, oscSlot(1, 2)) == Catch::Approx(7.0f));
    CHECK(slotValue(translated.device, oscSlot(1, 3)) == Catch::Approx(-25.0f));
    CHECK(slotValue(translated.device, oscSlot(1, 1)) == Catch::Approx(-6.0f));
}

TEST_CASE("A tune past Poly Synth's range is clamped rather than wrapped", "[core][4osc]") {
    // 4OSC tunes +/-36 semitones and Poly Synth +/-24. Clamping detunes the
    // patch; wrapping would transpose it into another key.
    const auto patch = FourOscPatch{}.property("waveShape1", 1).parameter("tune1", 36.0f).build();

    CHECK(slotValue(magda::daw::audio::translateFourOsc(patch).device, oscSlot(1, 2)) ==
          Catch::Approx(24.0f));
}

TEST_CASE("An oscillator set to none is silenced rather than left playing", "[core][4osc]") {
    // Poly Synth's oscillators are always in the dsp, so "off" is the enable
    // slot. Left alone, osc 1 defaults audible and the patch gains a voice it
    // never had.
    const auto patch = FourOscPatch{}.property("waveShape1", 0).build();

    CHECK(slotValue(magda::daw::audio::translateFourOsc(patch).device,
                    PolySynth::kOscEnableBaseSlot) == 0.0f);
}

TEST_CASE("A noise oscillator is reported rather than approximated", "[core][4osc]") {
    const auto patch = FourOscPatch{}.property("waveShape2", 6).build();
    const auto translated = magda::daw::audio::translateFourOsc(patch);

    CHECK(slotValue(translated.device, PolySynth::kOscEnableBaseSlot + 1) == 0.0f);
    CHECK(mentions(translated.gaps, "noise"));
}

TEST_CASE("Envelope times reach Poly Synth in milliseconds", "[core][4osc]") {
    // 4OSC holds them in seconds and its sustain as a percentage. A straight
    // copy would give a 250 ms decay a value of 0.25 ms and a full sustain 100.
    const auto patch = FourOscPatch{}
                           .parameter("ampAttack", 0.25f)
                           .parameter("ampDecay", 0.5f)
                           .parameter("ampSustain", 80.0f)
                           .parameter("ampRelease", 1.5f)
                           .build();

    const auto translated = magda::daw::audio::translateFourOsc(patch);

    CHECK(slotValue(translated.device, PolySynth::kAmpAttackSlot) == Catch::Approx(250.0f));
    CHECK(slotValue(translated.device, PolySynth::kAmpDecaySlot) == Catch::Approx(500.0f));
    CHECK(slotValue(translated.device, PolySynth::kAmpSustainSlot) == Catch::Approx(0.8f));
    CHECK(slotValue(translated.device, PolySynth::kAmpReleaseSlot) == Catch::Approx(1500.0f));
}

TEST_CASE("An envelope longer than Poly Synth holds is clamped", "[core][4osc]") {
    // 4OSC reaches 60 seconds; Poly Synth's attack stops at two.
    const auto patch = FourOscPatch{}.parameter("ampAttack", 60.0f).build();

    CHECK(slotValue(magda::daw::audio::translateFourOsc(patch).device, PolySynth::kAmpAttackSlot) ==
          Catch::Approx(2000.0f));
}

TEST_CASE("The filter cutoff is read as a MIDI note and written as Hz", "[core][4osc]") {
    // 4OSC's filterFreq is a note number, 0 to 135.076232, which is how it
    // spaces the control logarithmically. Read as Hz it would put every patch
    // at the bottom of Poly Synth's range.
    const auto patch = FourOscPatch{}
                           .property("filterType", 1)  // lowpass
                           .parameter("filterFreq", 69.0f)
                           .build();

    const auto translated = magda::daw::audio::translateFourOsc(patch);

    CHECK(slotValue(translated.device, PolySynth::kFilterTypeSlot) == 0.0f);
    CHECK(slotValue(translated.device, PolySynth::kCutoffSlot) == Catch::Approx(440.0f));
}

TEST_CASE("A 4OSC patch with its filter off gets one out of the way", "[core][4osc]") {
    // 4OSC bypasses the filter entirely at type 0 and Poly Synth's is always
    // in the path, so the cutoff goes to the top rather than wherever
    // filterFreq happened to sit under a filter nobody heard.
    const auto patch =
        FourOscPatch{}.property("filterType", 0).parameter("filterFreq", 40.0f).build();

    CHECK(slotValue(magda::daw::audio::translateFourOsc(patch).device, PolySynth::kCutoffSlot) >
          17000.0f);
}

TEST_CASE("Resonance and envelope amount are scaled into their ranges", "[core][4osc]") {
    const auto patch = FourOscPatch{}
                           .property("filterType", 1)
                           .parameter("filterResonance", 50.0f)
                           .parameter("filterAmount", 0.5f)
                           .build();

    const auto translated = magda::daw::audio::translateFourOsc(patch);

    CHECK(slotValue(translated.device, PolySynth::kResonanceSlot) == Catch::Approx(0.475f));
    CHECK(slotValue(translated.device, PolySynth::kFilterEnvAmtSlot) == Catch::Approx(2.0f));
}

TEST_CASE("Voice mode is reordered rather than copied", "[core][4osc]") {
    // 4OSC counts mono, legato, poly; Poly Synth counts poly, mono, legato.
    // Copied across, every mono patch would come back polyphonic.
    const auto mono = FourOscPatch{}.property("voiceMode", 0).build();
    const auto legato = FourOscPatch{}.property("voiceMode", 1).build();
    const auto poly = FourOscPatch{}.property("voiceMode", 2).build();

    CHECK(slotValue(magda::daw::audio::translateFourOsc(mono).device, PolySynth::kVoiceModeSlot) ==
          1.0f);
    CHECK(slotValue(magda::daw::audio::translateFourOsc(legato).device,
                    PolySynth::kVoiceModeSlot) == 2.0f);
    CHECK(slotValue(magda::daw::audio::translateFourOsc(poly).device, PolySynth::kVoiceModeSlot) ==
          0.0f);
}

TEST_CASE("Unison is reported rather than translated into something thinner", "[core][4osc]") {
    const auto patch = FourOscPatch{}.property("waveShape1", 3).property("voices1", 4).build();

    CHECK(mentions(magda::daw::audio::translateFourOsc(patch).gaps, "Unison"));
}

TEST_CASE("A patch using nothing Poly Synth lacks reports no gaps", "[core][4osc]") {
    const auto patch = FourOscPatch{}
                           .property("waveShape1", 3)
                           .property("filterType", 1)
                           .parameter("tune1", 0.0f)
                           .parameter("ampAttack", 0.01f)
                           .build();

    CHECK(magda::daw::audio::translateFourOsc(patch).gaps.empty());
}

TEST_CASE("Every 4OSC in a project is found, pads included", "[core][4osc]") {
    magda::TrackInfo track;
    track.id = magda::TrackId{1};
    track.chain.fxChainElements.emplace_back(FourOscPatch{}.property("waveShape1", 3).build());

    magda::DeviceInfo other;
    other.id = magda::DeviceId{9};
    other.pluginId = PolySynth::xmlTypeName;
    track.chain.fxChainElements.emplace_back(std::move(other));

    magda::TrackInfo master;
    master.id = magda::MASTER_TRACK_ID;

    const auto found = magda::daw::audio::findFourOscDevices({track}, master);
    REQUIRE(found.size() == 1);
    CHECK(found.front().deviceName == "4OSC");
}

TEST_CASE("A project with no 4OSC has nothing to ask about", "[core][4osc]") {
    magda::TrackInfo master;
    master.id = magda::MASTER_TRACK_ID;

    CHECK(magda::daw::audio::findFourOscDevices({}, master).empty());
    CHECK(magda::daw::audio::describeConversion({}).isEmpty());
}

TEST_CASE("The conversion prompt names what will be lost", "[core][4osc]") {
    // "Some settings may change" tells somebody to expect a difference
    // without telling them what to listen for.
    magda::TrackInfo track;
    track.id = magda::TrackId{1};
    track.chain.fxChainElements.emplace_back(
        FourOscPatch{}.property("waveShape1", 3).property("voices1", 4).build());

    magda::TrackInfo master;
    master.id = magda::MASTER_TRACK_ID;

    const auto text = magda::daw::audio::describeConversion(
        magda::daw::audio::findFourOscDevices({track}, master));

    CHECK(text.contains("Poly Synth"));
    CHECK(text.contains("Unison"));
    CHECK(text.contains("saved alongside"));
}

TEST_CASE("A patch losing nothing is not warned about", "[core][4osc]") {
    magda::TrackInfo track;
    track.id = magda::TrackId{1};
    track.chain.fxChainElements.emplace_back(FourOscPatch{}.property("waveShape1", 3).build());

    magda::TrackInfo master;
    master.id = magda::MASTER_TRACK_ID;

    const auto text = magda::daw::audio::describeConversion(
        magda::daw::audio::findFourOscDevices({track}, master));

    CHECK(text.contains("somewhere to go"));
    CHECK_FALSE(text.contains("will be lost"));
}

TEST_CASE("The backup sits beside the project and never overwrites", "[core][4osc]") {
    const auto project = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("FourOscMigration")
                             .getChildFile("Song.mgd");
    project.getParentDirectory().createDirectory();

    const auto backup = magda::daw::audio::backupFileFor(project);
    CHECK(backup.getParentDirectory() == project.getParentDirectory());
    CHECK(backup.getFileName() == "Song (4OSC).mgd");

    // Converting twice must not take the first copy with it.
    backup.replaceWithText("first");
    CHECK(magda::daw::audio::backupFileFor(project).getFileName() != backup.getFileName());

    backup.deleteFile();
    project.getParentDirectory().deleteRecursively();
}

TEST_CASE("A project that was never saved has no backup to write", "[core][4osc]") {
    CHECK(magda::daw::audio::backupFileFor(juce::File{}) == juce::File{});
}

TEST_CASE("The built-in effects become a rack of MAGDA devices", "[core][4osc]") {
    // 4OSC processes distortion, chorus, delay then reverb, and the rack has
    // to keep that order: a reverb before a delay is a different sound.
    const auto patch = FourOscPatch{}
                           .property("distortionOn", 1)
                           .property("chorusOn", 1)
                           .property("delayOn", 1)
                           .property("reverbOn", 1)
                           .parameter("distortion", 0.5f)
                           .parameter("chorusMix", 0.4f)
                           .parameter("reverbSize", 0.8f)
                           .build();

    auto next = magda::DeviceId{100};
    const auto translated = magda::daw::audio::translateFourOsc(patch, [&next] { return next++; });

    REQUIRE(translated.effects != nullptr);
    REQUIRE(translated.effects->chains.size() == 1);

    const auto& elements = translated.effects->chains.front().elements;
    REQUIRE(elements.size() == 4);
    CHECK(magda::getDevice(elements[0]).name == "Clipper");
    CHECK(magda::getDevice(elements[1]).name == "Chorus");
    CHECK(magda::getDevice(elements[2]).name == "Delay");
    CHECK(magda::getDevice(elements[3]).name == "Reverb");

    // Each is a device of its own in the project, so none may share the
    // synth's id or another effect's.
    std::set<magda::DeviceId> ids{patch.id};
    for (const auto& element : elements)
        CHECK(ids.insert(magda::getDevice(element).id).second);
}

TEST_CASE("Only the effects that were switched on are built", "[core][4osc]") {
    const auto patch = FourOscPatch{}.property("reverbOn", 1).build();

    auto next = magda::DeviceId{100};
    const auto translated = magda::daw::audio::translateFourOsc(patch, [&next] { return next++; });

    REQUIRE(translated.effects != nullptr);
    REQUIRE(translated.effects->chains.front().elements.size() == 1);
    CHECK(magda::getDevice(translated.effects->chains.front().elements.front()).name == "Reverb");
}

TEST_CASE("A patch with no effects on builds no rack", "[core][4osc]") {
    const auto patch = FourOscPatch{}.property("waveShape1", 3).build();

    auto next = magda::DeviceId{100};
    CHECK(magda::daw::audio::translateFourOsc(patch, [&next] { return next++; }).effects ==
          nullptr);
}

TEST_CASE("The effects are no longer reported as losses", "[core][4osc]") {
    // They were gaps until the rack carried them.
    const auto patch = FourOscPatch{}.property("reverbOn", 1).property("delayOn", 1).build();

    auto next = magda::DeviceId{100};
    const auto translated = magda::daw::audio::translateFourOsc(patch, [&next] { return next++; });

    CHECK_FALSE(mentions(translated.gaps, "Reverb"));
    CHECK_FALSE(mentions(translated.gaps, "Delay"));
}

TEST_CASE("4OSC's master level becomes the synth's output gain", "[core][4osc]") {
    const auto patch = FourOscPatch{}.parameter("masterLevel", -6.0f).build();

    CHECK(slotValue(magda::daw::audio::translateFourOsc(patch).device,
                    PolySynth::kOutputGainSlot) == Catch::Approx(-6.0f));
}
