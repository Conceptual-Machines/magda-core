#include "ViewModeController.hpp"

#include <algorithm>

namespace magda {

ViewModeController& ViewModeController::getInstance() {
    static ViewModeController instance;
    return instance;
}

ViewModeController::ViewModeController() = default;

void ViewModeController::dispatch(const ViewModeEvent& event) {
    std::visit(
        [this](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, SetViewModeEvent>) {
                if (e.mode != currentMode && canSwitchTo(e.mode)) {
                    if (isExclusive(e.mode))
                        modeBeforeExclusive_ = currentMode;
                    currentMode = e.mode;
                    notifyListeners();
                }
            }
            // RequestAudioProfileEvent is handled by getAudioProfile()
        },
        event);
}

void ViewModeController::setViewMode(ViewMode mode) {
    dispatch(SetViewModeEvent{mode});
}

void ViewModeController::setExclusive(ViewMode mode, bool exclusive) {
    exclusive_[static_cast<size_t>(mode)] = exclusive;
}

bool ViewModeController::isExclusive(ViewMode mode) const {
    return exclusive_[static_cast<size_t>(mode)];
}

bool ViewModeController::canSwitchTo(ViewMode mode) const {
    return mode == currentMode || !isExclusive(currentMode);
}

void ViewModeController::exitExclusiveMode() {
    if (!isExclusive(currentMode))
        return;
    currentMode = modeBeforeExclusive_;
    notifyListeners();
}

void ViewModeController::addListener(ViewModeListener* listener) {
    if (listener != nullptr) {
        listeners.push_back(listener);
    }
}

void ViewModeController::removeListener(ViewModeListener* listener) {
    listeners.erase(std::remove(listeners.begin(), listeners.end(), listener), listeners.end());
}

void ViewModeController::notifyListeners() {
    auto profile = getAudioProfile();
    for (auto* listener : listeners) {
        listener->viewModeChanged(currentMode, profile);
    }
}

}  // namespace magda
