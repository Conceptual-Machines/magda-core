// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <vector>

namespace magda {

/**
 * @brief The command targets extensions add, next in the chain after the main component (#3076).
 *
 * The main component does not list their commands, so the command manager walks on to this,
 * which hands each command to the target that lists it.
 */
class ContributedCommands final : public juce::ApplicationCommandTarget {
  public:
    void add(juce::ApplicationCommandTarget* target) {
        if (target != nullptr && std::ranges::find(targets_, target) == targets_.end())
            targets_.push_back(target);
    }

    void remove(juce::ApplicationCommandTarget* target) {
        std::erase(targets_, target);
    }

    juce::ApplicationCommandTarget* getNextCommandTarget() override {
        return nullptr;
    }

    void getAllCommands(juce::Array<juce::CommandID>& commands) override {
        for (auto* target : targets_)
            target->getAllCommands(commands);
    }

    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override {
        if (auto* target = ownerOf(commandID))
            target->getCommandInfo(commandID, result);
    }

    bool perform(const InvocationInfo& info) override {
        auto* target = ownerOf(info.commandID);
        return target != nullptr && target->perform(info);
    }

  private:
    juce::ApplicationCommandTarget* ownerOf(juce::CommandID commandID) const {
        for (auto* target : targets_) {
            juce::Array<juce::CommandID> commands;
            target->getAllCommands(commands);
            if (commands.contains(commandID))
                return target;
        }
        return nullptr;
    }

    std::vector<juce::ApplicationCommandTarget*> targets_;
};

}  // namespace magda
