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
 * request holds a weak reference to it, and to the table it lives in; a load is
 * accepted only if that table still holds that same handle for that same key.
 * Nothing in the model carries it, so no copy, re-key or serialization path can
 * get it wrong, and a device that was never registered has no handle to lend --
 * which rejects its load rather than accepting it.
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

/**
 * @brief The live set, held apart from the object that manages it.
 *
 * A load in flight outlives things. The project can be closed and the runtime
 * that owns the assignments destroyed while a plugin is still loading, and the
 * completion that arrives afterwards still has to answer -- so it cannot answer
 * by dereferencing the owner. It holds a weak reference to this instead, and a
 * table that is gone is a load nobody is waiting for.
 */
class AssignmentTable {
  public:
    /** The handle held for @p key, or null when nothing is. */
    std::shared_ptr<const AssignmentHandle> find(magda::engine::DeviceKey key) const;

  private:
    friend class PluginAssignments;
    std::unordered_map<magda::engine::DeviceKey, std::shared_ptr<const AssignmentHandle>,
                       magda::engine::DeviceKeyHash>
        byKey_;
};

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
 * Weak at both ends, and for the same reason. A load must not keep an
 * assignment alive, because an assignment outliving its device is exactly the
 * state in which a stale load would be accepted; and it must not keep the table
 * alive either, because the runtime that owns it may be gone by the time the
 * plugin arrives. Either reference expiring is the answer "no longer wanted",
 * and an unregistered device hands out an already-expired handle, so forgetting
 * to register fails closed.
 *
 * Self-contained on purpose: completion needs nothing but this to decide, so
 * nothing about the runtime has to be captured by an asynchronous callback.
 */
struct LoadRequest {
    magda::engine::DeviceKey key;
    std::weak_ptr<const AssignmentHandle> handle;
    std::weak_ptr<const AssignmentTable> table;

    /**
     * @brief Whether a load answering this is still wanted.
     *
     * True only when the table is still there and still holds, for this key,
     * the very handle the request was made against. Every part matters: the
     * weak handle can still be locked by a caller holding the ActiveAssignment
     * it was given, and a key can be live again under a different assignment.
     */
    bool isStillWanted() const;

    /**
     * @brief Whether the key is live again under a different assignment.
     *
     * Only for telling a person which of the two happened: a slot now asking
     * for a different plugin, against a device that went away. Never the basis
     * for accepting anything.
     */
    bool keyWasReassigned() const;

    /**
     * @brief Whether the runtime that owns this assignment is still there.
     *
     * A different question from isStillWanted(), and the one a caller asks
     * before *reporting* rather than before accepting. A device deleted while
     * its runtime lives is a load to refuse and say so about, because something
     * is waiting to hear it. A runtime that is gone is not: there is nothing to
     * publish onto and nobody to tell, and a completion callback made by that
     * runtime is exactly the thing that must not be called now.
     */
    bool runtimeIsAlive() const;
};

/**
 * @brief The live set of plugin assignments, and the arbiter of stale loads.
 *
 * Keyed on engine::DeviceKey rather than a bare DeviceId: DeviceId is allocated
 * per section, so the main FX, post-FX and mixer-analysis sections can each hold
 * the same integer, and a device in one section must not be able to accept a
 * load requested for a device in another.
 *
 * Move-only. The table is shared with the loads in flight against it, never
 * with a second manager.
 */
class PluginAssignments {
  public:
    PluginAssignments();

    PluginAssignments(const PluginAssignments&) = delete;
    PluginAssignments& operator=(const PluginAssignments&) = delete;
    PluginAssignments(PluginAssignments&&) noexcept = default;
    PluginAssignments& operator=(PluginAssignments&&) noexcept = default;
    ~PluginAssignments() = default;

    /**
     * @brief @p key is live and keeps whatever assignment it already has.
     *
     * What ordinary registration is: a plan prepared again, a device re-visited
     * as the project is walked, a slot confirmed to still be there. None of
     * those is a new assignment, and minting one would expire a load that is
     * still wanted -- repeatedly, for anything that re-registers on a timer or
     * on every recompile.
     */
    ActiveAssignment ensureAssignment(magda::engine::DeviceKey key);

    /**
     * @brief @p key is live as a *new* assignment, whatever it held before.
     *
     * What a slot asking for a different plugin is, and what a placement is: a
     * duplicate, a paste, a preset import, an undo reinsertion, or an id handed
     * out again after the project was cleared. Any handle already held for @p
     * key is dropped, which expires every request outstanding against it.
     */
    ActiveAssignment replaceAssignment(magda::engine::DeviceKey key);

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

    /** How many assignments are live. For tests and diagnostics. */
    std::size_t size() const;

  private:
    std::shared_ptr<AssignmentTable> table_;
};

}  // namespace magda::daw::audio::engine_adapter
