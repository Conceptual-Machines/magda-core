// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file GrooveLibrary.hpp
 * @brief The groove templates a project can quantize to (#2757).
 */

#include <juce_core/juce_core.h>

#include <functional>
#include <magda/sdk/sequencer/TimeBend.hpp>
#include <optional>
#include <vector>

namespace magda {

/** @brief A groove as MAGDA states it, before any engine's own representation. */
struct GrooveTemplateData {
    juce::String name;
    int notesPerBeat = 2;
    bool parameterized = true;
    std::vector<float> latenessProportions;
};

/**
 * @brief The named grooves, owned here rather than by whichever engine renders.
 *
 * A clip plays a groove as a time bend, its curve loaded from here (#3044). The list
 * persists through GrooveStore, in MAGDA's own grooves.xml (#2761).
 */
class GrooveLibrary {
  public:
    /// What persists a write, normally GrooveStore.
    using Writer = std::function<bool(const GrooveTemplateData&)>;

    /// The store's own list, which is what a write is read back through.
    using Reader = std::function<std::vector<GrooveTemplateData>()>;

    static GrooveLibrary& getInstance();

    GrooveLibrary(const GrooveLibrary&) = delete;
    GrooveLibrary& operator=(const GrooveLibrary&) = delete;

    /** @brief Take @p grooves as the library, replacing whatever was there. */
    void import(std::vector<GrooveTemplateData> grooves);

    /**
     * @brief Put @p reader and @p writer behind the library, and take the store's list.
     *
     * The store canonicalises what it is given (trims and truncates a name, deduplicates it
     * with a "(2)" suffix, forces the parameterized flag), so every write is read back
     * rather than assumed (#2757).
     */
    void setStore(Reader reader, Writer writer);

    /// The engine is going away; the list it imported stays.
    void forgetStore() {
        reader_ = nullptr;
        writer_ = nullptr;
    }

    juce::StringArray names() const;

    /** @brief Every groove. */
    const std::vector<GrooveTemplateData>& all() const {
        return grooves_;
    }

    const GrooveTemplateData* find(const juce::String& name) const;

    /**
     * @brief The time bend the groove named @p name plays, in a meter of @p beatsPerBar.
     *
     * @p strength is its AMOUNT for a parameterized groove; the others carry their amount in the
     * table. Nullopt when no groove has that name or it does not fit a bend.
     */
    std::optional<sdk::sequencer::TimeBend> bend(const juce::String& name, double beatsPerBar,
                                                 float strength = 1.0f) const;

    /**
     * @brief Add @p groove, or replace the one of that name. False when it names nothing
     * or carries no lateness to apply.
     */
    bool upsert(const GrooveTemplateData& groove);

  private:
    GrooveLibrary() = default;

    std::vector<GrooveTemplateData> grooves_;
    Reader reader_;
    Writer writer_;
};

}  // namespace magda
