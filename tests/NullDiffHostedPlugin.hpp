#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/DeviceInfo.hpp"
#include "core/TypeIds.hpp"

/**
 * @file NullDiffHostedPlugin.hpp
 * @brief The plugin the corpus hosts, and the model device that names it
 *        (#2246).
 *
 * Every other device in the corpus is written twice: once as a te::Plugin for
 * the incumbent leg and once as an SDK device for the native one, from a
 * contract that says what the two owe each other (NullDiffGain.hpp). An
 * external plugin is the one device that cannot be written twice, and that is
 * the whole reason these cases exist. Neither engine wrote it, neither can ask
 * anything of it, and both are reduced to handing it blocks and taking back
 * what it returns -- so what the two hosts do around the plugin is the entire
 * difference between them, and it is what a case here measures.
 *
 * One implementation, two hosts. The instance below is created through the
 * ordinary path in both legs: a format registered with the engine's own
 * juce::AudioPluginFormatManager, a description in the same
 * juce::KnownPluginList both legs resolve against, and from there
 * te::ExternalPlugin on one side and EngineExternalDevice on the other. Nothing
 * in either engine knows this plugin is ours.
 *
 * It is in the binary rather than on the machine, and that is not a shortcut
 * around #2175. The two questions are different. A real project hosting a real
 * plugin asks whether this machine's Pro-Q renders the same under both engines,
 * and it can only be asked where that plugin is installed. These ask what the
 * host does: which channels a mono plugin is handed, what the wrapper pair
 * means, where a sidechain key lands, what a plugin is told about the
 * transport. Every one of those is an assertion about a number the host chose,
 * and a plugin that reports exactly what it was given answers it on every
 * machine, CI included, where a real plugin answers it nowhere.
 *
 * What each role does is chosen so the answer is readable off the render rather
 * than inferred from it: the polarity plugin makes the wrapper pair's
 * arithmetic the whole signal, the key plugin returns its sidechain instead of
 * its input so a render of the wrong bus is silence, and the transport plugin
 * renders where it thinks it is.
 */

namespace magda::nulldiff {

/**
 * @brief What a hosted plugin does with a block.
 *
 * One class implements all of them and one description is published per role,
 * because a project names a plugin rather than a mode: a case that had to
 * configure the plugin after creating it would be testing a path no project
 * has.
 */
enum class HostedRole {
    /// Stereo in, stereo out, output is the input inverted.
    ///
    /// The wrapper pair's own material. Dry plus wet of an inverted signal is
    /// `(dry - wet)` times the input, so a mix the host got wrong is a level
    /// nobody has to squint at, and a fully wet slot is full scale rather than
    /// something that looks like the input.
    Polarity,

    /// One channel in, one channel out, output is the input.
    ///
    /// Identity on purpose. What a mono case measures is the fold and the
    /// spread the host puts either side of the plugin, and a plugin that also
    /// changed the signal would put a second arithmetic in the residual.
    Narrow,

    /// Stereo in, stereo out, delayed by @ref kHostedLatencySamples and
    /// reporting exactly that.
    ///
    /// A pure delay, so what it renders is a function of the timeline and
    /// nothing else, and the case is about the compensation rather than about
    /// the plugin.
    Latency,

    /// Stereo main input, stereo sidechain input, stereo out; the output is the
    /// sidechain.
    ///
    /// Returning the key rather than processing with it is what makes the case
    /// legible: a host that wired the key to the wrong channels renders the
    /// main input, and one that wired nothing renders silence. A compressor
    /// would render a difference in level that a reader has to trust somebody
    /// measured.
    Key,

    /// Stereo in, stereo out; the input is discarded and a sine locked to the
    /// bar is written in its place.
    ///
    /// The one thing a plugin cannot work out for itself. Computed per sample
    /// from the position the host published for the block, so it is continuous
    /// across block boundaries and a host that framed the render differently
    /// still owes the same samples -- and a host reporting the wrong position
    /// renders the right shape at the wrong phase, which is loud.
    Transport,

    /// An instrument: no audio input, stereo out, one sample at
    /// `velocity / 127` on every note-on.
    ///
    /// The same law the corpus's own impulse synth runs (NullDiffGain.hpp), for
    /// the same reason: an impulse at the note's own sample says both that the
    /// MIDI arrived and where, and nothing between the two engines interpolates
    /// it.
    Instrument,

    /// The same instrument, which also writes every note-on it received to its
    /// MIDI output.
    ///
    /// What a project does with that is @ref Echo below. Kept apart from
    /// @ref Instrument rather than made a property of it, because a plugin that
    /// produces MIDI is a different device to both engines -- it is what
    /// DeviceInfo::producesMidi says and what midiInThru is about -- and a case
    /// about an instrument's audio should not quietly be a case about its MIDI.
    InstrumentMidiOut,

    /// A stereo effect that accepts MIDI and adds an impulse at
    /// @ref kHostedEchoScale of the velocity for every note-on it receives.
    ///
    /// It exists to hear what the plugin in front of it said. Behind an
    /// @ref InstrumentMidiOut it renders the instrument's MIDI output as audio,
    /// at a level nothing else in the render uses, so a host that dropped the
    /// plugin's MIDI on the way to the next device renders the instrument alone
    /// and the case says so.
    Echo,
};

/// Every role, for the places that publish or enumerate all of them.
inline constexpr HostedRole kHostedRoles[]{
    HostedRole::Polarity,  HostedRole::Narrow,     HostedRole::Latency,           HostedRole::Key,
    HostedRole::Transport, HostedRole::Instrument, HostedRole::InstrumentMidiOut, HostedRole::Echo,
};

/// What @ref HostedRole::Latency reports and delays by.
///
/// Not a power of two and not a multiple of any block size the corpus renders
/// at, so a compensation that happened to be right only on aligned boundaries
/// is wrong here.
inline constexpr int kHostedLatencySamples = 333;

/// The bar the transport plugin's sine is locked to, in quarter notes.
inline constexpr double kHostedTransportBeats = 4.0;

/// Its level. Below full scale so a host that summed it with something else
/// does not clip the comparison.
inline constexpr float kHostedTransportLevel = 0.5f;

/// What @ref HostedRole::Echo renders, relative to the velocity it received.
///
/// A quarter, which is not what any instrument here renders, so the two are
/// separable in one buffer.
inline constexpr float kHostedEchoScale = 0.25f;

/// The vendor every description carries. A vendor of our own rather than a
/// plausible one, so nothing on a developer's machine can be confused for it.
inline constexpr const char* kHostedManufacturer = "MAGDA Null Diff";

/// The format's name, which is what a description carries and what the format
/// manager dispatches on.
///
/// Its own name rather than "VST3". Two formats answering to one name are told
/// apart only by which of them claims a file, which would make every creation
/// in the binary depend on the real VST3 format declining a path that is not a
/// bundle. The lookup a project resolves through does not need the name to
/// agree: a device saved with an identifier matches on identity, which is the
/// first pass and the exact one (ExternalPluginLookup.hpp).
inline constexpr const char* kHostedFormatName = "Null Diff";

/// The description @p role publishes, as the scan would hold it.
juce::PluginDescription hostedDescription(HostedRole role);

/**
 * @brief The model device naming that plugin, as a project would save it.
 *
 * `uniqueId` is the description's own identifier string, which is what makes
 * the device resolve by identity rather than by name: it is the first pass of
 * the lookup and the only one that cannot return a different plugin
 * (ExternalPluginLookup.hpp).
 *
 * The channel counts and the MIDI flags are the plugin's own, declared here as
 * well because the plan reads them off the model while the incumbent reads them
 * off the live instance. Both have to say the same thing or a case measures the
 * disagreement between two declarations rather than what the engines do
 * (NullDiffGain.hpp).
 */
magda::DeviceInfo hostedDevice(magda::DeviceId id, HostedRole role);

/**
 * @brief Put the wrapper pair on @p device at the levels a project saved.
 *
 * The fork gives every external plugin a dry level and a wet level the plugin
 * never declared, at slots zero and one in front of its own parameters, and
 * MAGDA persists both (DeviceInfo::wrapperParameters). A device that leaves
 * them off is a device at the pair's own default, which is fully wet: the
 * absence is a value, so a case that wants one says so.
 */
void setHostedMix(magda::DeviceInfo& device, float dry, float wet);

/**
 * @brief Register the format and its descriptions, once per manager.
 *
 * Both legs are handed the same two objects for the reason the corpus already
 * hands them one scan: two legs that each found their own copy of a plugin
 * would be rendering two projects and calling the difference an engine bug.
 *
 * Idempotent, because the runner calls it per case and the tests that build
 * their own scan build one per project. A second call adds neither a second
 * format nor a second description.
 */
void installHostedPlugins(juce::AudioPluginFormatManager& formats,
                          juce::KnownPluginList& knownPlugins);

/**
 * @brief A scan holding the corpus's own plugins and nothing else.
 *
 * For the tests that drive the native leg on their own, where there is no
 * engine to borrow a format manager from and no reason to want one: these
 * plugins are in the binary, so a manager and a list built here hold exactly
 * what the runner's engine-owned pair holds.
 *
 * A local rather than a static, and the reason is JUCE's own bookkeeping: a
 * format manager that outlives main() is reported as a leaked
 * AudioPluginFormat, which is a real complaint about a real object and would
 * arrive in every future run of a binary that owns one, having nothing to do
 * with what it was measuring. Building one costs a format and its descriptions.
 *
 * A real project's plugin is still absent from it, still skipped by whatever
 * asks, and still named when it is.
 */
struct HostedScan {
    HostedScan() {
        installHostedPlugins(formats, knownPlugins);
    }

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList knownPlugins;
};

}  // namespace magda::nulldiff
