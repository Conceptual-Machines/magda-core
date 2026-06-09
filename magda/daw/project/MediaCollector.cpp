#include "MediaCollector.hpp"

#include "../audio/AudioThumbnailManager.hpp"
#include "../audio/plugins/DrumGridPlugin.hpp"
#include "../audio/plugins/MagdaSamplerPlugin.hpp"
#include "../core/ClipInfo.hpp"
#include "../core/ClipManager.hpp"
#include "../core/TrackManager.hpp"
#include "../engine/TracktionEngineWrapper.hpp"
#include "ProjectManager.hpp"

namespace magda {

namespace te = tracktion;

namespace {

// Resolve the running edit, or nullptr if the engine isn't a TracktionEngineWrapper.
te::Edit* getEdit() {
    if (auto* wrapper =
            dynamic_cast<TracktionEngineWrapper*>(TrackManager::getInstance().getAudioEngine()))
        return wrapper->getEdit();
    return nullptr;
}

// Bundled resources roots a factory sample could live under. Mirrors
// DrumkitManager's bundled-dir probe but stops at the resources parent so any
// shipped sample (not just drumkits) is recognised.
juce::Array<juce::File> factoryRoots() {
    auto appFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);

    juce::Array<juce::File> roots;
#if JUCE_MAC
    roots.add(appFile.getChildFile("Contents/Resources"));
#endif
#if JUCE_LINUX
    if (auto real = juce::File("/proc/self/exe").getLinkedTarget(); real.exists())
        roots.add(real.getParentDirectory());
#endif
    roots.add(appFile.getParentDirectory());

    // Dev-tree: the binary lives deep in cmake-build-*, walk up to the repo's
    // resources dir so collecting in a dev build also leaves factory kits alone.
    auto walk = appFile.getParentDirectory();
    for (int i = 0; i < 8 && walk.exists(); ++i) {
        auto maybe = walk.getChildFile("resources");
        if (maybe.isDirectory()) {
            roots.add(maybe);
            break;
        }
        walk = walk.getParentDirectory();
    }
    return roots;
}

// Pick a destination filename in `dir` that collides neither with an existing
// file nor with a name already handed out this run (files aren't created yet).
juce::File uniqueDest(const juce::File& dir, const juce::File& source,
                      juce::StringArray& usedNames) {
    auto base = source.getFileNameWithoutExtension();
    auto ext = source.getFileExtension();
    auto name = base + ext;
    int n = 2;
    while (usedNames.contains(name) || dir.getChildFile(name).existsAsFile()) {
        name = base + " (" + juce::String(n++) + ")" + ext;
    }
    usedNames.add(name);
    return dir.getChildFile(name);
}

}  // namespace

bool MediaCollector::isFactoryFile(const juce::File& file) {
    for (const auto& root : factoryRoots())
        if (root != juce::File() && file.isAChildOf(root))
            return true;
    return false;
}

MediaCollector::Plan MediaCollector::scan() {
    Plan plan;

    const auto mediaDir = ProjectManager::getInstance().getMediaDirectory();
    const auto importedDir = ProjectManager::getInstance().getImportedDirectory();
    juce::StringArray usedNames;

    // path string -> index into plan.items, so references to the same source
    // collapse onto one copy.
    std::map<juce::String, size_t> indexByPath;

    // Classify one referenced path; returns the item index to attach a reference
    // to, or -1 if the path should be skipped (and bumps the relevant counter).
    auto resolve = [&](const juce::String& rawPath) -> long {
        if (rawPath.isEmpty())
            return -1;
        const juce::File file(rawPath);

        if (mediaDir != juce::File() && file.isAChildOf(mediaDir)) {
            ++plan.alreadyLocalCount;
            return -1;
        }
        if (isFactoryFile(file)) {
            ++plan.factoryCount;
            return -1;
        }
        if (!file.existsAsFile()) {
            ++plan.missingCount;
            return -1;
        }

        const auto key = file.getFullPathName();
        auto it = indexByPath.find(key);
        if (it != indexByPath.end())
            return static_cast<long>(it->second);

        Item item;
        item.source = file;
        item.dest = uniqueDest(importedDir, file, usedNames);
        plan.items.push_back(std::move(item));
        const auto idx = plan.items.size() - 1;
        indexByPath[key] = idx;
        return static_cast<long>(idx);
    };

    // 1. Audio clip sources.
    for (const auto& clip : ClipManager::getInstance().getClips()) {
        if (!clip.isAudio())
            continue;
        const auto idx = resolve(clip.audio().source.filePath);
        if (idx >= 0)
            plan.items[static_cast<size_t>(idx)].clipRefs.push_back(clip.id);
    }

    // 2 + 3. Samplers (standalone, incl. inside instrument racks) and drum pads.
    if (auto* edit = getEdit()) {
        auto attachSampler = [&](te::Plugin* plugin) {
            auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(plugin);
            if (sampler == nullptr)
                return;
            const auto idx = resolve(sampler->getSampleFile().getFullPathName());
            if (idx >= 0)
                plan.items[static_cast<size_t>(idx)].samplerRefs.push_back(
                    te::Plugin::Ptr(sampler));
        };

        for (auto plugin : te::getAllPlugins(*edit, true)) {
            attachSampler(plugin);

            // Drum-pad samplers live inside the DrumGridPlugin's own chains, not
            // in the rack list, so reach them explicitly.
            if (auto* drumGrid = dynamic_cast<daw::audio::DrumGridPlugin*>(plugin)) {
                for (const auto& chain : drumGrid->getChains())
                    for (auto& nested : chain->plugins)
                        attachSampler(nested.get());
            }
        }
    }

    return plan;
}

void MediaCollector::copy(Plan& plan, const std::function<void(float)>& onProgress,
                          std::atomic<bool>& cancel) {
    ProjectManager::getInstance().getImportedDirectory().createDirectory();

    const auto total = static_cast<int>(plan.items.size());
    for (int i = 0; i < total; ++i) {
        if (cancel.load())
            return;
        auto& item = plan.items[static_cast<size_t>(i)];
        item.copied = item.source.copyFileTo(item.dest) && item.dest.existsAsFile();
        if (onProgress)
            onProgress(static_cast<float>(i + 1) / static_cast<float>(total));
    }
}

MediaCollector::Summary MediaCollector::apply(const Plan& plan) {
    Summary summary;
    summary.missing = plan.missingCount;
    summary.alreadyLocal = plan.alreadyLocalCount;
    summary.factory = plan.factoryCount;

    auto& clipManager = ClipManager::getInstance();
    auto& thumbs = AudioThumbnailManager::getInstance();

    for (const auto& item : plan.items) {
        if (!item.copied) {
            summary.failed += 1;
            continue;
        }

        const auto newPath = item.dest.getFullPathName();

        for (auto clipId : item.clipRefs) {
            if (auto* clip = clipManager.getClip(clipId); clip != nullptr && clip->isAudio()) {
                const auto oldPath = clip->audio().source.filePath;
                clip->audio().source.filePath = newPath;
                thumbs.invalidateFile(oldPath);
                thumbs.invalidateFile(newPath);
                clipManager.forceNotifyClipPropertyChanged(clipId);
            }
        }

        for (auto& samplerRef : item.samplerRefs) {
            if (auto* sampler = dynamic_cast<daw::audio::MagdaSamplerPlugin*>(samplerRef.get()))
                sampler->loadSample(item.dest);
        }

        summary.collected += 1;
    }

    if (summary.collected > 0)
        ProjectManager::getInstance().markDirty();

    return summary;
}

juce::String MediaCollector::Summary::toMessage() const {
    juce::StringArray parts;
    parts.add(juce::String(collected) + (collected == 1 ? " file collected" : " files collected"));
    if (failed > 0)
        parts.add(juce::String(failed) + (failed == 1 ? " failed to copy" : " failed to copy"));
    if (missing > 0)
        parts.add(juce::String(missing) + " missing on disk");
    if (alreadyLocal > 0)
        parts.add(juce::String(alreadyLocal) + " already in the project");
    if (factory > 0)
        parts.add(juce::String(factory) + " factory (left in place)");
    return parts.joinIntoString(", ") + ".";
}

}  // namespace magda
