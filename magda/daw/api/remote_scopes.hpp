#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

namespace magda {
namespace remote {

/**
 * @brief What a remote client is allowed to reach (#1860).
 *
 * Five words, shared by every transport, and the whole vocabulary: a grant is a
 * set of these, an operation requires exactly one of them, and enforcement is a
 * set-membership test in `RemoteApiService`. Adding a sixth means adding it here
 * and nowhere else.
 *
 * The split is by *what the user would regret*, not by which manager the code
 * happens to call. Editing the project and driving the transport are separable
 * because a remote that only starts and stops playback is a thing people
 * genuinely want, and it must not also be able to delete a track. Session launch
 * is separate from both because a performance controller firing clips is not
 * editing anything and is not the timeline transport either.
 *
 * ## What these are, and are not
 *
 * They are the user's intent about a *named* client, recorded so a well-behaved
 * one cannot exceed it by accident. They are not a security boundary against a
 * hostile process on the same machine: admission is the per-run bearer token,
 * that token lives in a file any process running as the user can read, and the
 * client's name is self-declared. A caller holding the token can claim any name
 * and get whatever that name was granted.
 *
 * That is a deliberate position rather than an oversight — per-client secrets
 * would be published in the same readable file and would buy nothing — and it is
 * documented in `docs/remote-api-permissions.md` so nobody mistakes this for
 * sandboxing.
 */
enum class Scope {
    /// Read anything the API projects. Every client has this; it is what being
    /// admitted at all means.
    Read,
    /// Change project content: tempo, tracks, clips, notes, devices, racks,
    /// automation, and the user's selection.
    Edit,
    /// Drive the timeline: play, stop, record-arm, loop, seek.
    Transport,
    /// Launch and stop session clips and scenes.
    Session,
    /**
     * Reach physical MIDI ports, including SysEx.
     *
     * No operation requires this today — the registry exposes no hardware MIDI
     * surface yet. It is declared now because grants are persisted: a scope
     * invented later would silently read as "not granted" on every existing
     * client, which is the correct answer, but only if the word already exists
     * when those grants are written. It also gives the settings UI a stable
     * place to show the permission before there is anything behind it.
     */
    HardwareMidi,
};

inline constexpr std::size_t SCOPE_COUNT = 5;

/**
 * @brief A set of scopes, as bits.
 *
 * Small enough to sit in `RequestContext` by value and be compared without
 * allocating, which matters: a grant is looked up and copied on every single
 * request so that revoking one takes effect on the next one rather than at the
 * next restart.
 */
class ScopeSet {
  public:
    constexpr ScopeSet() = default;

    constexpr ScopeSet(std::initializer_list<Scope> scopes) {
        for (const auto scope : scopes)
            add(scope);
    }

    constexpr ScopeSet& add(Scope scope) {
        bits_ |= bit(scope);
        return *this;
    }

    constexpr ScopeSet& remove(Scope scope) {
        bits_ &= static_cast<std::uint32_t>(~bit(scope));
        return *this;
    }

    constexpr ScopeSet& set(Scope scope, bool granted) {
        return granted ? add(scope) : remove(scope);
    }

    constexpr bool has(Scope scope) const {
        return (bits_ & bit(scope)) != 0;
    }

    constexpr bool empty() const {
        return bits_ == 0;
    }

    /// Whether this grant covers everything `required` asks for.
    constexpr bool containsAll(ScopeSet required) const {
        return (bits_ & required.bits_) == required.bits_;
    }

    constexpr std::uint32_t bits() const {
        return bits_;
    }

    /**
     * @brief Rebuild from a stored bit pattern, discarding bits that name
     *        nothing.
     *
     * The mask is not defensive clutter: these are persisted, and a config file
     * written by a newer MAGDA — or edited by hand — must not be able to set a
     * bit this build has no name for and would then carry around as an
     * unexplainable grant.
     */
    static constexpr ScopeSet fromBits(std::uint32_t bits) {
        ScopeSet result;
        result.bits_ = bits & ((1u << SCOPE_COUNT) - 1u);
        return result;
    }

    friend constexpr bool operator==(ScopeSet, ScopeSet) = default;

  private:
    static constexpr std::uint32_t bit(Scope scope) {
        return 1u << static_cast<unsigned>(scope);
    }

    std::uint32_t bits_ = 0;
};

/// The wire and config spelling: `read`, `edit`, `transport`, `session`,
/// `hardware-midi`. Stable — it appears in `system.describe`, in the config
/// file, and in the audit log.
juce::String scopeName(Scope scope);

/// The reverse, for config and for anything a client sends. Nothing for a word
/// this build does not know, which callers must treat as "not granted" rather
/// than as a reason to fail.
std::optional<Scope> scopeFromName(const juce::String& name);

/// Every scope, in declaration order.
const std::vector<Scope>& allScopeValues();

ScopeSet allScopes();

/**
 * @brief What a client MAGDA has never seen before starts with.
 *
 * Read-only, which is #1860's stated default and the only one that makes the
 * settings UI meaningful: if a new client arrived able to edit, granting would
 * be something the user did after the damage rather than before it.
 */
ScopeSet defaultClientScopes();

std::vector<juce::String> scopeNames(ScopeSet scopes);

/// A JSON array of names, for `system.describe` and the config file.
juce::var scopesToJson(ScopeSet scopes);

/// The inverse. Unknown names are ignored rather than rejected, so a config
/// written by a newer build downgrades to the scopes this one understands.
ScopeSet scopesFromJson(const juce::var& value);

/// `read, edit` — for a log line or a settings row, never for the wire.
juce::String describeScopes(ScopeSet scopes);

/**
 * @brief The key a self-declared client name is stored and matched under.
 *
 * Trimmed, lowercased, capped, and reduced to a conservative character set, so
 * `Cursor`, `cursor `, and `cursor` are one client rather than three rows the
 * user has to grant separately. Anything that survives none of that — an empty
 * name, or one that was entirely punctuation — becomes `unknown`, which is a
 * real client entry like any other: it collects every caller that did not
 * identify itself, and stays read-only until the user says otherwise.
 */
juce::String normaliseClientName(const juce::String& name);

/// The bucket for callers that declared nothing.
inline constexpr const char* ANONYMOUS_CLIENT = "unknown";

/// The longest a normalised client name may be. Names are shown in a settings
/// table and written to a config file; neither wants an unbounded string a
/// client chose.
inline constexpr int MAX_CLIENT_NAME_LENGTH = 64;

}  // namespace remote
}  // namespace magda
