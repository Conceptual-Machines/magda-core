#include "ModeContent.hpp"

#include "core/ViewModeController.hpp"

namespace magda {

ModeContentRegistry& ModeContentRegistry::getInstance() {
    static ModeContentRegistry instance;
    return instance;
}

void ModeContentRegistry::add(ViewMode mode, ModeContent content) {
    ViewModeController::getInstance().setExclusive(mode, content.exclusive);
    contents_[mode] = std::move(content);
    sendChangeMessage();
}

const ModeContent* ModeContentRegistry::find(ViewMode mode) const {
    const auto it = contents_.find(mode);
    return it != contents_.end() && it->second.create ? &it->second : nullptr;
}

}  // namespace magda
