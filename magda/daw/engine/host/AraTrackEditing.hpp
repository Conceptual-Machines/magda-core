// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <map>
#include <memory>

#include "core/ClipManager.hpp"
#include "core/TypeIds.hpp"
#include "project/ProjectManager.hpp"
#include "transport/TempoMap.hpp"

namespace magda::daw::audio::engine_adapter {
class EngineExternalDevice;
}

/**
 * @file AraTrackEditing.hpp
 * @brief ARA plugins inserted on a track, playing that track's clips live (#1579).
 *
 * An ARA plugin on an audio track sees every arrangement clip on it as a playback region and
 * renders them at the transport's position, the way Logic or Reaper host Melodyne. One document
 * per plugin per project, shared by every track that has the plugin; each instance renders only
 * its own track's regions.
 */

namespace magda::daw::engine_host {

/** @brief Binds ARA plugins on tracks to their documents and keeps the regions current. */
class AraTrackEditing final : private ClipManagerListener,
                              private ProjectManagerListener,
                              private juce::Timer {
  public:
    /// @p tempo is read whenever the plugins are handed the project's musical context; @p known
    /// is where a loaded instance's scanned description is found.
    AraTrackEditing(juce::AudioPluginFormatManager& formats, const juce::KnownPluginList& known,
                    std::function<const engine::TempoMap&()> tempo);
    ~AraTrackEditing() override;

    /**
     * @brief Bind @p device's plugin on @p track to its document, if it offers ARA.
     *
     * Before the engine prepares it; a plugin without ARA is left alone. Message thread.
     */
    void attach(TrackId track, audio::engine_adapter::EngineExternalDevice& device);

    /** @brief Archive every document under @p mediaDirectory; a project save does this. */
    void saveArchives(const juce::File& mediaDirectory);

    /** @brief Drop the documents. After the host has let go of the devices bound to them. */
    void forgetProject();

    /** @brief The archive a plugin's document is saved to, beside the project's media. */
    static juce::File archiveFile(const juce::File& mediaDirectory, const juce::String& pluginId);

    /// Shown to the user when a plugin cannot be bound or its saved edits cannot be read.
    std::function<void(const juce::String&)> notify;

  private:
    struct Document;
    class Binding;

    void clipsChanged() override;
    void clipPropertyChanged(ClipId clipId) override;
    void projectSaved(const ProjectInfo& info) override;
    void projectPropertiesChanged() override;
    void timerCallback() override;

    std::shared_ptr<Document> documentFor(juce::AudioPluginInstance& plugin);
    void sync(Document& document);
    void report(const juce::String& message) const;

    juce::AudioPluginFormatManager& formats_;
    const juce::KnownPluginList& known_;
    std::function<const engine::TempoMap&()> tempo_;
    std::map<juce::String, std::shared_ptr<Document>> documents_;
};

}  // namespace magda::daw::engine_host
