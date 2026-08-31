#pragma once

#include <cstddef>
#include <memory>
#include <unordered_map>

#include "plan/RenderPlan.hpp"

/**
 * @file PluginAssignments.hpp
 * @brief Who a plugin is being loaded for, owned at runtime (#2261).
 *
 * An asynchronous plugin load takes seconds, and the answer has to be refused
 * when it comes back for a device that is no longer asking. Mutable plugin
 * metadata cannot decide that: resolution deliberately tolerates a moved file,
 * a renamed plugin, a missing vendor and an unreliable imported role, so a
 * comparison of names or paths finds a legitimate correction indistinguishable
 * from a swap. The question needs an identity nothing about the plugin can
 * change.
 *
 * That identity used to live in the model, as a counter on DeviceInfo minted by
 * whichever call site remembered to. It could not hold: DeviceInfo is a value,
 * and C++ copying cannot tell a snapshot from an undo record from a browser
 * template from a duplicate from a paste from a new live placement. The
 * invariant survived only as a habit, missed sites were found one at a time,
 * and a missed one failed the wrong way -- the copy kept the token, so a stale
 * load was *accepted* and one plugin's state was restored onto another.
 *
 * So identity is owned here instead, by whoever runs the plugins, and it is a
 * pointer rather than a number. A device that becomes live gets a handle; the
 * request holds a weak reference to it; completion is accepted only if this
 * still holds that same handle for that same key. Nothing in the model carries
 * it, so no copy, re-key or serialization path can get it wrong, and a device
 * that was never registered has no handle to lend -- which rejects its load
 * rather than accepting it.
 *
 * Message thread only. Registration happens as devices are placed, and JUCE
 * calls an asynchronous load back on the message thread, so both ends of the
 * question are already there.
 */

namespace magda::daw::audio::engine_adapter {

/**
 * @brief The identity of one live plugin assignment.
 *
 * Deliberately empty: it is identified by its address and by nothing else, so
 * there is no field a copy could carry and no value a caller could forge. Held
 * by shared_ptr so a request can hold a weak reference that expires on its own
 * when the assignment ends.
 */
struct AssignmentHandle {};

/** A device that is live now, and the handle standing for its assignment. */
struct ActiveAssignment {
    magda::engine::DeviceKey key;
    std::shared_ptr<const AssignmentHandle> handle;

    /// Whether this names an assignment at all. False for the empty value a
    /// lookup returns when nothing is registered.
    explicit operator bool() const {
        return handle != nullptr;
    }
};

/**
 * @brief What an asynchronous load remembers about who it is loading for.
 *
 * Weak on purpose. A load in flight must not keep an assignment alive, because
 * an assignment outliving its device is exactly the state in which a stale load
 * would be accepted. An expired handle is the answer "no longer wanted", and it
 * is also what an unregistered device hands out, so forgetting to register
 * fails closed.
 */
struct LoadRequest {
    magda::engine::DeviceKey key;
    std::weak_ptr<const AssignmentHandle> handle;
};

/**
 * @brief The live set of plugin assignments, and the arbiter of stale loads.
 *
 * Keyed on engine::DeviceKey rather than a bare DeviceId: DeviceId is allocated
 * per section, so the main FX, post-FX and mixer-analysis sections can each hold
 * the same integer, and a device in one section must not be able to accept a
 * load requested for a device in another.
 */
class PluginAssignments {
  public:
    /**
     * @brief A device is live under @p key, as a new assignment.
     *
     * Any handle already held for @p key is dropped, which expires every
     * request outstanding against it. That is what covers a slot whose plugin
     * was replaced and an id handed out again after the project was cleared:
     * both are a new assignment on a key that had one, and neither can be told
     * from the other by anything in the model.
     */
    ActiveAssignment assign(magda::engine::DeviceKey key);

    /** The assignment held for @p key, or an empty one when there is none. */
    ActiveAssignment current(magda::engine::DeviceKey key) const;

    /**
     * @brief What to remember when starting a load for @p key.
     *
     * An unregistered key yields an already-expired request, so a load started
     * without one is refused at completion.
     */
    LoadRequest request(magda::engine::DeviceKey key) const;

    /** The device is gone, or its plugin is being replaced by nothing. */
    void release(magda::engine::DeviceKey key);

    /** Every assignment ends: the project was closed or cleared. */
    void releaseAll();

    /**
     * @brief Whether a load answering @p request is still wanted.
     *
     * True only when this still holds, for @p request's key, the very handle
     * the request was made against. Both halves matter: the weak reference can
     * still be locked by a caller holding the ActiveAssignment it was given,
     * and a key can be live again under a different assignment.
     */
    bool accepts(const LoadRequest& request) const;

    /** How many assignments are live. For tests and diagnostics. */
    std::size_t size() const {
        return live_.size();
    }

  private:
    std::unordered_map<magda::engine::DeviceKey, std::shared_ptr<const AssignmentHandle>,
                       magda::engine::DeviceKeyHash>
        live_;
};

}  // namespace magda::daw::audio::engine_adapter
