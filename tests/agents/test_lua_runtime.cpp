#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include "magda/scripting/LuaRuntime.hpp"

using magda::scripting::LuaRuntime;

namespace {

juce::File testTempRoot() {
    auto envTmp = juce::SystemStats::getEnvironmentVariable("TMPDIR", {});
    auto root = envTmp.isNotEmpty() ? juce::File(envTmp)
                                    : juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto luaRoot = root.getChildFile("magda_tests");
    luaRoot.createDirectory();
    return luaRoot;
}

// Captures every line passed to juce::Logger::writeToLog while installed.
// Restores the previous logger on destruction.
class CapturingLogger : public juce::Logger {
  public:
    CapturingLogger() : previous_(juce::Logger::getCurrentLogger()) {
        juce::Logger::setCurrentLogger(this);
    }
    ~CapturingLogger() override {
        juce::Logger::setCurrentLogger(previous_);
    }

    void logMessage(const juce::String& message) override {
        lines_.add(message);
    }

    const juce::StringArray& lines() const noexcept {
        return lines_;
    }

  private:
    juce::Logger* previous_;
    juce::StringArray lines_;
};

}  // namespace

TEST_CASE("LuaRuntime evaluates an integer expression", "[lua_runtime]") {
    LuaRuntime rt;
    auto result = rt.evalToInt("2 + 2");
    REQUIRE(result.has_value());
    REQUIRE(*result == 4);
    REQUIRE(rt.lastError().isEmpty());
}

TEST_CASE("LuaRuntime evaluates a string expression", "[lua_runtime]") {
    LuaRuntime rt;
    auto result = rt.evalToString("'hello ' .. 'world'");
    REQUIRE(result.has_value());
    REQUIRE(*result == "hello world");
}

TEST_CASE("LuaRuntime reports syntax errors with line numbers", "[lua_runtime]") {
    LuaRuntime rt;
    REQUIRE_FALSE(rt.eval("if then"));
    // Lua's load error format is `[chunkname]:LINE: message`.
    REQUIRE(rt.lastError().contains(":1:"));
}

TEST_CASE("LuaRuntime reports runtime errors with the original message", "[lua_runtime]") {
    LuaRuntime rt;
    REQUIRE_FALSE(rt.eval("error('oops')"));
    REQUIRE(rt.lastError().contains("oops"));
    // Traceback is appended by the message handler.
    REQUIRE(rt.lastError().contains("stack traceback"));
}

TEST_CASE("LuaRuntime print() forwards to juce::Logger", "[lua_runtime]") {
    CapturingLogger capture;
    LuaRuntime rt;
    REQUIRE(rt.eval("print('hi from lua')"));
    REQUIRE(capture.lines().size() >= 1);
    REQUIRE(capture.lines().joinIntoString("\n").contains("hi from lua"));
}

TEST_CASE("LuaRuntime print() joins arguments with tabs", "[lua_runtime]") {
    CapturingLogger capture;
    LuaRuntime rt;
    REQUIRE(rt.eval("print('a', 1, true)"));
    REQUIRE(capture.lines().size() >= 1);
    auto joined = capture.lines().joinIntoString("\n");
    REQUIRE(joined.contains("a\t1\ttrue"));
}

TEST_CASE("LuaRuntime sandbox: calling os.execute errors with a descriptive message",
          "[lua_runtime][sandbox]") {
    LuaRuntime rt;
    REQUIRE_FALSE(rt.eval("os.execute('ls')"));
    REQUIRE(rt.lastError().contains("os.execute"));
    REQUIRE(rt.lastError().contains("sandbox"));
}

TEST_CASE("LuaRuntime sandbox: blocked os.* fields error with their name",
          "[lua_runtime][sandbox]") {
    for (const char* field : {"execute", "remove", "rename", "exit", "tmpname", "getenv"}) {
        LuaRuntime rt;
        auto chunk = juce::String("os.") + field + "('x')";
        REQUIRE_FALSE(rt.eval(chunk));
        REQUIRE(rt.lastError().contains(juce::String("os.") + field));
        REQUIRE(rt.lastError().contains("sandbox"));
    }
}

TEST_CASE("LuaRuntime sandbox: read-only os timing helpers stay available",
          "[lua_runtime][sandbox]") {
    LuaRuntime rt;
    REQUIRE(rt.evalToString("type(os.time)") == std::optional<juce::String>{"function"});
    REQUIRE(rt.evalToString("type(os.date)") == std::optional<juce::String>{"function"});
    REQUIRE(rt.evalToString("type(os.clock)") == std::optional<juce::String>{"function"});
    REQUIRE(rt.evalToString("type(os.difftime)") == std::optional<juce::String>{"function"});
}

TEST_CASE("LuaRuntime sandbox: io.open errors with a sandbox message", "[lua_runtime][sandbox]") {
    LuaRuntime rt;
    REQUIRE_FALSE(rt.eval("io.open('/tmp/x', 'w')"));
    REQUIRE(rt.lastError().contains("'io'"));
    REQUIRE(rt.lastError().contains("sandbox"));
}

TEST_CASE("LuaRuntime sandbox: blocked tables error on read, write, and call",
          "[lua_runtime][sandbox]") {
    for (const char* tbl : {"io", "package", "debug"}) {
        LuaRuntime rt;
        // Read access
        auto readChunk = juce::String("local _ = ") + tbl + ".x";
        REQUIRE_FALSE(rt.eval(readChunk));
        REQUIRE(rt.lastError().contains(juce::String("'") + tbl + "'"));

        // Write access
        auto writeChunk = juce::String(tbl) + ".x = 1";
        REQUIRE_FALSE(rt.eval(writeChunk));
        REQUIRE(rt.lastError().contains(juce::String("'") + tbl + "'"));

        // setmetatable() can't strip our locked metatable
        auto mtChunk = juce::String("setmetatable(") + tbl + ", {})";
        REQUIRE_FALSE(rt.eval(mtChunk));
    }
}

TEST_CASE("LuaRuntime sandbox: chunk loaders error with their name", "[lua_runtime][sandbox]") {
    for (const char* name : {"dofile", "loadfile", "load", "loadstring", "require"}) {
        LuaRuntime rt;
        auto chunk = juce::String(name) + "('x')";
        REQUIRE_FALSE(rt.eval(chunk));
        REQUIRE(rt.lastError().contains(juce::String("'") + name + "'"));
        REQUIRE(rt.lastError().contains("sandbox"));
    }
}

TEST_CASE("LuaRuntime sandbox: stdlib that should remain works", "[lua_runtime][sandbox]") {
    LuaRuntime rt;
    REQUIRE(rt.evalToInt("string.len('abc')") == std::optional<long long>{3});
    REQUIRE(rt.evalToInt("math.floor(3.7)") == std::optional<long long>{3});
    REQUIRE(rt.evalToInt("#({10, 20, 30})") == std::optional<long long>{3});
}

TEST_CASE("LuaRuntime instances are isolated", "[lua_runtime]") {
    LuaRuntime a;
    LuaRuntime b;
    REQUIRE(a.eval("magda_test_global = 42"));
    auto seen_in_b = b.evalToString("type(magda_test_global)");
    REQUIRE(seen_in_b.has_value());
    REQUIRE(*seen_in_b == "nil");
}

TEST_CASE("LuaRuntime evalFile loads a file and reports its filename on error", "[lua_runtime]") {
    // Lua truncates the chunk source name to LUA_IDSIZE (60) chars and keeps
    // the tail when the name starts with '@', so the bare filename always
    // survives even on long temp-directory paths.
    auto scriptFile = testTempRoot().getChildFile("magda_test_lua_runtime.lua");
    scriptFile.replaceWithText("error('intentional')\n");

    LuaRuntime rt;
    REQUIRE_FALSE(rt.evalFile(scriptFile));
    REQUIRE(rt.lastError().contains(scriptFile.getFileName()));
    REQUIRE(rt.lastError().contains("intentional"));

    scriptFile.deleteFile();
}

TEST_CASE("LuaRuntime survives a runtime error and remains usable", "[lua_runtime]") {
    LuaRuntime rt;
    REQUIRE_FALSE(rt.eval("error('first')"));
    REQUIRE(rt.lastError().contains("first"));

    auto ok = rt.evalToInt("1 + 1");
    REQUIRE(ok.has_value());
    REQUIRE(*ok == 2);
    REQUIRE(rt.lastError().isEmpty());
}
