#include "SessionViewState.hpp"

#include <algorithm>

namespace magda {

SessionViewState& SessionViewState::getInstance() {
    static SessionViewState instance;
    return instance;
}

void SessionViewState::setControllerSceneWindow(int sceneOffset, int sceneCount) {
    sceneOffset_.store(std::max(0, sceneOffset), std::memory_order_relaxed);
    sceneCount_.store(std::max(0, sceneCount), std::memory_order_relaxed);
    revision_.fetch_add(1, std::memory_order_release);
}

void SessionViewState::clearControllerSceneWindow() {
    sceneOffset_.store(-1, std::memory_order_relaxed);
    sceneCount_.store(0, std::memory_order_relaxed);
    revision_.fetch_add(1, std::memory_order_release);
}

SessionSceneWindow SessionViewState::getControllerSceneWindow() const {
    SessionSceneWindow window;
    window.sceneOffset = sceneOffset_.load(std::memory_order_relaxed);
    window.sceneCount = sceneCount_.load(std::memory_order_relaxed);
    window.revision = revision_.load(std::memory_order_acquire);
    return window;
}

}  // namespace magda
