#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace magda::test {

/// Every wave clip's playback file exists and nothing is still rendering it.
///
/// The check is on the render manager rather than on the file, because
/// AudioFile::isValid can go true before the job has released it, which the
/// app's own reverse path already had to learn.
inline bool proxiesReady(tracktion::Engine& engine, tracktion::Edit& edit, int& waitedFor) {
    waitedFor = 0;

    const auto ready = [&](tracktion::Clip* clip) {
        auto* audio = dynamic_cast<tracktion::AudioClipBase*>(clip);
        if (audio == nullptr)
            return true;

        const auto playbackFile = audio->getPlaybackFile();
        if (!playbackFile.isValid())
            return false;

        if (engine.getRenderManager().isProxyBeingGenerated(playbackFile)) {
            ++waitedFor;
            return false;
        }

        return true;
    };

    for (auto* track : tracktion::getAudioTracks(edit)) {
        for (auto* clip : track->getClips())
            if (!ready(clip))
                return false;

        for (auto* slot : track->getClipSlotList().getClipSlots())
            if (!ready(slot->getClip()))
                return false;
    }

    return true;
}

}  // namespace magda::test
