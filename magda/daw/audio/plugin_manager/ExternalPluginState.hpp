#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

#include "core/DeviceInfo.hpp"

/**
 * @file ExternalPluginState.hpp
 * @brief A project's record of a plugin and a live instance, in both
 *        directions, once (#2243, #2244).
 *
 * A project stores an external plugin twice over, and the two records are not
 * the same shape. One is an array of parameter values, which is what the UI
 * draws, what automation addresses and what a preset browser can read. The
 * other is the chunk the plugin itself wrote, which is fuller: it carries the
 * current program, a sampler's loaded samples and key mappings, and every value
 * the plugin never exposed as a parameter at all.
 *
 * Neither is redundant and the order between them is load-bearing, so it lives
 * here rather than in whichever engine happens to be loading the project. The
 * incumbent's sequence, which this is:
 *
 * 1. **The parameter array, as a baseline.** It is what a project has for a
 *    plugin with no chunk -- one that stores nothing, or whose chunk this build
 *    of it refuses -- and skipping it leaves such a plugin on its factory
 *    defaults with the project's own values unused in the model.
 * 2. **The chunk, over the top.** Where the two disagree the chunk is right and
 *    the array is stale, which is what every project saved by a MAGDA old
 *    enough to have written a stale array looks like
 *    (test_external_plugin_state_restore_juce.cpp).
 * 3. **A snapshot of what the plugin now holds**, handed back to whoever owns
 *    the model. That is the step the fork spends `valueChangedByPlugin` on, and
 *    the reason it exists is the same here: after a restore the model's array
 *    and the live plugin can disagree, and everything downstream -- an
 *    automation lane's base value, a knob's position, the values a render
 *    writes every block -- reads the model. Correcting it is the host's job and
 *    the message thread's; what this returns is what to correct it to.
 *
 * The third step is what keeps the engine out of the guessing business. Without
 * it a device would have to work out, from the numbers alone, whether a
 * parameter it is about to write is the project's own value or a stale one the
 * chunk has already corrected -- and no comparison can tell those apart from an
 * automation lane that happens to be passing through the same value.
 *
 * Saving is the same three records read the other way, and it is here for the
 * same reason: a project saved while the native engine holds the instance has
 * to be a project the incumbent can open, so what goes back into the model is
 * what the fork's own flushPluginStateToValueTree() would have written. One
 * definition of what a project keeps about a plugin, read and written by
 * whichever engine happens to be holding it.
 *
 * There is a third record beside those two, and it belongs to neither engine:
 * the portable .vstpreset a DAWproject carries, which is how a VST3's patch
 * crosses between hosts that share no chunk format. It is the overlay when a
 * project has one, because a project that has one has just been imported and
 * has no chunk of its own yet, and it is consumed rather than kept: applying it
 * is what turns it into native state, and a second application over a chunk
 * written since would undo the patch it was imported to become.
 */

namespace magda {

/**
 * @brief The plugin's parameters in the order a project's indices address them.
 *
 * Index zero is the slot-level dry level and index one the wet level, both
 * null: they are the host's own numbers, the plugin has never heard of them,
 * and no chunk carries them. From two on it is the plugin's own parameters, in
 * plugin order, skipping any it says are not automatable.
 *
 * That shape is not a choice. It is the list the incumbent builds
 * (ExternalPlugin::buildParameterList) and therefore what MAGDA saved in
 * ParameterInfo::paramIndex, so anything reading a project's parameter values
 * back onto a live plugin has to reproduce it. One definition, because two
 * would eventually differ by one and put a project's cutoff on its resonance.
 */
std::vector<juce::AudioProcessorParameter*> hostParameterOrder(
    const juce::AudioPluginInstance& instance);

/** One parameter's live value, addressed the way a project addresses it. */
struct RestoredParameter {
    int paramIndex = 0;

    /// Normalised, which is the unit an external plugin's parameters are in on
    /// both sides of this: what the plugin takes, and what the model stores.
    float value = 0.0f;
};

/** How far applySavedPluginState() got. */
enum class SavedStateOutcome {
    /// The parameter array is on the instance and no chunk went over it: the
    /// device saved none, or what it saved was not decodable. A plugin that
    /// takes a chunk and quietly ignores it lands here too, because from
    /// outside it is the same thing.
    Baseline,

    /// The array, then the chunk, both applied.
    Restored,

    /// The array, then the portable .vstpreset the project was imported with,
    /// which the plugin took. The caller publishes a model with
    /// DeviceInfo::vst3Preset cleared: it has done its one job and a project
    /// that kept it would apply it again over whatever was saved since.
    RestoredFromPreset,

    /// The plugin threw partway through reading its own state. What it holds
    /// now is not the baseline, not the chunk, and not knowable from here.
    Failed,
};

/**
 * @brief Apply what @p device saved onto @p instance: array, then overlay.
 *
 * Steps one and two above.
 *
 * The overlay is the portable .vstpreset when the project carries one and the
 * instance is a VST3 that takes it, and the plugin's own chunk otherwise. The
 * two are alternatives rather than a sequence: each is a complete statement of
 * the patch, and a project holds the preset only until the first load turns it
 * into a chunk of its own.
 *
 * A plugin's own state handler is third-party code running in-process, and a
 * corrupt chunk is exactly the input it is least likely to have been tested
 * against, so it is called inside a catch-all. What the catch-all buys is that
 * the host survives, and nothing more: a plugin is free to have loaded half a
 * preset, switched program and swapped a sample before it threw, and no
 * sequence of parameter writes puts any of that back. So a throw is reported as
 * a failure rather than smoothed over, and the caller's business is to discard
 * the instance rather than to publish one whose state nobody can describe.
 */
SavedStateOutcome applySavedPluginState(juce::AudioPluginInstance& instance,
                                        const DeviceInfo& device);

/**
 * @brief What @p instance holds now, for the model to record.
 *
 * Step three. Every parameter a project addresses, at its live value, in the
 * order hostParameterOrder() defines. The wrapper pair is not in it: nothing
 * on the plugin holds those, so there is nothing to read back.
 */
std::vector<RestoredParameter> snapshotHostParameters(const juce::AudioPluginInstance& instance);

/**
 * @brief Write @p restored into @p device's own parameter records.
 *
 * The other half of step three, for a host that keeps its model in a DeviceInfo
 * rather than somewhere of its own. Off the audio thread, on the model, which
 * is the only place a value like this may be written.
 */
void applyRestoredParameters(DeviceInfo& device, const std::vector<RestoredParameter>& restored);

/**
 * @brief The .vstpreset @p instance would write, or nothing.
 *
 * Empty for anything that is not a live VST3, which is the only way to ask:
 * the format is not in the description a host holds, only in whether the
 * instance answers the VST3 extension.
 */
juce::MemoryBlock readVst3Preset(const juce::AudioPluginInstance& instance);

/**
 * @brief Hand @p preset to @p instance as a .vstpreset, and say whether it took.
 *
 * False for a non-VST3, for a preset it refuses, and for an instance that threw
 * reading it. It is the same third-party state handler applySavedPluginState()
 * guards, reached through the format's own preset call rather than through the
 * chunk.
 */
bool writeVst3Preset(juce::AudioPluginInstance& instance, const juce::MemoryBlock& preset);

/**
 * @brief Refresh @p device's portable VST3 records from @p instance.
 *
 * The .vstpreset a DAWproject export writes and the class id other hosts match
 * on, both read off the live instance and neither part of native state. A
 * no-op for a plugin that is not a VST3, which leaves whatever the project
 * already carried rather than clearing it: a project imported from another host
 * keeps the identity it came with even where this machine's copy cannot restate
 * it.
 *
 * The class id is written once and then left alone. It is the plugin's
 * identity, not its patch, and the one the project was authored against is
 * worth more than the one this machine happens to have installed.
 */
void captureVst3Records(const juce::AudioPluginInstance& instance, DeviceInfo& device);

/**
 * @brief Write what @p instance holds into @p device: chunk, array, VST3 records.
 *
 * Saving, and the mirror of applySavedPluginState(). What lands in the model is
 * what the fork writes for the same instance -- the chunk from
 * getStateInformation(), base64 in juce::MemoryBlock's own encoding, absent
 * rather than empty when the plugin has nothing to say -- so a project saved
 * under either engine opens under the other.
 *
 * Message thread, and the plugin is suspended across the read the way the fork
 * suspends it: a plugin asked to describe itself mid-block is entitled to
 * answer with half of one state and half of another.
 *
 * False when the plugin threw describing itself, and then @p device is left
 * exactly as it was. The two records have to agree with each other -- the array
 * is the baseline the chunk overlays -- so a save that can only write one of
 * them writes neither, and the project keeps the last pair that did agree.
 */
bool captureSavedPluginState(juce::AudioPluginInstance& instance, DeviceInfo& device);

}  // namespace magda
