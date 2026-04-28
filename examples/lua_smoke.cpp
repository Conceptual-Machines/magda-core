// lua_smoke — interactive smoke test for the embedded Lua runtime + bindings.
//
// Usage:
//   lua_smoke                  → REPL on stdin (blank line / EOF to quit)
//   lua_smoke script.lua       → run script.lua and exit
//
// The REPL pre-registers a MockMagdaApi so the `magda.*` bindings work end
// to end without a real DAW. Reads return seeded mock state, writes log
// what the binding called. Useful for #29 and #30 verification.

#include "magda/scripting/LuaRuntime.hpp"
#include "magda/scripting/MagdaApiLuaBindings.hpp"
#include "tests/MockMagdaApi.hpp"

#include <juce_core/juce_core.h>

#include <iostream>
#include <string>

namespace {

class StdoutLogger : public juce::Logger {
public:
    void logMessage(const juce::String& message) override {
        std::cout << message.toRawUTF8() << '\n';
    }
};

}  // namespace

int main(int argc, char** argv) {
    StdoutLogger logger;
    juce::Logger::setCurrentLogger(&logger);

    magda::scripting::LuaRuntime rt;

    // Pre-seed a mock with a couple of tracks + a session clip so reads
    // return something interesting at the prompt.
    magda::test::MockMagdaApi mock;
    {
        magda::TrackInfo a;
        a.id = 1;
        a.name = "Drums";
        a.type = magda::TrackType::Audio;
        a.volume = 0.8f;
        magda::TrackInfo b;
        b.id = 2;
        b.name = "Bass";
        b.type = magda::TrackType::Audio;
        b.volume = 0.6f;
        mock.tracks_.tracks.push_back(a);
        mock.tracks_.tracks.push_back(b);
        mock.tracks_.nextId = 3;
        mock.selection_.selectedTrack = 1;
        mock.session_.activeOnTrack[1] = 707;
        mock.project_.info.name = "smoke";
        mock.project_.info.tempo = 120.0;
    }
    magda::scripting::registerMagdaApi(rt.state(), mock);

    if (argc == 2) {
        juce::File scriptFile(juce::String::fromUTF8(argv[1]));
        bool ok = rt.evalFile(scriptFile);
        if (!ok)
            std::cerr << "! " << rt.lastError().toRawUTF8() << '\n';
        juce::Logger::setCurrentLogger(nullptr);
        return ok ? 0 : 1;
    }

    std::cout
        << "MAGDA Lua smoke REPL — Lua 5.4 with sandbox + magda.* bindings.\n"
           "Mock DAW state pre-seeded: 2 tracks (Drums, Bass), 1 active session clip.\n"
           "  for _, t in ipairs(magda.tracks.list()) do print(t.id, t.name) end\n"
           "  magda.tracks.set_volume(1, 0.5)\n"
           "  print(magda.selection.track())\n"
           "  print(magda.project.info().tempo)\n"
           "  magda.session.launch_clip(101)\n"
           "  Blank line or EOF to quit.\n";

    std::string line;
    for (;;) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line))
            break;
        if (line.empty())
            break;
        juce::String chunk = juce::String::fromUTF8(line.c_str());
        if (!rt.eval(chunk, "=stdin"))
            std::cerr << "! " << rt.lastError().toRawUTF8() << '\n';
    }

    juce::Logger::setCurrentLogger(nullptr);
    return 0;
}
