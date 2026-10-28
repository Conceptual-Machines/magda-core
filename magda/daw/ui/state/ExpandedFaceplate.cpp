// SPDX-FileCopyrightText: Conceptual Machines
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ExpandedFaceplate.hpp"

namespace magda {

ExpandedFaceplate& ExpandedFaceplate::getInstance() {
    static ExpandedFaceplate instance;
    return instance;
}

void ExpandedFaceplate::expand(const ChainNodePath& device, juce::Component& faceplate,
                               const juce::String& title, std::function<void()> giveBack) {
    if (faceplate_ != nullptr && !holds(faceplate)) {
        auto previous = std::exchange(giveBack_, {});
        faceplate_ = nullptr;
        if (previous)
            previous();
    }
    device_ = device;
    faceplate_ = &faceplate;
    title_ = title;
    giveBack_ = std::move(giveBack);
    pending_ = false;
    ++generation_;
    sendSynchronousChangeMessage();
}

void ExpandedFaceplate::collapse() {
    auto giveBack = std::exchange(giveBack_, {});
    clear();
    if (giveBack)
        giveBack();
    sendSynchronousChangeMessage();
}

void ExpandedFaceplate::release(const juce::Component& faceplate) {
    if (!holds(faceplate))
        return;
    faceplate_ = nullptr;
    giveBack_ = {};
    pending_ = true;
    const int generation = ++generation_;
    sendSynchronousChangeMessage();

    // A chain rebuild makes the new slot in the same pass; nothing claiming it means it is gone.
    juce::MessageManager::callAsync([generation] {
        auto& self = getInstance();
        if (self.generation_ != generation || !self.pending_)
            return;
        self.clear();
        self.sendSynchronousChangeMessage();
    });
}

void ExpandedFaceplate::clear() {
    device_ = {};
    faceplate_ = nullptr;
    title_.clear();
    giveBack_ = {};
    pending_ = false;
    ++generation_;
}

}  // namespace magda
