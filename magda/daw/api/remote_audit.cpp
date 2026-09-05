#include "remote_audit.hpp"

#include <algorithm>
#include <utility>

namespace magda::remote {
namespace {

/// Registered secrets, and the lock over them. A plain function-local static
/// rather than a member of anything: redaction has to be reachable from log
/// sites that have no access to the host, which is the point of it.
std::mutex& secretsMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<juce::String>& secrets() {
    static std::vector<juce::String> values;
    return values;
}

constexpr const char* kMask = "***";

/**
 * How long a `Bearer` value has to be before it is treated as a credential.
 *
 * Sixteen, because MAGDA's own token is sixty-four hex characters and no
 * English word this appears next to in a MAGDA message is that long. The
 * boundary is a judgement, not a rule — see `redactSecrets`.
 */
constexpr int kMinimumCredentialLength = 16;

/// The characters a bearer credential is built from — RFC 7235's `token68`.
/// `.` is in the set because JWTs are built from it; the cost is that a
/// credential ending a sentence takes the full stop into the mask with it,
/// which is the harmless direction to be wrong in.
bool isCredentialCharacter(juce::juce_wchar character) {
    return juce::CharacterFunctions::isLetterOrDigit(character) || character == '-' ||
           character == '.' || character == '_' || character == '~' || character == '+' ||
           character == '/' || character == '=';
}

}  // namespace

juce::String toString(AuditOutcome outcome) {
    switch (outcome) {
        case AuditOutcome::Ok:
            return "ok";
        case AuditOutcome::Denied:
            return "denied";
        case AuditOutcome::Failed:
            return "failed";
        case AuditOutcome::Connected:
            return "connected";
        case AuditOutcome::Disconnected:
            return "disconnected";
        case AuditOutcome::Rejected:
            return "rejected";
    }
    return "unknown";
}

juce::var AuditEntry::toJson() const {
    auto* object = new juce::DynamicObject();
    object->setProperty("timestampMs", timestampMs);
    object->setProperty("client", client);
    object->setProperty("connectionId", connectionId);
    object->setProperty("transport", transport);
    object->setProperty("operation", operation);
    object->setProperty("requestId", requestId);
    object->setProperty("outcome", magda::remote::toString(outcome));
    object->setProperty("detail", detail);
    return juce::var(object);
}

// ===========================================================================
// RemoteAuditLog
// ===========================================================================

RemoteAuditLog::RemoteAuditLog(std::size_t capacity)
    : capacity_(capacity == 0 ? DEFAULT_CAPACITY : capacity) {}

void RemoteAuditLog::record(AuditEntry entry) {
    if (entry.timestampMs == 0)
        entry.timestampMs = juce::Time::getCurrentTime().toMilliseconds();
    // Unconditional, at the one point every entry passes through. Redacting at
    // the call sites instead would mean each new one is a chance to forget.
    entry.detail = redactSecrets(entry.detail);

    const std::scoped_lock lock(mutex_);
    entries_.push_back(std::move(entry));
    ++totalRecorded_;
    while (entries_.size() > capacity_)
        entries_.pop_front();
}

std::vector<AuditEntry> RemoteAuditLog::entries() const {
    const std::scoped_lock lock(mutex_);
    return {entries_.begin(), entries_.end()};
}

std::vector<AuditEntry> RemoteAuditLog::recent(std::size_t count) const {
    const std::scoped_lock lock(mutex_);
    const auto take = std::min(count, entries_.size());
    return {entries_.end() - static_cast<std::ptrdiff_t>(take), entries_.end()};
}

std::vector<AuditEntry> RemoteAuditLog::forClient(const juce::String& clientName) const {
    const std::scoped_lock lock(mutex_);
    std::vector<AuditEntry> result;
    for (const auto& entry : entries_) {
        if (entry.client == clientName)
            result.push_back(entry);
    }
    return result;
}

void RemoteAuditLog::clear() {
    const std::scoped_lock lock(mutex_);
    entries_.clear();
    // `totalRecorded_` deliberately survives: it is the "did anything change"
    // cursor the UI polls, and resetting it would make a clear look like no
    // change at all.
}

std::size_t RemoteAuditLog::size() const {
    const std::scoped_lock lock(mutex_);
    return entries_.size();
}

std::size_t RemoteAuditLog::capacity() const {
    const std::scoped_lock lock(mutex_);
    return capacity_;
}

void RemoteAuditLog::setCapacity(std::size_t capacity) {
    const std::scoped_lock lock(mutex_);
    capacity_ = capacity == 0 ? DEFAULT_CAPACITY : capacity;
    while (entries_.size() > capacity_)
        entries_.pop_front();
}

std::uint64_t RemoteAuditLog::totalRecorded() const {
    const std::scoped_lock lock(mutex_);
    return totalRecorded_;
}

// ===========================================================================
// Redaction
// ===========================================================================

void registerRemoteSecret(const juce::String& secret) {
    // Short values are refused rather than registered. A one- or two-character
    // "secret" would match somewhere in almost every message and turn the whole
    // log into asterisks — and nothing MAGDA generates is that short, so a value
    // this small is a bug at the call site.
    if (secret.length() < 8)
        return;

    const std::scoped_lock lock(secretsMutex());
    auto& values = secrets();
    if (std::find(values.begin(), values.end(), secret) == values.end())
        values.push_back(secret);
}

void forgetRemoteSecret(const juce::String& secret) {
    const std::scoped_lock lock(secretsMutex());
    auto& values = secrets();
    values.erase(std::remove(values.begin(), values.end(), secret), values.end());
}

void forgetAllRemoteSecrets() {
    const std::scoped_lock lock(secretsMutex());
    secrets().clear();
}

juce::String redactSecrets(const juce::String& text) {
    if (text.isEmpty())
        return text;

    juce::String result = text;
    {
        const std::scoped_lock lock(secretsMutex());
        for (const auto& secret : secrets())
            result = result.replace(secret, kMask, false);
    }

    // Then by shape, for a credential MAGDA never held: a client's own
    // `Authorization` header, echoed into a rejection reason. Case-insensitive
    // because the scheme is, per RFC 7235.
    //
    // The word after "bearer" is only masked when it *looks* like a credential
    // — long enough, and drawn from the character set a token uses. Without
    // that, "invalid or missing bearer token" redacts to "…bearer ***", which
    // is how a heuristic meant to protect the log ends up destroying it. This
    // cannot be tight enough to be exact, and it does not have to be: the real
    // guarantee is that MAGDA never builds a message out of a credential in the
    // first place, and registered secrets above cover the values it does hold.
    for (int from = 0;;) {
        const auto at = result.indexOfIgnoreCase(from, "bearer ");
        if (at < 0)
            break;

        const auto valueStart = at + 7;
        auto valueEnd = valueStart;
        while (valueEnd < result.length() && isCredentialCharacter(result[valueEnd]))
            ++valueEnd;

        // Advance unconditionally, whatever was found: a "bearer" with nothing
        // credential-shaped after it must not leave `from` where it was, or
        // this scans the same position for ever.
        const auto length = valueEnd - valueStart;
        if (length < kMinimumCredentialLength) {
            from = valueStart;
            continue;
        }

        result = result.substring(0, valueStart) + kMask + result.substring(valueEnd);
        from = valueStart + static_cast<int>(std::string_view(kMask).size());
    }

    return result;
}

juce::String redactedFileName(const juce::File& file) {
    return file == juce::File() ? juce::String() : file.getFileName();
}

}  // namespace magda::remote
