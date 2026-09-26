#pragma once

#include <juce_core/juce_core.h>

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

#include "remote_api.hpp"

namespace magda::remote {

enum class RemoteFileCapability { ProjectSource, ProjectDestination, AudioDestination };

struct RemoteFileHandleDto {
    juce::String id;
    juce::String capability;
    juce::String name;
    juce::int64 createdAtMs = 0;
    juce::int64 expiresAtMs = 0;
    bool overwriteApproved = false;
};

struct ResolvedRemoteFileHandle {
    juce::File file;
    bool overwriteApproved = false;
};

const char* toString(RemoteFileCapability capability);
juce::var toJson(const RemoteFileHandleDto& handle);

/** Process-local capabilities issued only after a native user file choice. */
class RemoteFileHandleRegistry {
  public:
    struct Options {
        std::size_t maxHandles = 128;
        std::chrono::milliseconds lifetime = std::chrono::minutes(30);
    };

    RemoteFileHandleRegistry();
    explicit RemoteFileHandleRegistry(Options options);
    ~RemoteFileHandleRegistry();

    juce::String approve(const juce::String& ownerClientId, const juce::String& ownerClientName,
                         RemoteFileCapability capability, const juce::File& file,
                         bool overwriteApproved = false);
    std::vector<RemoteFileHandleDto> list(const juce::String& ownerClientId);
    std::optional<ResolvedRemoteFileHandle> resolve(const juce::String& id,
                                                    const juce::String& ownerClientId,
                                                    RemoteFileCapability capability, Error& error);
    bool revoke(const juce::String& id, const juce::String& ownerClientId, Error& error);
    void ownerDisconnected(const juce::String& ownerClientId);
    void revokeClient(const juce::String& ownerClientName);
    int countForClient(const juce::String& ownerClientName);
    void shutdown();

  private:
    struct Entry;

    static juce::int64 nowMs();
    static juce::String makeId();
    void pruneLocked();

    const Options options_;
    std::mutex mutex_;
    std::vector<Entry> entries_;
    bool shutdown_ = false;
};

}  // namespace magda::remote
