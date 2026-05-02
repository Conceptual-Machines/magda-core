#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <string>

extern "C" {
struct lua_State;
}

namespace magda::scripting {

/**
 * Owns a Lua 5.4 VM. RAII: constructor opens stdlib + applies the sandbox +
 * redirects print() to juce::Logger; destructor closes the state.
 *
 * Threading: not thread-safe. A LuaRuntime instance must be used from a single
 * thread (typically the JUCE message thread). #592 will rely on this when
 * draining MIDI events on the message thread before calling into Lua.
 */
class LuaRuntime {
  public:
    LuaRuntime();
    ~LuaRuntime();

    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;
    LuaRuntime(LuaRuntime&&) noexcept;
    LuaRuntime& operator=(LuaRuntime&&) noexcept;

    /** Compile and execute `chunk`. Returns true on success.
     *  On failure, lastError() returns the message (with line numbers).
     *  `chunkName` is the source label used in error messages — pass a path
     *  like "@/path/to/script.lua" when evaluating from a file. */
    bool eval(const juce::String& chunk, const juce::String& chunkName = "=chunk");

    /** Read `file` and evaluate its contents. */
    bool evalFile(const juce::File& file);

    /** Evaluate `chunk` and return the first result coerced to integer.
     *  Returns std::nullopt on eval failure or if the result is not an integer. */
    std::optional<long long> evalToInt(const juce::String& chunk);

    /** Evaluate `chunk` and return the first result coerced to string.
     *  Returns std::nullopt on eval failure or if the result is not a string. */
    std::optional<juce::String> evalToString(const juce::String& chunk);

    /** Last error message captured by eval/evalFile, or empty if the last call
     *  succeeded (or no call has been made). */
    const juce::String& lastError() const noexcept {
        return lastError_;
    }

    /** Raw VM access — for binding code in #30 and beyond.
     *  Returns nullptr only if this instance has been moved-from. */
    lua_State* state() noexcept {
        return L_;
    }

  private:
    lua_State* L_ = nullptr;
    juce::String lastError_;
};

}  // namespace magda::scripting
