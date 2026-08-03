#include "SourcePool.hpp"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <memory>

namespace magda {

namespace {

/// Shared reader factory. Registering the basic formats is not cheap, and a
/// project load probes every referenced file, so keep one manager alive.
juce::AudioFormatManager& probeFormatManager() {
    static juce::AudioFormatManager manager;
    static bool registered = [] {
        manager.registerBasicFormats();
        return true;
    }();
    juce::ignoreUnused(registered);
    return manager;
}

}  // namespace

SourcePool& SourcePool::getInstance() {
    static SourcePool instance;
    return instance;
}

juce::String SourcePool::canonicalKey(const juce::String& filePath) {
    if (filePath.isEmpty())
        return {};

    // Test the string before building a juce::File: the File constructor
    // asserts on a relative path, and getFullPathName() would resolve it
    // against the working directory, which is not what a project-relative path
    // means. Only canonicalise what is already absolute; anything else keys on
    // itself.
    juce::String path =
        juce::File::isAbsolutePath(filePath) ? juce::File(filePath).getFullPathName() : filePath;

#if JUCE_MAC || JUCE_WINDOWS
    return path.toLowerCase();
#else
    return path;
#endif
}

void SourcePool::probe(Source& source) const {
    if (const auto seeded = seededFacts_.find(canonicalKey(source.filePath));
        seeded != seededFacts_.end()) {
        source.durationSeconds = seeded->second.durationSeconds;
        source.sampleRate = seeded->second.sampleRate;
        return;
    }

    // Same reason as canonicalKey: a relative path is not something we can
    // open, and constructing a juce::File from one asserts.
    if (!juce::File::isAbsolutePath(source.filePath))
        return;

    const juce::File file(source.filePath);
    if (!file.existsAsFile())
        return;

    std::unique_ptr<juce::AudioFormatReader> reader(probeFormatManager().createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
        return;

    source.sampleRate = reader->sampleRate;
    source.durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
}

SourceId SourcePool::acquire(const juce::String& filePath) {
    if (filePath.isEmpty())
        return INVALID_SOURCE_ID;

    const auto key = canonicalKey(filePath);
    if (const auto it = idByPathKey_.find(key); it != idByPathKey_.end())
        return it->second;

    Source source;
    source.id = nextId_++;
    source.filePath = filePath;
    probe(source);

    idByPathKey_[key] = source.id;
    sources_[source.id] = std::move(source);
    return idByPathKey_[key];
}

SourceId SourcePool::findByPath(const juce::String& filePath) const {
    if (filePath.isEmpty())
        return INVALID_SOURCE_ID;
    const auto it = idByPathKey_.find(canonicalKey(filePath));
    return it != idByPathKey_.end() ? it->second : INVALID_SOURCE_ID;
}

const Source* SourcePool::get(SourceId id) const {
    const auto it = sources_.find(id);
    return it != sources_.end() ? &it->second : nullptr;
}

Source* SourcePool::getMutable(SourceId id) {
    const auto it = sources_.find(id);
    return it != sources_.end() ? &it->second : nullptr;
}

SourceId SourcePool::insert(const Source& source) {
    if (source.filePath.isEmpty() || source.id == INVALID_SOURCE_ID)
        return INVALID_SOURCE_ID;

    const auto key = canonicalKey(source.filePath);
    if (const auto existing = idByPathKey_.find(key); existing != idByPathKey_.end())
        return existing->second;

    sources_[source.id] = source;
    idByPathKey_[key] = source.id;
    nextId_ = std::max(nextId_, source.id + 1);
    return source.id;
}

std::vector<Source> SourcePool::snapshot() const {
    std::vector<Source> out;
    out.reserve(sources_.size());
    for (const auto& [id, source] : sources_)
        out.push_back(source);
    std::sort(out.begin(), out.end(), [](const Source& a, const Source& b) { return a.id < b.id; });
    return out;
}

void SourcePool::retainOnly(const std::unordered_set<SourceId>& live) {
    for (auto it = sources_.begin(); it != sources_.end();) {
        if (live.count(it->first) > 0) {
            ++it;
            continue;
        }
        idByPathKey_.erase(canonicalKey(it->second.filePath));
        it = sources_.erase(it);
    }
}

void SourcePool::clear() {
    sources_.clear();
    idByPathKey_.clear();
    nextId_ = 1;
}

bool SourcePool::resolveFacts(SourceId id) {
    auto* source = getMutable(id);
    if (source == nullptr)
        return false;

    const bool wasResolved = source->isResolved();
    probe(*source);
    return !wasResolved && source->isResolved();
}

bool SourcePool::relink(SourceId id, const juce::String& newFilePath) {
    auto* source = getMutable(id);
    if (source == nullptr || newFilePath.isEmpty())
        return false;

    const bool wasResolved = source->isResolved();

    idByPathKey_.erase(canonicalKey(source->filePath));
    source->filePath = newFilePath;
    source->sampleRate = 0.0;
    source->durationSeconds = 0.0;
    probe(*source);
    idByPathKey_[canonicalKey(newFilePath)] = id;

    return !wasResolved && source->isResolved();
}

void SourcePool::setNextId(int nextId) {
    nextId_ = std::max(1, nextId);
}

void SourcePool::seedFactsForTesting(const juce::String& filePath, double durationSeconds,
                                     double sampleRate) {
    seededFacts_[canonicalKey(filePath)] = {durationSeconds, sampleRate};
}

void SourcePool::clearSeededFactsForTesting() {
    seededFacts_.clear();
}

}  // namespace magda
