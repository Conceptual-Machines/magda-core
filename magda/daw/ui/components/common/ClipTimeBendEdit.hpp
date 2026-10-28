// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>
#include <vector>

#include "core/ClipManager.hpp"
#include "core/ClipPropertyCommands.hpp"
#include "core/UndoManager.hpp"

namespace magda::daw::ui {

/**
 * @brief Edits clips' time bends live: a gesture plays and draws as it moves, and lands as one
 *        undo step (#3044).
 */
class ClipTimeBendEdit {
  public:
    /** @brief Shows @p bend on @p clips now, with no undo step. */
    void preview(const std::vector<magda::ClipId>& clips, const sdk::sequencer::TimeBend& bend) {
        auto& manager = magda::ClipManager::getInstance();
        for (const auto id : clips) {
            if (!before_.contains(id))
                if (const auto* clip = manager.getClip(id))
                    before_[id] = clip->timeBend;
            manager.setTimeBend(id, bend);
        }
    }

    /** @brief Sets @p bend on @p clips as one undo step, from where they were before a preview. */
    void commit(const std::vector<magda::ClipId>& clips, const sdk::sequencer::TimeBend& bend) {
        auto& manager = magda::ClipManager::getInstance();
        for (const auto& [id, original] : before_)
            manager.setTimeBend(id, original);
        before_.clear();

        magda::CompoundOperationScope scope("Set Clip Time Bend");
        for (const auto id : clips)
            magda::UndoManager::getInstance().executeCommand(
                std::make_unique<magda::SetClipTimeBendCommand>(id, bend));
    }

    /** @brief Whether a gesture is showing a preview, while the editor must not be reloaded. */
    bool previewing() const {
        return !before_.empty();
    }

  private:
    std::map<magda::ClipId, sdk::sequencer::TimeBend> before_;
};

}  // namespace magda::daw::ui
