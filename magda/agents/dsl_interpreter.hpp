#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <string>
#include <vector>

#include "../daw/core/TrackTypes.hpp"

namespace magda::dsl {

// ============================================================================
// Token Types
// ============================================================================
enum class TokenType {
    IDENTIFIER,     // track, filter, new_clip, etc.
    STRING,         // "Serum", "Bass"
    NUMBER,         // 3, 4.5, -6.0
    LPAREN,         // (
    RPAREN,         // )
    LBRACKET,       // [
    RBRACKET,       // ]
    DOT,            // .
    COMMA,          // ,
    EQUALS,         // =
    EQUALS_EQUALS,  // ==
    SEMICOLON,      // ;
    AT,             // @
    END_OF_INPUT,
    ERROR
};

struct Token {
    TokenType type;
    std::string value;
    int line;
    int col;

    Token() : type(TokenType::END_OF_INPUT), line(0), col(0) {}
    Token(TokenType t, const std::string& v, int l = 0, int c = 0)
        : type(t), value(v), line(l), col(c) {}

    bool is(TokenType t) const {
        return type == t;
    }
    bool is(const char* id) const {
        return type == TokenType::IDENTIFIER && value == id;
    }
};

// ============================================================================
// Tokenizer
// ============================================================================
class Tokenizer {
  public:
    explicit Tokenizer(const char* input);

    Token next();
    Token peek();
    bool hasMore() const;
    bool expect(TokenType type);
    bool expect(const char* identifier);

  private:
    void skipWhitespace();
    void skipComment();
    Token readIdentifier();
    Token readString();
    Token readNumber();

    const char* input_;
    const char* pos_;
    int line_;
    int col_;
    Token peeked_;
    bool hasPeeked_;
};

// ============================================================================
// Parameter Map
// ============================================================================
class Params {
  public:
    void set(const std::string& key, const std::string& value);
    bool has(const std::string& key) const;
    std::string get(const std::string& key, const std::string& def = "") const;
    int getInt(const std::string& key, int def = 0) const;
    double getFloat(const std::string& key, double def = 0.0) const;
    bool getBool(const std::string& key, bool def = false) const;
    void clear() {
        params_.clear();
    }

  private:
    std::map<std::string, std::string> params_;
};

// ============================================================================
// Interpreter Context
// ============================================================================
struct InterpreterContext {
    int currentTrackId = -1;

    // For filter operations
    std::vector<int> filteredTrackIds;
    bool inFilterContext = false;

    // Error handling
    juce::String error;
    bool hasError = false;

    // Results log (human-readable)
    juce::StringArray results;

    void setError(const juce::String& msg) {
        error = msg;
        hasError = true;
    }

    void addResult(const juce::String& msg) {
        results.add(msg);
    }
};

// ============================================================================
// DSL Interpreter
// ============================================================================
class Interpreter {
  public:
    Interpreter();

    /**
     * @brief Execute DSL code against TrackManager/ClipManager
     * @return true on success, false on error (check getError())
     */
    bool execute(const char* dslCode);

    const char* getError() const {
        return ctx_.error.toRawUTF8();
    }

    /**
     * @brief Get human-readable results of the last execution
     */
    juce::String getResults() const {
        return ctx_.results.joinIntoString("\n");
    }

    /**
     * @brief Build a JSON snapshot of current project state for LLM context
     */
    static juce::String buildStateSnapshot();

  private:
    // Statement parsing
    bool parseStatement(Tokenizer& tok);
    bool parseTrackStatement(Tokenizer& tok);
    bool parseFilterStatement(Tokenizer& tok);

    // Chain method parsing
    bool parseMethodChain(Tokenizer& tok);
    bool executeNewClip(const Params& params);
    bool executeSetTrack(const Params& params);
    bool executeDelete();
    bool executeDeleteClip(const Params& params);

    // Parameter parsing
    bool parseParams(Tokenizer& tok, Params& outParams);
    bool parseValue(Tokenizer& tok, std::string& outValue);

    // Helpers
    static TrackType parseTrackType(const Params& params);
    int findTrackByName(const juce::String& name) const;

    // Time conversion
    double barsToTime(int bar) const;

    InterpreterContext ctx_;
};

}  // namespace magda::dsl
