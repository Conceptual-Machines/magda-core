#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>
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
 * A VST3 that refuses the preset falls through to the chunk, which overwrites
 * whatever the refusal left behind. With no chunk to fall through to there is
 * nothing left that describes the instance, and this reports Failed rather than
 * Baseline: a refused preset is not a no-op (Vst3PresetOutcome::Refused), so the
 * parameter array underneath it is no longer a description of the plugin.
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
struct Vst3PresetRead {
    /// The preset the instance wrote. Empty for a plugin of another format, and
    /// also for a VST3 that could not write one.
    juce::MemoryBlock preset;

    /// Whether the instance answered the VST3 extension at all.
    ///
    /// The half that cannot be inferred from the block. An empty preset from a
    /// VST3 is a plugin that failed to describe its patch, and an empty one from
    /// an AU is a question that was never asked; a caller that treated them the
    /// same would either discard an imported project's identity or keep a patch
    /// it knows is out of date.
    bool isVst3 = false;
};

Vst3PresetRead readVst3Preset(const juce::AudioPluginInstance& instance);

/// What became of a .vstpreset handed to an instance.
enum class Vst3PresetOutcome {
    /// A plugin of another format. Nothing was asked of it and nothing moved.
    NotVst3,

    /// The plugin took the patch.
    Applied,

    /// A VST3 that would not take it, and which may have moved partway doing so.
    ///
    /// Not a no-op, which is the whole reason this is not a bool. Steinberg's
    /// loader restores the component's state and then the controller's, and it
    /// returns false if the second fails after the first succeeded, so a refused
    /// preset can leave a plugin holding half of one. A caller with nothing
    /// authoritative to apply afterwards has an instance nobody can describe.
    Refused,
};

/**
 * @brief Hand @p preset to @p instance as a .vstpreset.
 *
 * It is the same third-party state handler applySavedPluginState() guards,
 * reached through the format's own preset call rather than through the chunk, so
 * a throw is caught and reported as a refusal for the same reason: what the
 * plugin holds afterwards is not knowable from here.
 */
Vst3PresetOutcome writeVst3Preset(juce::AudioPluginInstance& instance,
                                  const juce::MemoryBlock& preset);

/**
 * @brief Refresh @p device's portable VST3 records from @p instance.
 *
 * The .vstpreset a DAWproject export writes and the class id other hosts match
 * on, both read off the live instance and neither part of native state.
 *
 * A no-op for a plugin that is not a VST3, which leaves whatever the project
 * already carried rather than clearing it: a project imported from another host
 * keeps the identity it came with even where this machine's copy cannot restate
 * it.
 *
 * A VST3 that could not write one is the opposite case and is cleared. What the
 * project is holding describes an older patch, the portable record is the
 * overlay on the next load, and keeping it would restore that older patch over
 * the chunk the same save just wrote.
 *
 * The class id survives either way. It is the plugin's identity rather than its
 * patch, and the one the project was authored against is worth more than the
 * one this machine happens to have installed.
 */
void captureVst3Records(const juce::AudioPluginInstance& instance, DeviceInfo& device);

/**
 * @brief Everything a project keeps about a plugin, read off one in one go.
 *
 * Data, and nothing but data: no reference to the instance, no lock, nothing
 * that has to still be alive when this is written to a model. That is what lets
 * reading the plugin and writing the model be two separate moments, which is
 * what the caller needs in order to check between them that the device it read
 * is still the device it is about to write (PluginAssignments.hpp) -- and what
 * a plugin living in another process would need in order to send one at all.
 */
struct ExternalPluginSnapshot {
    /// The chunk, in the encoding DeviceInfo::pluginState holds. Empty for a
    /// plugin with nothing to say, which the model records as absent.
    juce::String pluginState;

    /// Every parameter a project addresses, at its live value.
    std::vector<RestoredParameter> parameters;

    /// The portable VST3 records, and what the read learned about their absence.
    Vst3PresetRead portable;
};

/**
 * @brief Read what @p instance holds: chunk, parameter values, VST3 records.
 *
 * Saving, and the mirror of applySavedPluginState(). What it reads is what the
 * fork writes for the same instance -- the chunk from getStateInformation(),
 * base64 in juce::MemoryBlock's own encoding -- so a project saved under either
 * engine opens under the other.
 *
 * It reads and does not write, for the same reason the restore hands its
 * corrections back rather than applying them: the model is the caller's, and a
 * function holding a plugin is the wrong place to be deciding a project's
 * contents. Symmetry aside, it is what keeps the commit a separate step the
 * caller can refuse.
 *
 * Message thread, and the plugin is suspended across the whole read the way the
 * fork holds its processMutex across one: a plugin asked to describe itself
 * mid-block is entitled to answer with half of one state and half of another,
 * and that applies to its parameter values and its portable preset as much as to
 * its chunk.
 *
 * Suspension is only worth anything because the host honours it. A block that
 * arrives while this is in flight passes through without touching the plugin at
 * all, parameter writes included (EngineExternalDevice::process), which is what
 * makes suspendProcessing() wait for one already in progress rather than merely
 * set a flag nobody reads.
 *
 * Nullopt when the plugin threw describing itself. The records have to agree
 * with each other -- the array is the baseline the chunk overlays -- so a read
 * that can only answer part of the question answers none of it, and the project
 * keeps the last set that did agree.
 */
std::optional<ExternalPluginSnapshot> captureExternalPluginState(
    juce::AudioPluginInstance& instance);

/**
 * @brief Write @p snapshot into @p device.
 *
 * The other half, and the only half that touches a model. Nothing here reaches
 * a plugin, so a caller may put whatever it likes between the two: the check
 * that the device is still the one that was read, a hop between processes, or
 * nothing at all.
 */
void applyCapturedPluginState(DeviceInfo& device, const ExternalPluginSnapshot& snapshot);

}  // namespace magda
