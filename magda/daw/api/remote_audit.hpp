#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace magda {
namespace remote {

/// How one audited request or connection ended.
enum class AuditOutcome {
    /// The operation ran and succeeded.
    Ok,
    /// Refused because the client's grant did not cover it. The one outcome
    /// that is about permission rather than correctness, and the reason the
    /// settings UI can show "this client tried to edit" without the user having
    /// to read a log file.
    Denied,
    /// Reached the dispatcher and came back an error — validation, not found,
    /// conflict, timeout. `detail` carries the MAGDA error code.
    Failed,
    /// A connection opened.
    Connected,
    /// A connection closed.
    Disconnected,
    /// Refused before it became a request at all: bad token, bad Origin, over a
    /// limit. `detail` says which.
    Rejected,
};

juce::String toString(AuditOutcome outcome);

/**
 * @brief One line of the record (#1860).
 *
 * Deliberately six fixed fields and a short reason, and deliberately no payload.
 * A request's input is whatever a client chose to send and its result is a
 * projection of the user's project; neither belongs in a buffer that the
 * settings UI displays and that a user may screenshot into a bug report. What is
 * here is enough to answer "who did what, and did it work" — which is what an
 * audit log is for — and nothing more.
 */
struct AuditEntry {
    /// Wall clock, milliseconds since the epoch. Wall clock rather than a
    /// steady clock precisely because this is shown to a person.
    juce::int64 timestampMs = 0;
    /// Normalised client name, the same key the grant is stored under.
    juce::String client;
    /// The transport's connection handle, so two connections from one client
    /// stay distinguishable.
    juce::String connectionId;
    /// `websocket` or `mcp`.
    juce::String transport;
    /// The operation name, or a `connection.*` pseudo-operation for lifecycle.
    juce::String operation;
    /// The client's own request id when it sent one. Empty is normal.
    juce::String requestId;
    AuditOutcome outcome = AuditOutcome::Ok;
    /// A short, already-redacted reason. Never a payload.
    juce::String detail;

    bool operator==(const AuditEntry&) const = default;

    /// For the settings UI and for tests. Not a wire format — the audit log is
    /// not exposed over the remote API, because a client that could read it
    /// would learn what every other client is doing.
    juce::var toJson() const;
};

/// Pseudo-operations, so connection lifecycle and requests share one table
/// rather than needing two views that have to be interleaved by timestamp.
inline constexpr const char* AUDIT_CONNECTION_OPEN = "connection.open";
inline constexpr const char* AUDIT_CONNECTION_CLOSE = "connection.close";
inline constexpr const char* AUDIT_CONNECTION_REJECTED = "connection.rejected";

/**
 * @brief A bounded, in-memory record of what remote clients did.
 *
 * In memory and nowhere else. A file would outlive the session that produced it,
 * would need rotation and a retention policy, and would put a description of the
 * user's editing session on disk permanently — for a local control socket, that
 * is a liability rather than a feature. The ring holds enough to diagnose what
 * just happened, which is the case that actually arises.
 *
 * Safe from any thread: entries arrive on transport threads and on the message
 * thread, and the settings UI reads from the message thread.
 */
class RemoteAuditLog {
  public:
    static constexpr std::size_t DEFAULT_CAPACITY = 512;

    explicit RemoteAuditLog(std::size_t capacity = DEFAULT_CAPACITY);

    RemoteAuditLog(const RemoteAuditLog&) = delete;
    RemoteAuditLog& operator=(const RemoteAuditLog&) = delete;

    /// Stamps `timestampMs` when the caller left it at zero, redacts `detail`,
    /// and drops the oldest entry when full.
    void record(AuditEntry entry);

    /// Oldest first.
    std::vector<AuditEntry> entries() const;

    /// The most recent `count`, newest last. What the settings UI shows.
    std::vector<AuditEntry> recent(std::size_t count) const;

    /// Entries for one client, oldest first.
    std::vector<AuditEntry> forClient(const juce::String& clientName) const;

    void clear();

    std::size_t size() const;
    std::size_t capacity() const;
    void setCapacity(std::size_t capacity);

    /// How many entries have ever been recorded, including those aged out.
    /// A UI polls this to know whether anything changed without copying the
    /// buffer, and it makes silent loss visible: `totalRecorded() > size()`
    /// means the window no longer covers the whole session.
    std::uint64_t totalRecorded() const;

  private:
    mutable std::mutex mutex_;
    std::deque<AuditEntry> entries_;
    std::size_t capacity_;
    std::uint64_t totalRecorded_ = 0;
};

// ===========================================================================
// Redaction
// ===========================================================================

/**
 * @brief Register a value that must never reach a log line or an error message.
 *
 * The bearer token and the path of the file it is published in. Registration is
 * exact-match replacement rather than pattern matching, which is the only way to
 * do this without false positives: a heuristic that scrubbed anything
 * token-shaped would also scrub plugin ids and project names, and one that
 * scrubbed anything path-shaped would mangle ordinary prose.
 *
 * Idempotent. Safe from any thread.
 */
void registerRemoteSecret(const juce::String& secret);

/// Stop masking a value — a rotated token, or one whose listener has closed.
void forgetRemoteSecret(const juce::String& secret);

/// Every registered secret, for tests.
void forgetAllRemoteSecrets();

/**
 * @brief Mask registered secrets, and any `Bearer …` credential, in `text`.
 *
 * Applied to every audit `detail` automatically, and available to any code
 * about to log something that passed near a credential. `Bearer` is handled by
 * shape as well as by registration, because a client's own malformed
 * `Authorization` header is a value MAGDA never held and therefore never
 * registered — and echoing it back into a log is exactly the accident this
 * exists to prevent.
 */
juce::String redactSecrets(const juce::String& text);

/**
 * @brief A file named without saying where it lives.
 *
 * `remote-api-4021.json` rather than the full path. For log lines that need to
 * identify a file to a user who can find it, without writing the account name
 * and directory layout of the machine into a buffer that ends up in bug reports.
 */
juce::String redactedFileName(const juce::File& file);

}  // namespace remote
}  // namespace magda
