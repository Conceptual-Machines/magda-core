#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>

#include "magda/agents/command_model_downloader.hpp"
#include "magda/daw/core/Config.hpp"

namespace {

class CommandModelLocationGuard {
  public:
    CommandModelLocationGuard()
        : original_(magda::Config::getInstance().getCommandModelModelsDir()) {}

    ~CommandModelLocationGuard() {
        magda::Config::getInstance().setCommandModelModelsDir(original_);
        directory_.deleteRecursively();
    }

    juce::File createDirectory() {
        directory_ = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile(
            ".magda-command-model-test", "");
        REQUIRE(directory_.createDirectory().wasOk());
        return directory_;
    }

  private:
    std::string original_;
    juce::File directory_;
};

}  // namespace

TEST_CASE("Command model download uses the configured destination",
          "[agents][command_model][downloader]") {
    if (std::getenv("MAGDA_COMMAND_MODEL_DIR") != nullptr)
        SKIP("MAGDA_COMMAND_MODEL_DIR intentionally overrides the saved destination");

    CommandModelLocationGuard guard;
    const auto destination = guard.createDirectory();
    magda::Config::getInstance().setCommandModelModelsDir(
        destination.getFullPathName().toStdString());

    REQUIRE(magda::CommandModelDownloader::modelsDir() == destination);

    const auto files = magda::CommandModelDownloader::modelFiles();
    REQUIRE(files.size() == 3);
    CHECK(files[0] == destination.getChildFile("command_model.onnx"));
    CHECK(files[1] == destination.getChildFile("tokenizer.json"));
    CHECK(files[2] == destination.getChildFile("maps.json"));
}
