#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>

#include "magda/agents/llama_model_manager.hpp"

namespace magda {

struct LlamaModelManagerTestAccess {
    static std::unique_lock<std::mutex> lockModelMutex(LlamaModelManager& manager) {
        return std::unique_lock<std::mutex>(manager.mutex_);
    }
};

}  // namespace magda

TEST_CASE("Llama model status reads do not wait for the model mutex", "[llama][issue-1762]") {
    using namespace std::chrono_literals;

    auto& manager = magda::LlamaModelManager::getInstance();
    auto modelLock = magda::LlamaModelManagerTestAccess::lockModelMutex(manager);

    std::promise<void> readerStartedPromise;
    auto readerStarted = readerStartedPromise.get_future();
    std::promise<void> readerFinishedPromise;
    auto readerFinished = readerFinishedPromise.get_future();

    std::thread reader([&]() {
        readerStartedPromise.set_value();
        static_cast<void>(manager.isLoaded());
        static_cast<void>(manager.getLoadedModelPath());
        readerFinishedPromise.set_value();
    });

    readerStarted.wait();
    const bool completedWithoutModelLock =
        readerFinished.wait_for(500ms) == std::future_status::ready;

    modelLock.unlock();
    reader.join();

    CHECK(completedWithoutModelLock);
}
