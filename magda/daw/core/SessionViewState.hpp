#pragma once

#include <atomic>
#include <cstdint>

namespace magda {

struct SessionSceneWindow {
    int sceneOffset = -1;
    int sceneCount = 0;
    std::uint64_t revision = 0;
};

class SessionViewState {
  public:
    static SessionViewState& getInstance();

    void setControllerSceneWindow(int sceneOffset, int sceneCount);
    void clearControllerSceneWindow();
    SessionSceneWindow getControllerSceneWindow() const;

  private:
    std::atomic<int> sceneOffset_{-1};
    std::atomic<int> sceneCount_{0};
    std::atomic<std::uint64_t> revision_{0};
};

}  // namespace magda
