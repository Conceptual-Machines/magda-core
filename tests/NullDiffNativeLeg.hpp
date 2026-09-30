#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <map>
#include <string>
#include <vector>

#include "NullDiffCase.hpp"
#include "NullDiffCompare.hpp"

/**
 * @file NullDiffNativeLeg.hpp
 * @brief A null-diff case rendered through the native engine (#2040).
 *
 * The engine's own offline render, compiler and voice pool. Nothing here
 * reimplements a step of it: a harness that rendered through a second
 * implementation would prove things about that implementation.
 *
 * What this file does own is the host side the engine deliberately does not
 * have. A snapshot carries paths because that is what the model holds, and
 * turning one into a reader is the host's job (io/AudioFileReader.hpp), so the
 * factory that opens a WAV lives here. Binding a Device op is the same kind of
 * job and is no longer done here: the app's own factory answers for every
 * device that has moved to the SDK (EngineDeviceFactory.hpp). What is left in
 * this file is the corpus's own two -- the gain and the impulse instrument,
 * which are in no catalog because they were written for the corpus -- and the
 * stand-in for a device the corpus does not run (#2174).
 */

namespace juce {
class AudioPluginFormatManager;
class KnownPluginList;
}  // namespace juce

namespace magda::nulldiff {

/**
 * @brief The machine's plugin scan, for a case whose project hosts one.
 *
 * The engine does not go looking for plugins and could not: which plugin a
 * project meant is answered against a scan, and the scan belongs to the host.
 * So the leg is handed one rather than finding one, and the corpus hands every
 * case the same list, so every case resolves against one scan.
 *
 * Absent is the normal case and not an error: the code-built corpus hosts no
 * plugins, and a machine with no scan has none to host. What it must not be is
 * silent -- a project rendered without its plugin is a different project, so a
 * case that names one and cannot have it is reported unmeasurable rather than
 * compared (#2175).
 */
struct InstalledPlugins {
    juce::AudioPluginFormatManager* formats = nullptr;
    const juce::KnownPluginList* knownPlugins = nullptr;

    bool available() const {
        return formats != nullptr && knownPlugins != nullptr;
    }
};

struct NativeRender {
    juce::AudioBuffer<float> audio;

    /// Every capture's events, in timeline order. What the report prints.
    MidiStream midi;

    /// The same events, kept apart by the track that received them.
    ///
    /// A flat stream cannot answer the question a project with two instruments
    /// asks. A MidiEvent carries no identity beyond its bytes and its position,
    /// so two synths that received each other's notes produce the same aggregate
    /// as two that received their own: comparing the flat stream would certify
    /// the capture landing on the wrong track. Compared per track, that is the
    /// finding it should be.
    std::map<TrackId, MidiStream> midiByTrack;

    /// Set when the case could not be rendered at all, which is never reported
    /// as a residual.
    std::string failure;

    /// What the engine said it could not do. Anything in here is a finding
    /// whether or not the audio nulled: a snapshot that dropped a clip and a
    /// render that matched it are two bugs, not none.
    std::vector<std::string> diagnostics;

    /// Clips that wanted a voice and had none, and MIDI a block could not fit.
    /// Silence nobody counted is indistinguishable from silence that was meant,
    /// so both are read back and reported.
    int starvedVoices = 0;
    int droppedMidiEvents = 0;

    /// What each external plugin the render actually reached resolved to on
    /// this machine: name, version and format.
    ///
    /// Read back from the scan rather than declared, because a version is a
    /// fact about the machine. It goes on the case's environment line, where a
    /// residual that is really a plugin update can be seen for what it is
    /// (CaseEnvironment::plugins).
    ///
    /// Only the devices the plan reached and the factory built. A plugin on a
    /// bypassed chain is not instantiated, and a report claiming a project ran
    /// one it never loaded would be worse than one that said nothing.
    std::vector<std::string> plugins;

    /// The most material any of this case's stretchers is primed with, in
    /// samples of the reading.
    ///
    /// This is what a stretched case predicts its shift from, and it comes from
    /// the engine rather than from a number written down here: a constant in
    /// the corpus would go stale the moment the stretcher's preset changed.
    int primingSamples = 0;
};

/// Render @p value through the native engine, over its own beat range.
///
/// @p installed is the scan an external plugin is resolved against. A case
/// whose project names one without it renders nothing for that device and says
/// so in the diagnostics.
NativeRender renderNative(const Case& value, const InstalledPlugins& installed = {});

}  // namespace magda::nulldiff
