#include "remote_file_handles.hpp"

#include <algorithm>
#include <random>

namespace magda::remote {

const char* toString(RemoteFileCapability capability) {
    switch (capability) {
        case RemoteFileCapability::ProjectSource:
            return "project_source";
        case RemoteFileCapability::ProjectDestination:
            return "project_destination";
        case RemoteFileCapability::AudioDestination:
            return "audio_destination";
    }
    return "project_source";
}

juce::var toJson(const RemoteFileHandleDto& handle) {
    auto* value = new juce::DynamicObject();
    value->setProperty("id", handle.id);
    value->setProperty("capability", handle.capability);
    value->setProperty("name", handle.name);
    value->setProperty("createdAtMs", handle.createdAtMs);
    value->setProperty("expiresAtMs", handle.expiresAtMs);
    value->setProperty("overwriteApproved", handle.overwriteApproved);
    return value;
}

struct RemoteFileHandleRegistry::Entry {
    RemoteFileHandleDto dto;
    juce::String ownerClientId;
    juce::String ownerClientName;
    RemoteFileCapability capability = RemoteFileCapability::ProjectSource;
    juce::File file;
};

RemoteFileHandleRegistry::RemoteFileHandleRegistry() : RemoteFileHandleRegistry(Options{}) {}

RemoteFileHandleRegistry::RemoteFileHandleRegistry(Options options) : options_(options) {}

RemoteFileHandleRegistry::~RemoteFileHandleRegistry() = default;

juce::int64 RemoteFileHandleRegistry::nowMs() {
    return juce::Time::currentTimeMillis();
}

juce::String RemoteFileHandleRegistry::makeId() {
    std::random_device entropy;
    juce::String id("file_");
    for (int index = 0; index < 4; ++index)
        id += juce::String::toHexString(static_cast<int>(entropy())).paddedLeft('0', 8);
    return id;
}

void RemoteFileHandleRegistry::pruneLocked() {
    const auto now = nowMs();
    std::erase_if(entries_, [now](const Entry& entry) { return entry.dto.expiresAtMs <= now; });
    while (entries_.size() > options_.maxHandles)
        entries_.erase(entries_.begin());
}

juce::String RemoteFileHandleRegistry::approve(const juce::String& ownerClientId,
                                               const juce::String& ownerClientName,
                                               RemoteFileCapability capability,
                                               const juce::File& file, bool overwriteApproved) {
    if (ownerClientId.isEmpty() || file.getFullPathName().isEmpty())
        return {};

    Entry entry;
    entry.dto.id = makeId();
    entry.dto.capability = toString(capability);
    entry.dto.name = file.getFileName().substring(0, 256);
    entry.dto.createdAtMs = nowMs();
    entry.dto.expiresAtMs =
        entry.dto.createdAtMs + static_cast<juce::int64>(options_.lifetime.count());
    entry.dto.overwriteApproved = overwriteApproved;
    entry.ownerClientId = ownerClientId;
    entry.ownerClientName = ownerClientName;
    entry.capability = capability;
    entry.file = file;

    const std::scoped_lock lock(mutex_);
    if (shutdown_)
        return {};
    pruneLocked();
    while (std::ranges::any_of(
        entries_, [&](const Entry& existing) { return existing.dto.id == entry.dto.id; }))
        entry.dto.id = makeId();
    const auto id = entry.dto.id;
    entries_.push_back(std::move(entry));
    pruneLocked();
    return id;
}

std::vector<RemoteFileHandleDto> RemoteFileHandleRegistry::list(const juce::String& ownerClientId) {
    const std::scoped_lock lock(mutex_);
    pruneLocked();
    std::vector<RemoteFileHandleDto> result;
    for (const auto& entry : entries_)
        if (entry.ownerClientId == ownerClientId)
            result.push_back(entry.dto);
    return result;
}

std::optional<ResolvedRemoteFileHandle> RemoteFileHandleRegistry::resolve(
    const juce::String& id, const juce::String& ownerClientId, RemoteFileCapability capability,
    Error& error) {
    const std::scoped_lock lock(mutex_);
    pruneLocked();
    const auto found =
        std::ranges::find(entries_, id, [](const Entry& entry) { return entry.dto.id; });
    if (found == entries_.end() || found->ownerClientId != ownerClientId) {
        error = Error{ErrorCode::NotFound, "file handle not found", {}};
        return std::nullopt;
    }
    if (found->capability != capability) {
        error = Error{
            ErrorCode::PermissionDenied, "file handle does not grant the required capability", {}};
        return std::nullopt;
    }
    return ResolvedRemoteFileHandle{found->file, found->dto.overwriteApproved};
}

bool RemoteFileHandleRegistry::revoke(const juce::String& id, const juce::String& ownerClientId,
                                      Error& error) {
    const std::scoped_lock lock(mutex_);
    pruneLocked();
    const auto found =
        std::ranges::find(entries_, id, [](const Entry& entry) { return entry.dto.id; });
    if (found == entries_.end() || found->ownerClientId != ownerClientId) {
        error = Error{ErrorCode::NotFound, "file handle not found", {}};
        return false;
    }
    entries_.erase(found);
    return true;
}

void RemoteFileHandleRegistry::ownerDisconnected(const juce::String& ownerClientId) {
    const std::scoped_lock lock(mutex_);
    std::erase_if(entries_,
                  [&](const Entry& entry) { return entry.ownerClientId == ownerClientId; });
}

void RemoteFileHandleRegistry::revokeClient(const juce::String& ownerClientName) {
    const std::scoped_lock lock(mutex_);
    std::erase_if(entries_,
                  [&](const Entry& entry) { return entry.ownerClientName == ownerClientName; });
}

int RemoteFileHandleRegistry::countForClient(const juce::String& ownerClientName) {
    const std::scoped_lock lock(mutex_);
    pruneLocked();
    return static_cast<int>(std::ranges::count(entries_, ownerClientName, &Entry::ownerClientName));
}

void RemoteFileHandleRegistry::shutdown() {
    const std::scoped_lock lock(mutex_);
    shutdown_ = true;
    entries_.clear();
}

}  // namespace magda::remote
