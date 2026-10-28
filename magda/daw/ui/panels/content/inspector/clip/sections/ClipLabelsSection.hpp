// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <unordered_set>
#include <vector>

#include "core/ClipManager.hpp"
#include "ui/components/common/CollapsibleSectionHeader.hpp"

namespace magda::daw::ui {

/**
 * @brief The label regions of one audio clip's source (#2316): rename, recolour, delete.
 *
 * Labels are drawn in the waveform editor's label lane; this lists them. Hidden unless
 * exactly one audio clip is selected.
 */
class ClipLabelsSection : public juce::Component {
  public:
    ClipLabelsSection();
    ~ClipLabelsSection() override;

    void setSelectedClips(const std::unordered_set<magda::ClipId>& clipIds);

    /** Height for the current clip, 0 when there is nothing to show. */
    int getPreferredHeight() const;

    /** True while a name is being typed, so a refresh must not rebuild the rows. */
    bool isEditing() const;

    void resized() override;

    std::function<void()> onCollapsedChanged;

  private:
    class Row;

    magda::ClipId clipId_ = magda::INVALID_CLIP_ID;
    CollapsibleSectionHeader header_{"Labels", "clipLabels"};
    juce::Label emptyHint_;
    std::vector<std::unique_ptr<Row>> rows_;
    std::vector<magda::LabelRegion> shown_;

    const std::vector<magda::LabelRegion>* labels() const;
    void update();
    void edit(int index, const std::function<void(magda::LabelRegion&)>& change,
              const juce::String& description);
    void remove(int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipLabelsSection)
};

}  // namespace magda::daw::ui
