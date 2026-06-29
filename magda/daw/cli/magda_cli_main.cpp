#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

#include "api/clip_api.hpp"
#include "api/magda_api.hpp"
#include "api/project_api.hpp"
#include "api/track_api.hpp"
#include "engine/TracktionEngineWrapper.hpp"
#include "project/ProjectInfo.hpp"
#include "project/ProjectManager.hpp"
#include "version.hpp"

namespace {

void printUsage(std::ostream& out) {
    out << "magda-cli " << MAGDA_VERSION << "\n"
        << "\n"
        << "Usage:\n"
        << "  magda-cli boot\n"
        << "  magda-cli run <project.mgd> [--out <out.mgd>]\n"
        << "  magda-cli run <project.mgd> --cmds <cmds.txt> [--out <out.mgd>] [--dump-json]\n"
        << "  magda-cli exec <project.mgd> <commands...> [--out <out.mgd>] [--dump-json]\n"
        << "\n"
        << "Commands:\n"
        << "  set-tempo <bpm>\n"
        << "  add-track <audio|group|aux|chord> [name]\n"
        << "  delete-track <track-id>\n"
        << "  add-midi-clip <track-id> <start-beats> <length-beats>\n"
        << "  delete-clip <clip-id>\n"
        << "  dump --json\n";
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

std::optional<double> parseDouble(const juce::String& text) {
    double value = 0.0;
    if (std::istringstream{text.toStdString()} >> value)
        return value;
    return std::nullopt;
}

std::optional<int> parseInt(const juce::String& text) {
    int value = 0;
    if (std::istringstream{text.toStdString()} >> value)
        return value;
    return std::nullopt;
}

std::optional<magda::TrackType> parseTrackType(const juce::String& text) {
    auto normalized = text.trim().toLowerCase();
    if (normalized == "audio" || normalized == "midi")
        return magda::TrackType::Audio;
    if (normalized == "group")
        return magda::TrackType::Group;
    if (normalized == "aux")
        return magda::TrackType::Aux;
    if (normalized == "chord")
        return magda::TrackType::Chord;
    return std::nullopt;
}

juce::String trackTypeToString(magda::TrackType type) {
    return juce::String(magda::getTrackTypeName(type)).toLowerCase();
}

juce::var midiNoteToJson(const magda::MidiNote& note) {
    auto obj = new juce::DynamicObject();
    obj->setProperty("note", note.noteNumber);
    obj->setProperty("velocity", note.velocity);
    obj->setProperty("startBeat", note.startBeat);
    obj->setProperty("lengthBeats", note.lengthBeats);
    return obj;
}

juce::var clipToJson(const magda::ClipInfo& clip) {
    auto obj = new juce::DynamicObject();
    obj->setProperty("id", clip.id);
    obj->setProperty("trackId", clip.trackId);
    obj->setProperty("name", clip.name);
    obj->setProperty("type", clip.isAudio() ? "audio" : "midi");
    obj->setProperty("view", clip.view == magda::ClipView::Session ? "session" : "arrangement");
    obj->setProperty("startBeat", clip.placement.startBeat);
    obj->setProperty("lengthBeats", clip.placement.lengthBeats);

    juce::Array<juce::var> notes;
    for (const auto& note : clip.midiNotes)
        notes.add(midiNoteToJson(note));
    obj->setProperty("notes", notes);

    if (clip.isAudio())
        obj->setProperty("sourceFile", clip.audio().source.filePath);

    return obj;
}

juce::var trackToJson(magda::MagdaApi& api, const magda::TrackInfo& track) {
    auto obj = new juce::DynamicObject();
    obj->setProperty("id", track.id);
    obj->setProperty("type", trackTypeToString(track.type));
    obj->setProperty("name", track.name);
    obj->setProperty("volume", track.volume);
    obj->setProperty("pan", track.pan);
    obj->setProperty("muted", track.muted);
    obj->setProperty("soloed", track.soloed);
    obj->setProperty("recordArmed", track.recordArmed);

    juce::Array<juce::var> clips;
    for (auto clipId : api.clips().getClipsOnTrack(track.id)) {
        if (auto* clip = api.clips().getClip(clipId))
            clips.add(clipToJson(*clip));
    }
    obj->setProperty("clips", clips);
    return obj;
}

juce::String dumpProjectJson(magda::MagdaApi& api) {
    const auto& project = api.project().getCurrentProjectInfo();
    auto root = new juce::DynamicObject();
    root->setProperty("name", project.name);
    root->setProperty("filePath", project.filePath);
    root->setProperty("tempo", project.tempo);
    root->setProperty("timeSignatureNumerator", project.timeSignatureNumerator);
    root->setProperty("timeSignatureDenominator", project.timeSignatureDenominator);
    root->setProperty("sampleRate", project.sampleRate);
    root->setProperty("timelineLengthBars", project.timelineLengthBars);

    juce::Array<juce::var> tracks;
    for (const auto& track : api.tracks().getTracks())
        tracks.add(trackToJson(api, track));
    root->setProperty("tracks", tracks);

    return juce::JSON::toString(juce::var(root), true);
}

struct CommandResult {
    bool ok = true;
    juce::String error;
};

class CommandDispatcher {
  public:
    explicit CommandDispatcher(magda::TracktionEngineWrapper& engine) : engine_(engine) {}

    CommandResult execute(const juce::StringArray& tokens, size_t& index) {
        if (index >= static_cast<size_t>(tokens.size()))
            return {};

        const auto command = tokens[static_cast<int>(index++)];
        if (command == "set-tempo")
            return setTempo(tokens, index);
        if (command == "add-track")
            return addTrack(tokens, index);
        if (command == "delete-track")
            return deleteTrack(tokens, index);
        if (command == "add-midi-clip" || command == "add-clip")
            return addMidiClip(tokens, index);
        if (command == "delete-clip")
            return deleteClip(tokens, index);
        if (command == "dump")
            return dump(tokens, index);

        return fail("Unknown command: " + command);
    }

    void dumpJson() {
        std::cout << dumpProjectJson(engine_.getMagdaApi()) << "\n";
    }

  private:
    CommandResult setTempo(const juce::StringArray& tokens, size_t& index) {
        if (index >= static_cast<size_t>(tokens.size()))
            return fail("set-tempo requires <bpm>");
        auto bpm = parseDouble(tokens[static_cast<int>(index++)]);
        if (!bpm || *bpm <= 0.0)
            return fail("set-tempo requires a positive numeric bpm");

        engine_.setTempo(*bpm);
        magda::ProjectManager::getInstance().setTempo(*bpm);
        return {};
    }

    CommandResult addTrack(const juce::StringArray& tokens, size_t& index) {
        if (index >= static_cast<size_t>(tokens.size()))
            return fail("add-track requires <audio|group|aux|chord>");

        const auto typeToken = tokens[static_cast<int>(index++)];
        auto type = parseTrackType(typeToken);
        if (!type)
            return fail("Unsupported track type: " + typeToken);

        juce::String name = juce::String(magda::getTrackTypeName(*type));
        if (index < static_cast<size_t>(tokens.size()) &&
            !isCommand(tokens[static_cast<int>(index)]))
            name = tokens[static_cast<int>(index++)];

        auto id = engine_.getMagdaApi().tracks().createTrack(name, *type);
        std::cout << "track " << id << "\n";
        return {};
    }

    CommandResult deleteTrack(const juce::StringArray& tokens, size_t& index) {
        auto trackId = takeInt(tokens, index, "delete-track requires <track-id>");
        if (!trackId)
            return fail(lastParseError_);
        engine_.getMagdaApi().tracks().deleteTrack(*trackId);
        return {};
    }

    CommandResult addMidiClip(const juce::StringArray& tokens, size_t& index) {
        auto trackId = takeInt(tokens, index, "add-midi-clip requires <track-id>");
        if (!trackId)
            return fail(lastParseError_);
        auto start = takeDouble(tokens, index, "add-midi-clip requires <start-beats>");
        if (!start)
            return fail(lastParseError_);
        auto length = takeDouble(tokens, index, "add-midi-clip requires <length-beats>");
        if (!length)
            return fail(lastParseError_);
        if (*length <= 0.0)
            return fail("add-midi-clip requires a positive length");

        auto id = engine_.getMagdaApi().clips().createMidiClipBeats(*trackId, *start, *length);
        std::cout << "clip " << id << "\n";
        return {};
    }

    CommandResult deleteClip(const juce::StringArray& tokens, size_t& index) {
        auto clipId = takeInt(tokens, index, "delete-clip requires <clip-id>");
        if (!clipId)
            return fail(lastParseError_);
        engine_.getMagdaApi().clips().deleteClip(*clipId);
        return {};
    }

    CommandResult dump(const juce::StringArray& tokens, size_t& index) {
        if (index >= static_cast<size_t>(tokens.size()) ||
            tokens[static_cast<int>(index)] != "--json")
            return fail("dump requires --json");
        ++index;
        dumpJson();
        return {};
    }

    static bool isCommand(const juce::String& token) {
        static const std::set<juce::String> commands = {
            "set-tempo", "add-track",   "delete-track", "add-midi-clip",
            "add-clip",  "delete-clip", "dump"};
        return commands.count(token) > 0 || token.startsWith("--");
    }

    std::optional<int> takeInt(const juce::StringArray& tokens, size_t& index,
                               const juce::String& missingError) {
        if (index >= static_cast<size_t>(tokens.size())) {
            lastParseError_ = missingError;
            return std::nullopt;
        }
        auto value = parseInt(tokens[static_cast<int>(index++)]);
        if (!value)
            lastParseError_ = "Expected integer argument";
        return value;
    }

    std::optional<double> takeDouble(const juce::StringArray& tokens, size_t& index,
                                     const juce::String& missingError) {
        if (index >= static_cast<size_t>(tokens.size())) {
            lastParseError_ = missingError;
            return std::nullopt;
        }
        auto value = parseDouble(tokens[static_cast<int>(index++)]);
        if (!value)
            lastParseError_ = "Expected numeric argument";
        return value;
    }

    static CommandResult fail(const juce::String& error) {
        return {false, error};
    }

    magda::TracktionEngineWrapper& engine_;
    juce::String lastParseError_;
};

struct RunOptions {
    juce::File input;
    juce::File output;
    juce::File commandFile;
    bool hasCommandFile = false;
    bool dumpJson = false;
    juce::StringArray execTokens;
};

bool loadProjectForCli(const juce::File& input, HeadlessEngineSession& session) {
    auto& projectManager = magda::ProjectManager::getInstance();
    if (!projectManager.loadProject(input, [&session](const magda::ProjectInfo& info) {
            restoreProjectTiming(session.engine(), info);
        })) {
        std::cerr << "Failed to load project: " << projectManager.getLastError() << "\n";
        return false;
    }
    return true;
}

bool saveProjectForCli(const juce::File& output) {
    auto& projectManager = magda::ProjectManager::getInstance();
    if (!projectManager.saveProjectAs(output)) {
        std::cerr << "Failed to save project: " << projectManager.getLastError() << "\n";
        return false;
    }
    std::cout << "Saved " << projectManager.getCurrentProjectFile().getFullPathName() << "\n";
    return true;
}

bool executeCommandTokens(CommandDispatcher& dispatcher, const juce::StringArray& tokens) {
    size_t index = 0;
    while (index < static_cast<size_t>(tokens.size())) {
        auto result = dispatcher.execute(tokens, index);
        if (!result.ok) {
            std::cerr << result.error << "\n";
            return false;
        }
    }
    return true;
}

bool executeCommandFile(CommandDispatcher& dispatcher, const juce::File& file) {
    if (!file.existsAsFile()) {
        std::cerr << "Command file does not exist: " << file.getFullPathName() << "\n";
        return false;
    }

    juce::StringArray lines;
    lines.addLines(file.loadFileAsString());
    for (auto line : lines) {
        line = line.trim();
        if (line.isEmpty() || line.startsWith("#"))
            continue;

        juce::StringArray tokens;
        tokens.addTokens(line, true);
        if (!executeCommandTokens(dispatcher, tokens))
            return false;
    }
    return true;
}

int runCli(const RunOptions& options) {
    HeadlessEngineSession session;
    if (!session.initialize()) {
        std::cerr << session.error() << "\n";
        return 1;
    }

    if (!loadProjectForCli(options.input, session))
        return 1;

    CommandDispatcher dispatcher(session.engine());
    if (options.hasCommandFile && !executeCommandFile(dispatcher, options.commandFile))
        return 1;
    if (!options.execTokens.isEmpty() && !executeCommandTokens(dispatcher, options.execTokens))
        return 1;
    if (options.dumpJson)
        dispatcher.dumpJson();

    if (!saveProjectForCli(options.output))
        return 1;

    return 0;
}

int runRoundTrip(const juce::StringArray& args) {
    if (args.size() < 2) {
        printUsage(std::cerr);
        return 2;
    }

    RunOptions options;
    options.input = fileFromArg(args[1]);
    options.output = defaultOutputFor(options.input);

    for (int i = 2; i < args.size(); ++i) {
        if (args[i] == "--out") {
            if (++i >= args.size()) {
                printUsage(std::cerr);
                return 2;
            }
            options.output = fileFromArg(args[i]);
        } else if (args[i] == "--cmds") {
            if (++i >= args.size()) {
                printUsage(std::cerr);
                return 2;
            }
            options.commandFile = fileFromArg(args[i]);
            options.hasCommandFile = true;
        } else if (args[i] == "--dump-json") {
            options.dumpJson = true;
        } else {
            printUsage(std::cerr);
            return 2;
        }
    }

    return runCli(options);
}

int execCommands(const juce::StringArray& args) {
    if (args.size() < 3) {
        printUsage(std::cerr);
        return 2;
    }

    RunOptions options;
    options.input = fileFromArg(args[1]);
    options.output = defaultOutputFor(options.input);

    for (int i = 2; i < args.size(); ++i) {
        if (args[i] == "--out") {
            if (++i >= args.size()) {
                printUsage(std::cerr);
                return 2;
            }
            options.output = fileFromArg(args[i]);
        } else if (args[i] == "--dump-json") {
            options.dumpJson = true;
        } else {
            options.execTokens.add(args[i]);
        }
    }

    if (options.execTokens.isEmpty() && !options.dumpJson) {
        printUsage(std::cerr);
        return 2;
    }

    return runCli(options);
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
    if (command == "exec")
        return execCommands(args);

    std::cerr << "Unknown command: " << command << "\n";
    printUsage(std::cerr);
    return 2;
}
