#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <set>

#include "magda/daw/audio/FourOscMigration.hpp"
#include "magda/daw/audio/FourOscTranslation.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaDelayCompiledPlugin.hpp"
#include "magda/daw/audio/plugins/compiled/MagdaPolySynthCompiledPlugin.hpp"
#include "magda/daw/core/ChainWalk.hpp"
#include "magda/daw/core/DeviceState.hpp"
#include "magda/daw/core/RackInfo.hpp"
#include "magda/daw/core/TrackInfo.hpp"
#include "magda/daw/core/TrackManager.hpp"

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

    /// @p id is 4OSC's own parameter id. A project stores the display name
    /// ("Tune 1") and addresses the parameter by index, so the fixture does
    /// the same: matching on the id would pass against code no real project
    /// exercises.
    FourOscPatch& parameter(const juce::String& id, float value) {
        static const juce::StringArray order{"tune1",          "fineTune1",   "level1",
                                             "pulseWidth1",    "detune1",     "spread1",
                                             "pan1",           "tune2",       "fineTune2",
                                             "level2",         "pulseWidth2", "detune2",
                                             "spread2",        "pan2",        "tune3",
                                             "fineTune3",      "level3",      "pulseWidth3",
                                             "detune3",        "spread3",     "pan3",
                                             "tune4",          "fineTune4",   "level4",
                                             "pulseWidth4",    "detune4",     "spread4",
                                             "pan4",           "lfoRate1",    "lfoDepth1",
                                             "lfoRate2",       "lfoDepth2",   "modAttack1",
                                             "modDecay1",      "modSustain1", "modRelease1",
                                             "modAttack2",     "modDecay2",   "modSustain2",
                                             "modRelease2",    "ampAttack",   "ampDecay",
                                             "ampSustain",     "ampRelease",  "ampVelocity",
                                             "filterAttack",   "filterDecay", "filterSustain",
                                             "filterRelease",  "filterFreq",  "filterResonance",
                                             "filterAmount",   "filterKey",   "filterVelocity",
                                             "distortion",     "reverbSize",  "reverbDamping",
                                             "reverbWidth",    "reverbMix",   "delayFeedback",
                                             "delayCrossfeed", "delayMix",    "chorusSpeed",
                                             "chorusDepth",    "chorusWidth", "chorusMix",
                                             "legato",         "masterLevel"};

        const auto index = order.indexOf(id);
        REQUIRE(index >= 0);

        ParameterInfo info;
        info.paramIndex = index;
        info.name = id;  // a project stores a display name; nothing reads it
        info.currentValue = value;
        device_.parameters.push_back(std::move(info));
        return *this;
    }

    FourOscPatch& property(const juce::String& name, int value) {
        props_.set(name, value);
        return *this;
    }

    FourOscPatch& floatProperty(const juce::String& name, float value) {
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

TEST_CASE("A patch that wrote nothing lands on 4OSC's defaults", "[core][4osc]") {
    // TE writes only what differs from a property's default, so a patch left
    // alone arrives with no parameters and no properties at all. Poly Synth's
    // own defaults are a different sound: osc 1 sits at -12 dB where 4OSC
    // sits at 0, and the amp envelope is 5/200/70%/400 ms against 100/100/80%/
    // 100 ms.
    const auto translated = magda::daw::audio::translateFourOsc(FourOscPatch{}.build());
    const auto& device = translated.device;

    CHECK(slotValue(device, oscSlot(1, 1)) == Catch::Approx(0.0f));
    CHECK(slotValue(device, PolySynth::kAmpAttackSlot) == Catch::Approx(100.0f));
    CHECK(slotValue(device, PolySynth::kAmpDecaySlot) == Catch::Approx(100.0f));
    CHECK(slotValue(device, PolySynth::kAmpSustainSlot) == Catch::Approx(0.8f));
    CHECK(slotValue(device, PolySynth::kAmpReleaseSlot) == Catch::Approx(100.0f));

    // Nothing was set, so nothing is worth reporting as lost.
    CHECK(translated.gaps.empty());
}

TEST_CASE("A stock-named 4OSC does not stay called 4OSC", "[core][4osc]") {
    // A pathless alias materialises against the device whose normalised NAME
    // matches its key, so a device still called 4OSC answers for 4OSC's
    // aliases after the swap.
    auto stock = FourOscPatch{}.build();
    stock.name = "4OSC";
    CHECK(magda::daw::audio::translateFourOsc(stock).device.name == "Poly Synth");

    auto registered = FourOscPatch{}.build();
    registered.name = "4OSC Synth";
    CHECK(magda::daw::audio::translateFourOsc(registered).device.name == "Poly Synth");

    // A name somebody chose is theirs.
    auto named = FourOscPatch{}.build();
    named.name = "Bass";
    CHECK(magda::daw::audio::translateFourOsc(named).device.name == "Bass");
}

TEST_CASE("What the host owns on the slot survives the swap", "[core][4osc]") {
    // The gain knob, the macros and the modulators belong to the device in the
    // chain, not to the plugin being replaced underneath it. Building the
    // replacement from nothing put a slot trimmed to -6 dB back at unity.
    auto source = FourOscPatch{}.build();
    source.gainValue = 0.5f;
    source.gainDb = -6.0f;
    source.macros[0].name = "Brightness";
    source.macros[0].value = 0.25f;
    source.modPanelOpen = true;

    const auto translated = magda::daw::audio::translateFourOsc(source);

    CHECK(translated.device.gainValue == Catch::Approx(0.5f));
    CHECK(translated.device.gainDb == Catch::Approx(-6.0f));
    CHECK(translated.device.macros[0].name == "Brightness");
    CHECK(translated.device.macros[0].value == Catch::Approx(0.25f));
    CHECK(translated.device.modPanelOpen);

    // And nothing of 4OSC's own goes with it.
    CHECK(translated.device.pluginState.isEmpty());
    CHECK(translated.device.parameters.size() ==
          static_cast<std::size_t>(PolySynth{}.parameterCount()));
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
    CHECK(magda::daw::audio::describeMigration({}).contains("Tracktion engine"));
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

    const auto text = magda::daw::audio::describeMigration(
        magda::daw::audio::findFourOscDevices({track}, master));

    CHECK(text.contains("Poly Synth"));
    CHECK(text.contains("Unison"));
    CHECK(text.contains("original is left alone"));
}

TEST_CASE("A patch losing nothing is not warned about", "[core][4osc]") {
    magda::TrackInfo track;
    track.id = magda::TrackId{1};
    track.chain.fxChainElements.emplace_back(FourOscPatch{}.property("waveShape1", 3).build());

    magda::TrackInfo master;
    master.id = magda::MASTER_TRACK_ID;

    const auto text = magda::daw::audio::describeMigration(
        magda::daw::audio::findFourOscDevices({track}, master));

    CHECK_FALSE(text.contains("will be lost"));
}

TEST_CASE("The converted project is a new project beside the old one", "[core][4osc]") {
    // A MAGDA project is a folder holding its .mgd, so the new one is the
    // folder's neighbour rather than a stray file inside it.
    const auto root =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("FourOscMigration");
    const auto project = root.getChildFile("Song").getChildFile("Song.mgd");
    project.getParentDirectory().createDirectory();

    // Unwrapped: saveProjectAs() makes the folder, and skips making it when
    // the file already looks wrapped.
    const auto converted = magda::daw::audio::convertedProjectFileFor(project);
    CHECK(converted.getFileName() == "Song (magda engine).mgd");
    CHECK(converted.getParentDirectory() == root);

    root.deleteRecursively();
}

TEST_CASE("A project that was never saved has nowhere to sit beside", "[core][4osc]") {
    CHECK(magda::daw::audio::convertedProjectFileFor(juce::File{}) == juce::File{});
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
    const auto nextEffectId = [&next] { return next++; };
    const auto translated = magda::daw::audio::translateFourOsc(patch, nextEffectId);

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

TEST_CASE("The delay is synced to the beat value 4OSC held", "[core][4osc]") {
    // 4OSC divides its delay by the tempo when it renders, so its number is
    // beats. The Division slot stores the menu entry's POSITION, not the
    // quarter-note multiplier the entry carries, and 4OSC's default of one
    // beat has to reach the entry whose multiplier is 1.0 rather than the
    // second entry in the list.
    using Delay = magda::daw::audio::compiled::MagdaDelayCompiledPlugin;

    const auto divisionFor = [](float beats) {
        juce::NamedValueSet props;
        auto patch = FourOscPatch{}.property("delayOn", 1);
        if (beats != 1.0f)
            patch.floatProperty("delay", beats);

        auto next = magda::DeviceId{100};
        const auto nextEffectId = [&next] { return next++; };
        const auto translated = magda::daw::audio::translateFourOsc(patch.build(), nextEffectId);

        REQUIRE(translated.effects != nullptr);
        const auto& elements = translated.effects->chains.front().elements;
        REQUIRE(elements.size() == 1);

        const auto& delay = magda::getDevice(elements.front());
        CHECK(slotValue(delay, Delay::kSyncSlot) == 1.0f);
        return static_cast<int>(slotValue(delay, Delay::kDivisionSlot));
    };

    const Delay metadata;
    const auto values = metadata.menuValuesForIdx(Delay::kDivisionSlot);
    REQUIRE(!values.empty());

    const auto indexOfMultiplier = [&values](float multiplier) {
        for (auto index = 0; index < static_cast<int>(values.size()); ++index)
            if (std::abs(values[static_cast<std::size_t>(index)] - multiplier) < 1e-3f)
                return index;
        FAIL("no division worth " << multiplier);
        return -1;
    };

    CHECK(divisionFor(1.0f) == indexOfMultiplier(1.0f));
    CHECK(divisionFor(0.5f) == indexOfMultiplier(0.5f));
    CHECK(divisionFor(4.0f) == indexOfMultiplier(4.0f));
}

TEST_CASE("Only the effects that were switched on are built", "[core][4osc]") {
    const auto patch = FourOscPatch{}.property("reverbOn", 1).build();

    auto next = magda::DeviceId{100};
    const auto nextEffectId = [&next] { return next++; };
    const auto translated = magda::daw::audio::translateFourOsc(patch, nextEffectId);

    REQUIRE(translated.effects != nullptr);
    REQUIRE(translated.effects->chains.front().elements.size() == 1);
    CHECK(magda::getDevice(translated.effects->chains.front().elements.front()).name == "Reverb");
}

TEST_CASE("A patch with no effects on builds no rack", "[core][4osc]") {
    const auto patch = FourOscPatch{}.property("waveShape1", 3).build();

    auto next = magda::DeviceId{100};
    const auto nextEffectId = [&next] { return next++; };
    CHECK(magda::daw::audio::translateFourOsc(patch, nextEffectId).effects == nullptr);
}

TEST_CASE("The effects are no longer reported as losses", "[core][4osc]") {
    // They were gaps until the rack carried them.
    const auto patch = FourOscPatch{}.property("reverbOn", 1).property("delayOn", 1).build();

    auto next = magda::DeviceId{100};
    const auto nextEffectId = [&next] { return next++; };
    const auto translated = magda::daw::audio::translateFourOsc(patch, nextEffectId);

    CHECK_FALSE(mentions(translated.gaps, "Reverb"));
    CHECK_FALSE(mentions(translated.gaps, "Delay"));
}

TEST_CASE("4OSC's master level becomes the synth's output gain", "[core][4osc]") {
    const auto patch = FourOscPatch{}.parameter("masterLevel", -6.0f).build();

    CHECK(slotValue(magda::daw::audio::translateFourOsc(patch).device,
                    PolySynth::kOutputGainSlot) == Catch::Approx(-6.0f));
}

TEST_CASE("A patch with effects puts both gains after the effects", "[core][4osc]") {
    // 4OSC applies its master level after all four effects, and the slot's own
    // trim after the whole plugin. Left on the synth, both would sit in front
    // of the distortion and change its drive.
    auto source = FourOscPatch{}
                      .property("distortionOn", 1)
                      .parameter("masterLevel", -6.0f)
                      .parameter("distortion", 0.5f)
                      .build();
    source.gainDb = -3.0f;
    source.gainValue = 0.7f;

    auto next = magda::DeviceId{100};
    const auto nextEffectId = [&next] { return next++; };
    const auto translated = magda::daw::audio::translateFourOsc(source, nextEffectId);

    REQUIRE(translated.effects != nullptr);
    const auto& chain = translated.effects->chains.front();
    CHECK(chain.volume == Catch::Approx(-6.0f));

    const auto& last = magda::getDevice(chain.elements.back());
    CHECK(last.gainDb == Catch::Approx(-3.0f));
    CHECK(last.gainValue == Catch::Approx(0.7f));

    CHECK(slotValue(translated.device, PolySynth::kOutputGainSlot) == Catch::Approx(0.0f));
    CHECK(translated.device.gainDb == Catch::Approx(0.0f));
    CHECK(translated.device.gainValue == Catch::Approx(1.0f));
}

TEST_CASE("A silent patch with effects stays silent", "[core][4osc]") {
    // 4OSC's master level reaches -100 dB, which is silence, and no parameter
    // slot in MAGDA goes below -60. The chain fader does: -100 is the engine's
    // minus infinity (PlanValues.cpp).
    const auto patch =
        FourOscPatch{}.property("reverbOn", 1).parameter("masterLevel", -100.0f).build();

    auto next = magda::DeviceId{100};
    const auto nextEffectId = [&next] { return next++; };
    const auto translated = magda::daw::audio::translateFourOsc(patch, nextEffectId);

    REQUIRE(translated.effects != nullptr);
    CHECK(translated.effects->chains.front().volume == Catch::Approx(-100.0f));
}

TEST_CASE("A trim above what a fader holds survives the move", "[core][4osc]") {
    // A device trim reaches +12 dB where every fader stops at +6, which is why
    // the trim moves to another device's trim rather than onto the rack.
    auto source = FourOscPatch{}.property("reverbOn", 1).build();
    source.gainDb = 12.0f;
    source.gainValue = 3.98f;

    auto next = magda::DeviceId{100};
    const auto nextEffectId = [&next] { return next++; };
    const auto translated = magda::daw::audio::translateFourOsc(source, nextEffectId);

    REQUIRE(translated.effects != nullptr);
    const auto& last = magda::getDevice(translated.effects->chains.front().elements.back());
    CHECK(last.gainDb == Catch::Approx(12.0f));
    CHECK(last.gainValue == Catch::Approx(3.98f));
}

TEST_CASE("Converting a project replaces the synth and adds its effects",
          "[core][4osc][.singleton]") {
    auto& tracks = magda::TrackManager::getInstance();
    const auto trackId = tracks.createTrack("Synth");

    tracks.addDeviceToTrack(
        trackId, FourOscPatch{}.property("waveShape1", 3).property("reverbOn", 1).build());

    // addDeviceToTrack hands out the id, so read it back rather than assume.
    const auto* placed = tracks.getTrack(trackId);
    REQUIRE(placed != nullptr);
    REQUIRE(placed->chain.fxChainElements.size() == 1);
    const auto synthId = magda::getDevice(placed->chain.fxChainElements.front()).id;

    CHECK(magda::daw::audio::convertFourOscDevices(tracks) == 1);

    const auto* track = tracks.getTrack(trackId);
    REQUIRE(track != nullptr);

    const auto& elements = track->chain.fxChainElements;
    REQUIRE(elements.size() == 2);

    // The synth keeps its place and its id; the rack goes directly after it,
    // so it reaches the same signal 4OSC's own effects did.
    CHECK(magda::getDevice(elements[0]).pluginId == juce::String(PolySynth::xmlTypeName));
    CHECK(magda::getDevice(elements[0]).id == synthId);
    REQUIRE(magda::isRack(elements[1]));
    CHECK(magda::getRack(elements[1]).name == "4OSC FX");

    // And a second pass finds nothing left to do.
    CHECK(magda::daw::audio::convertFourOscDevices(tracks) == 0);
}

TEST_CASE("A link into the 4OSC's parameters goes with the 4OSC", "[core][4osc][.singleton]") {
    // Poly Synth's parameters are different controls at the same indices, so a
    // link the conversion left behind would drive the wrong one: 4OSC's Tune 1
    // is Poly Synth's Osc 1 Wave.
    auto& tracks = magda::TrackManager::getInstance();
    const auto trackId = tracks.createTrack("Synth");

    tracks.addDeviceToTrack(trackId, FourOscPatch{}.property("waveShape1", 3).build());

    magda::DeviceInfo other;
    other.name = "Utility";
    other.pluginId = "magda_utility";
    other.format = magda::PluginFormat::Internal;
    tracks.addDeviceToTrack(trackId, other);

    auto* placed = tracks.getTrack(trackId);
    REQUIRE(placed != nullptr);
    REQUIRE(placed->chain.fxChainElements.size() == 2);

    const auto synthPath =
        magda::chain_walk::deviceIn(magda::ChainNodePath::trackLevel(trackId),
                                    magda::getDevice(placed->chain.fxChainElements[0]).id);
    const auto otherPath =
        magda::chain_walk::deviceIn(magda::ChainNodePath::trackLevel(trackId),
                                    magda::getDevice(placed->chain.fxChainElements[1]).id);

    placed->macros[0].links.push_back({magda::ControlTarget::pluginParam(synthPath, 0), 1.0f});
    placed->macros[0].links.push_back({magda::ControlTarget::pluginParam(otherPath, 0), 1.0f});
    placed->macros[1].links.push_back({magda::ControlTarget::deviceMacro(synthPath, 0), 1.0f});

    CHECK(magda::daw::audio::convertFourOscDevices(tracks) == 1);

    const auto* track = tracks.getTrack(trackId);
    REQUIRE(track != nullptr);

    REQUIRE(track->macros[0].links.size() == 1);
    CHECK(track->macros[0].links.front().target.devicePath == otherPath);

    // A macro knob is a macro knob on either device, so that link stays.
    CHECK(track->macros[1].links.size() == 1);
}

TEST_CASE("A 4OSC saved as legacy engine XML still translates", "[core][4osc]") {
    // The format a project old enough to hold a 4OSC actually uses.
    // device_state::decode() refuses it, and reading nothing there left every
    // oscillator looking like "none" and the converted synth silent.
    magda::DeviceInfo device;
    device.id = magda::DeviceId{3};
    device.name = "4OSC";
    device.pluginId = "4osc";
    device.isInstrument = true;
    device.format = magda::PluginFormat::Internal;
    device.pluginState = R"(<PLUGIN type="4osc" waveShape1="3" waveShape2="1" filterType="1"
                                    voiceMode="0"/>)";

    const auto translated = magda::daw::audio::translateFourOsc(device);

    CHECK(slotValue(translated.device, PolySynth::kOscEnableBaseSlot) == 1.0f);
    CHECK(slotValue(translated.device, oscSlot(1, 0)) == 1.0f);  // sawUp -> Saw
    CHECK(slotValue(translated.device, PolySynth::kOscEnableBaseSlot + 1) == 1.0f);
    CHECK(slotValue(translated.device, oscSlot(2, 0)) == 0.0f);              // sine
    CHECK(slotValue(translated.device, PolySynth::kVoiceModeSlot) == 1.0f);  // mono
}

TEST_CASE("A patch whose oscillators are all off is not silently enabled", "[core][4osc]") {
    // The other side of the same bug: "none" everywhere really does mean
    // silence, and must not be confused with state that could not be read.
    magda::DeviceInfo device;
    device.pluginId = "4osc";
    device.pluginState = R"(<PLUGIN type="4osc" waveShape1="0"/>)";

    const auto translated = magda::daw::audio::translateFourOsc(device);
    for (auto osc = 0; osc < 4; ++osc)
        CHECK(slotValue(translated.device, PolySynth::kOscEnableBaseSlot + osc) == 0.0f);
}

TEST_CASE("A patch left on 4OSC's defaults still makes a sound", "[core][4osc]") {
    // TE writes only what differs from a property's default, so a patch
    // nobody changed carries no waveShape at all. Ten of the 4OSC devices in
    // a real project folder look like this, and defaulting them to "none"
    // converted every one of them to silence.
    magda::DeviceInfo device;
    device.pluginId = "4osc";
    device.pluginState = R"(<PLUGIN type="4osc" ampSustain="80.5"/>)";

    const auto translated = magda::daw::audio::translateFourOsc(device);

    CHECK(slotValue(translated.device, PolySynth::kOscEnableBaseSlot) == 1.0f);
    CHECK(slotValue(translated.device, oscSlot(1, 0)) == 0.0f);  // 4OSC's default sine

    // And the other three stay off, which is also 4OSC's default.
    for (auto osc = 1; osc < 4; ++osc)
        CHECK(slotValue(translated.device, PolySynth::kOscEnableBaseSlot + osc) == 0.0f);
}
