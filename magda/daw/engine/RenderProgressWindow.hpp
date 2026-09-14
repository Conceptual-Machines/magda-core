#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "AudioEngine.hpp"

namespace magda {

/**
 * @brief Runs one offline render task modally, with progress and Cancel.
 *
 * The task runs on the window's thread while runThread() pumps the message loop.
 */
class RenderProgressWindow : public juce::ThreadWithProgressWindow {
  public:
    RenderProgressWindow(const juce::String& title, std::unique_ptr<OfflineRenderTask> task)
        : ThreadWithProgressWindow(title, true, true), task_(std::move(task)) {
        setStatusMessage("Preparing to render...");
    }

    void run() override {
        setStatusMessage("Rendering...");
        if (!task_)
            return;
        result_ = task_->run([this]() { return threadShouldExit(); },
                             [this](float progress) { setProgress(progress); });
        success_ = result_.success;
    }

    bool wasSuccessful() const {
        return success_;
    }

    const OfflineRenderResult& result() const {
        return result_;
    }

  private:
    std::unique_ptr<OfflineRenderTask> task_;
    OfflineRenderResult result_;
    bool success_ = false;
};

}  // namespace magda
