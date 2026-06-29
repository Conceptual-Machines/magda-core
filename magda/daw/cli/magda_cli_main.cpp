#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>
#include <memory>

#include "engine/TracktionEngineWrapper.hpp"
#include "project/ProjectManager.hpp"
#include "version.hpp"

namespace {

void printUsage(std::ostream& out) {
    out << "magda-cli " << MAGDA_VERSION << "\n"
        << "\n"
        << "Usage:\n"
        << "  magda-cli boot\n"
        << "  magda-cli run <project.mgd> [--out <out.mgd>]\n"
        << "\n"
        << "P0 supports headless boot plus load -> save round-trip.\n";
}

juce::File fileFromArg(const juce::String& path) {
    if (juce::File::isAbsolutePath(path))
        return juce::File(path);
    return juce::File::getCurrentWorkingDirectory().getChildFile(path);
}

juce::File defaultOutputFor(const juce::File& input) {
    auto parent = input.getParentDirectory();
    auto stem = input.getFileNameWithoutExtension() + "_roundtrip";
    return parent.getChildFile(stem + ".mgd");
}

class HeadlessEngineSession {
  public:
    bool initialize() {
        engine_ = std::make_unique<magda::TracktionEngineWrapper>();
        engine_->setForceHeadless(true);
        if (!engine_->initialize()) {
            error_ = "Failed to initialize MAGDA engine";
            engine_.reset();
            return false;
        }
        return true;
    }

    magda::TracktionEngineWrapper& engine() {
        return *engine_;
    }

    const juce::String& error() const {
        return error_;
    }

  private:
    std::unique_ptr<magda::TracktionEngineWrapper> engine_;
    juce::String error_;
};

bool restoreProjectTiming(magda::TracktionEngineWrapper& engine, const magda::ProjectInfo& info) {
    engine.setTempo(info.tempo);
    engine.setTimeSignature(info.timeSignatureNumerator, info.timeSignatureDenominator);
    return true;
}

int runRoundTrip(const juce::StringArray& args) {
    if (args.size() != 2 && args.size() != 4) {
        printUsage(std::cerr);
        return 2;
    }

    const auto input = fileFromArg(args[1]);
    auto output = defaultOutputFor(input);

    if (args.size() == 4) {
        if (args[2] != "--out") {
            printUsage(std::cerr);
            return 2;
        }
        output = fileFromArg(args[3]);
    }

    HeadlessEngineSession session;
    if (!session.initialize()) {
        std::cerr << session.error() << "\n";
        return 1;
    }

    auto& projectManager = magda::ProjectManager::getInstance();
    if (!projectManager.loadProject(input, [&session](const magda::ProjectInfo& info) {
            restoreProjectTiming(session.engine(), info);
        })) {
        std::cerr << "Failed to load project: " << projectManager.getLastError() << "\n";
        return 1;
    }

    if (!projectManager.saveProjectAs(output)) {
        std::cerr << "Failed to save project: " << projectManager.getLastError() << "\n";
        return 1;
    }

    std::cout << "Saved " << projectManager.getCurrentProjectFile().getFullPathName() << "\n";
    return 0;
}

int bootOnly() {
    HeadlessEngineSession session;
    if (!session.initialize()) {
        std::cerr << session.error() << "\n";
        return 1;
    }

    std::cout << "MAGDA engine booted headless\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::StringArray args;
    for (int i = 0; i < argc; ++i)
        args.add(juce::String(argv[i]));

    if (args.size() < 2 || args[1] == "--help" || args[1] == "-h") {
        printUsage(args.size() < 2 ? std::cerr : std::cout);
        return args.size() < 2 ? 2 : 0;
    }

    const auto command = args[1];
    args.remove(0);

    if (command == "boot")
        return bootOnly();
    if (command == "run")
        return runRoundTrip(args);

    std::cerr << "Unknown command: " << command << "\n";
    printUsage(std::cerr);
    return 2;
}
