#include "launch/FollowActions.hpp"

#include <algorithm>
#include <bit>

#include "launch/SessionLauncher.hpp"

namespace magda::engine {

namespace {

/**
 * @brief Which of @p count slots a random action picks, at @p dueBeat.
 *
 * A hash rather than a generator with state, so two renders of one project
 * produce one file (#1896). splitmix64's finaliser: its avalanche is what makes
 * two adjacent beats pick unrelated slots.
 */
std::uint64_t mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::size_t randomIndex(const SlotKey& key, double dueBeat, std::size_t count) {
    auto seed = mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.trackId)));
    seed = mix(seed ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(key.sceneIndex)));

    // Unrounded, or a scene's worth of slots firing on one beat would share a
    // seed.
    seed = mix(seed ^ std::bit_cast<std::uint64_t>(dueBeat));

    return static_cast<std::size_t>(seed % count);
}

/// The slot @p follow names, or null for an action that starts nothing: a stop,
/// and a neighbour at the end of the track.
const LaunchHandleTable::Entry* followTarget(const LaunchHandleTable& table, const SlotKey& key,
                                             const SlotFollow& follow, double dueBeat) {
    if (follow.action == SlotAction::none || follow.action == SlotAction::stop)
        return nullptr;

    if (follow.action == SlotAction::again)
        return table.findEntry(key);

    // Every slot of the track that has a handle, in scene order, which is every
    // slot holding a clip: the table is built from the slots the snapshot named
    // (RuntimeStateStore::publishHandles).
    const auto [first, last] = table.rangeFor(key.trackId);
    const auto count = static_cast<std::size_t>(last - first);

    const auto* self = std::find_if(first, last, [&key](const auto& e) { return e.key == key; });
    if (self == last)
        return nullptr;

    const auto at = static_cast<std::size_t>(self - first);

    switch (follow.action) {
        case SlotAction::next:
            // No wrap at the ends, which is the incumbent's: the last slot of a
            // track stops rather than starting the first again.
            return at + 1 < count ? first + at + 1 : nullptr;

        case SlotAction::previous:
            return at > 0 ? first + at - 1 : nullptr;

        case SlotAction::random:
            return first + randomIndex(key, dueBeat, count);

        case SlotAction::none:
        case SlotAction::stop:
        case SlotAction::again:
            break;
    }

    return nullptr;
}

}  // namespace

std::optional<double> followDueBeat(const LaunchHandle& handle, const SlotFollow& follow) {
    const auto schedule = handle.scheduleBeat();
    if (!schedule)
        return {};

    const auto loop = handle.loopBeats();

    // The one run with no end. A slot that does not loop always has one, since
    // reaching the end of its material is where it stops even with nothing to
    // follow it.
    if (loop && follow.action == SlotAction::none)
        return {};

    const auto pass = loop.value_or(follow.lengthBeats);
    if (pass <= 0.0)
        return {};

    const auto passes = loop ? std::max(1, follow.loopCount) : 1;

    return *schedule + (pass * passes) + std::max(0.0, follow.delayBeats);
}

void applyFollowAction(const LaunchHandleTable& table, const SlotKey& key, const SlotFollow& follow,
                       double dueBeat) {
    const auto* target = followTarget(table, key, follow, dueBeat);

    if (target != nullptr && target->handle != nullptr)
        target->handle->play(dueBeat);
}

}  // namespace magda::engine
