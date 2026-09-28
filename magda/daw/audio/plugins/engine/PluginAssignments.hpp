#pragma once

#include <cstddef>
#include <memory>
#include <unordered_map>

#include "plan/RenderPlan.hpp"

/**
 * @file PluginAssignments.hpp
 * @brief Who a plugin is being loaded for, owned at runtime (#2261).
 *
 * An async plugin load takes seconds, and a stale answer must be refused
 * when the device that asked for it is no longer asking. Mutable plugin
 * metadata can't decide that: resolution deliberately tolerates a moved
 * file, a renamed plugin, a missing vendor, an unreliable imported role -- so
 * comparing names or paths can't tell a legitimate correction from a swap.
 *
 * Identity used to live in the model, as a counter on DeviceInfo minted by
 * whichever call site remembered to. That failed: DeviceInfo is a value, and
 * copying it can't distinguish a snapshot from an undo record from a
 * duplicate from a new live placement. A missed call site failed the wrong
 * way -- the copy kept the token, so a stale load was accepted and one
 * plugin's state got restored onto another.
 *
 * So identity is owned here, as a pointer rather than a number. A live
 * device gets a handle; a request holds a weak reference to it and to the
 * table it lives in; a load is accepted only if the table still holds that
 * same handle for that same key. Nothing in the model carries it, so no
 * copy, re-key or serialization path can get it wrong, and an unregistered
 * device has no handle to lend, so its load is rejected rather than accepted.
 *
 * Message thread only: registration happens as devices are placed, and JUCE
 * calls an async load back on the message thread too.
 */

namespace magda::daw::audio::engine_adapter {

/**
 * @brief The identity of one live plugin assignment.
 *
 * Deliberately empty: identified by its address alone, so there's no field a
 * copy could carry and no value a caller could forge. Held by shared_ptr so
 * a request can hold a weak reference that expires on its own when the
 * assignment ends.
 */
struct AssignmentHandle {};

/**
 * @brief The live set, held apart from the object that manages it.
 *
 * A load in flight can outlive its owner: the project can close and the
 * runtime destroy the assignments while a plugin is still loading, and the
 * completion that arrives afterward still has to answer without
 * dereferencing anything gone. It holds a weak reference to this instead --
 * a table that's gone is simply a load nobody is waiting for.
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
 * @brief What an asynchronous operation remembers about the device it is for.
 *
 * Both directions carry one: a load asks whether the slot that wanted it
 * still does when the plugin arrives, and a state capture asks the same
 * thing before writing a snapshot into a project (DeviceControl.hpp). Same
 * question either way -- is this still the assignment the operation started
 * against -- so it's one type with one check, not a convention every caller
 * reimplements.
 *
 * Weak at both ends, for the same reason: an operation must not keep an
 * assignment alive (that's exactly the state a stale answer would be
 * accepted in), and must not keep the table alive either, since the runtime
 * owning it may be gone by the time the answer arrives. Either reference
 * expiring means "no longer wanted", and an unregistered device hands out an
 * already-expired handle, so forgetting to register fails closed.
 *
 * Self-contained on purpose: completion needs nothing else to decide, so no
 * async callback has to capture anything about the runtime.
 */
struct AssignmentRequest {
    magda::engine::DeviceKey key;
    std::weak_ptr<const AssignmentHandle> handle;
    std::weak_ptr<const AssignmentTable> table;

    /**
     * @brief Whether a load answering this is still wanted.
     *
     * True only when the table is still there and still holds, for this
     * key, the exact handle the request was made against. Both parts
     * matter: the weak handle can still be locked by a caller holding the
     * ActiveAssignment it was given, and the key can be live again under a
     * different assignment.
     */
    bool isStillWanted() const;

    /**
     * @brief Whether the key is live again under a different assignment.
     *
     * For telling a person which of the two happened -- a slot now wanting
     * a different plugin, vs. a device that went away -- never as the basis
     * for accepting anything.
     */
    bool keyWasReassigned() const;

    /**
     * @brief Whether the runtime that owns this assignment is still there.
     *
     * A different question from isStillWanted(), asked before *reporting*
     * rather than before accepting: a device deleted while its runtime
     * lives is a load to refuse and report, since something is waiting to
     * hear it. A runtime that's gone is not -- there's nothing to publish
     * onto and nobody to tell, and its completion callback is exactly what
     * must not be called now.
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
    AssignmentRequest request(magda::engine::DeviceKey key) const;

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
