#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace magda {

// A user-saved drum grid kit: an ordered list of rows, each with a label and
// an optional role tag. Saved as JSON under presetsDir() / "Drumkits".
//
// The on-disk format is the standard MAGDA envelope ({magdaVersion, kind:
// "drumkit", payload: {rows: [...]}}) so it can be inspected and hand-edited
// without going through the app.
class DrumkitManager {
  public:
    struct Row {
        int noteNumber = 0;  // MIDI note this row maps to (DrumGrid baseNote + padIndex)
        juce::String label;
        juce::String role;  // canonical role id (see DrumGridRoles.hpp); empty = unset
    };

    struct Drumkit {
        juce::String name;
        juce::File file;
    };

    static DrumkitManager& getInstance();

    juce::File getDrumkitsDirectory() const;

    /** Save the given rows under `name`. Existing kits with the same name are
     *  overwritten. Returns false on filesystem failure. */
    bool saveDrumkit(const juce::String& name, const std::vector<Row>& rows);

    /** Load the rows for the drumkit with the given name. Returns empty
     *  vector if the kit does not exist or cannot be parsed. */
    std::vector<Row> loadDrumkit(const juce::String& name) const;

    /** List user drumkits sorted alphabetically by name. */
    std::vector<Drumkit> listDrumkits() const;

    /** Remove a user drumkit. Returns false if the file did not exist. */
    bool deleteDrumkit(const juce::String& name);

  private:
    DrumkitManager();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumkitManager)
};

}  // namespace magda
