#pragma once

/**
 * @file GrooveLibrary.hpp
 * @brief The groove templates a project can quantize to (#2757).
 */

#include <juce_core/juce_core.h>

#include <functional>
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
 * Both engines read this: the fork compiles each into a tracktion::GrooveTemplate, and the
 * native engine compiles the same entries into an engine::GrooveTemplateSet at publish.
 * Neither is asked for the list, which is what let a groove exist under one engine and not
 * the other (#2757).
 *
 * The list persists in Tracktion's Settings.xml, through the fork's manager under that
 * engine and through GrooveStore under the native one, in the same format (#2761).
 */
class GrooveLibrary {
  public:
    /// What persists a write: the fork's manager, or GrooveStore.
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
     * The store canonicalises what it is given -- Tracktion trims and truncates a name,
     * deduplicates it with a "(2)" suffix, and forces the parameterized flag to its own
     * mode -- so every write is read back rather than assumed (#2757).
     */
    void setStore(Reader reader, Writer writer);

    /// The engine is going away; the list it imported stays.
    void forgetStore() {
        reader_ = nullptr;
        writer_ = nullptr;
    }

    /// Told when the list changes, so the native engine republishes what a clip compiled.
    void setOnChanged(std::function<void()> onChanged) {
        onChanged_ = std::move(onChanged);
    }
    void forgetOnChanged() {
        onChanged_ = nullptr;
    }

    juce::StringArray names() const;

    /** @brief Every groove, for an engine compiling them into its own representation. */
    const std::vector<GrooveTemplateData>& all() const {
        return grooves_;
    }

    const GrooveTemplateData* find(const juce::String& name) const;

    /**
     * @brief Add @p groove, or replace the one of that name. False when it names nothing
     * or carries no lateness to apply.
     */
    bool upsert(const GrooveTemplateData& groove);

  private:
    GrooveLibrary() = default;

    void notifyChanged() const;

    std::vector<GrooveTemplateData> grooves_;
    Reader reader_;
    Writer writer_;
    std::function<void()> onChanged_;
};

}  // namespace magda
