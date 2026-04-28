// lua_smoke — interactive smoke test for the embedded Lua runtime (#29).
//
// Usage:
//   lua_smoke                  → REPL on stdin (blank line / EOF to quit)
//   lua_smoke script.lua       → run script.lua and exit
//
// Verifies end-to-end: stdlib opened, sandbox applied, print() routed to
// juce::Logger, errors carry tracebacks. No DAW dependencies.

#include "magda/scripting/LuaRuntime.hpp"

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

    if (argc == 2) {
        juce::File scriptFile(juce::String::fromUTF8(argv[1]));
        bool ok = rt.evalFile(scriptFile);
        if (!ok)
            std::cerr << "! " << rt.lastError().toRawUTF8() << '\n';
        juce::Logger::setCurrentLogger(nullptr);
        return ok ? 0 : 1;
    }

    std::cout << "MAGDA Lua smoke REPL — Lua 5.4 with sandbox.\n"
                 "  print(os.execute('ls'))   -- sandboxed away\n"
                 "  print(math.floor(3.7))    -- stdlib stays\n"
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
