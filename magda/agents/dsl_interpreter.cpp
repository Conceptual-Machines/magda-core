#include "dsl_interpreter.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <random>
#include <sstream>

#include "../daw/api/clip_api.hpp"
#include "../daw/api/groove_api.hpp"
#include "../daw/api/magda_api.hpp"
#include "../daw/api/plugin_api.hpp"
#include "../daw/api/project_api.hpp"
#include "../daw/api/selection_api.hpp"
#include "../daw/api/track_api.hpp"
#include "../daw/api/undo_api.hpp"
#include "../daw/audio/AudioThumbnailManager.hpp"
#include "../daw/core/ClipManager.hpp"
#include "../daw/core/ClipPropertyCommands.hpp"
#include "../daw/core/DeviceInfo.hpp"
#include "../daw/core/MidiNoteCommands.hpp"
#include "../daw/core/PluginAlias.hpp"
#include "../daw/core/SelectionManager.hpp"
#include "../daw/core/TrackManager.hpp"
#include "../daw/core/TrackPropertyCommands.hpp"
#include "../daw/core/UndoManager.hpp"
#include "../daw/project/ProjectManager.hpp"
#include "internal_plugins.hpp"
#include "music_helpers.hpp"

namespace magda::dsl {

namespace {

juce::String normaliseColourString(juce::String raw) {
    raw = raw.trim().unquoted();
    if (raw.startsWithChar('#'))
        raw = raw.substring(1);
    if (raw.startsWithIgnoreCase("0x"))
        raw = raw.substring(2);
    if (raw.length() == 6)
        raw = "ff" + raw;
    return raw;
}

juce::Colour parseDslColour(const std::string& value) {
    return juce::Colour::fromString(normaliseColourString(juce::String(value)));
}

}  // namespace

// ============================================================================
// Tokenizer Implementation
// ============================================================================

Tokenizer::Tokenizer(const char* input)
    : input_(input), pos_(input), line_(1), col_(1), hasPeeked_(false) {}

void Tokenizer::skipWhitespace() {
    while (*pos_) {
        if (*pos_ == ' ' || *pos_ == '\t' || *pos_ == '\r') {
            pos_++;
            col_++;
        } else if (*pos_ == '\n') {
            pos_++;
            line_++;
            col_ = 1;
        } else if (*pos_ == '/' && *(pos_ + 1) == '/') {
            skipComment();
        } else {
            break;
        }
    }
}

void Tokenizer::skipComment() {
    while (*pos_ && *pos_ != '\n') {
        pos_++;
    }
}

Token Tokenizer::readIdentifier() {
    int startCol = col_;
    const char* start = pos_;

    while (*pos_ && (std::isalnum(static_cast<unsigned char>(*pos_)) || *pos_ == '_')) {
        pos_++;
        col_++;
    }

    return Token(TokenType::IDENTIFIER, std::string(start, static_cast<size_t>(pos_ - start)),
                 line_, startCol);
}

Token Tokenizer::readString() {
    int startCol = col_;
    pos_++;  // Skip opening quote
    col_++;

    std::string value;
    while (*pos_ && *pos_ != '"') {
        if (*pos_ == '\\' && *(pos_ + 1)) {
            pos_++;
            col_++;
            switch (*pos_) {
                case 'n':
                    value += '\n';
                    break;
                case 't':
                    value += '\t';
                    break;
                case 'r':
                    value += '\r';
                    break;
                case '"':
                    value += '"';
                    break;
                case '\\':
                    value += '\\';
                    break;
                default:
                    value += *pos_;
                    break;
            }
        } else {
            value += *pos_;
        }
        pos_++;
        col_++;
    }

    if (*pos_ == '"') {
        pos_++;
        col_++;
    } else {
        return Token(TokenType::ERROR, "Unterminated string", line_, startCol);
    }

    return Token(TokenType::STRING, value, line_, startCol);
}

Token Tokenizer::readNumber() {
    int startCol = col_;
    const char* start = pos_;

    if (*pos_ == '-') {
        pos_++;
        col_++;
    }

    while (*pos_ && std::isdigit(static_cast<unsigned char>(*pos_))) {
        pos_++;
        col_++;
    }

    if (*pos_ == '.') {
        pos_++;
        col_++;
        while (*pos_ && std::isdigit(static_cast<unsigned char>(*pos_))) {
            pos_++;
            col_++;
        }
    }

    return Token(TokenType::NUMBER, std::string(start, static_cast<size_t>(pos_ - start)), line_,
                 startCol);
}

Token Tokenizer::next() {
    if (hasPeeked_) {
        hasPeeked_ = false;
        return peeked_;
    }

    skipWhitespace();

    if (!*pos_)
        return Token(TokenType::END_OF_INPUT, "", line_, col_);

    int startCol = col_;
    char c = *pos_;

    switch (c) {
        case '(':
            pos_++;
            col_++;
            return Token(TokenType::LPAREN, "(", line_, startCol);
        case ')':
            pos_++;
            col_++;
            return Token(TokenType::RPAREN, ")", line_, startCol);
        case '[':
            pos_++;
            col_++;
            return Token(TokenType::LBRACKET, "[", line_, startCol);
        case ']':
            pos_++;
            col_++;
            return Token(TokenType::RBRACKET, "]", line_, startCol);
        case '.':
            pos_++;
            col_++;
            return Token(TokenType::DOT, ".", line_, startCol);
        case ',':
            pos_++;
            col_++;
            return Token(TokenType::COMMA, ",", line_, startCol);
        case ';':
            pos_++;
            col_++;
            return Token(TokenType::SEMICOLON, ";", line_, startCol);
        case '@':
            pos_++;
            col_++;
            return Token(TokenType::AT, "@", line_, startCol);
        case '=':
            pos_++;
            col_++;
            if (*pos_ == '=') {
                pos_++;
                col_++;
                return Token(TokenType::EQUALS_EQUALS, "==", line_, startCol);
            }
            return Token(TokenType::EQUALS, "=", line_, startCol);
        case '!':
            pos_++;
            col_++;
            if (*pos_ == '=') {
                pos_++;
                col_++;
                return Token(TokenType::NOT_EQUALS, "!=", line_, startCol);
            }
            return Token(TokenType::ERROR, "!", line_, startCol);
        case '>':
            pos_++;
            col_++;
            if (*pos_ == '=') {
                pos_++;
                col_++;
                return Token(TokenType::GREATER_EQUALS, ">=", line_, startCol);
            }
            return Token(TokenType::GREATER, ">", line_, startCol);
        case '<':
            pos_++;
            col_++;
            if (*pos_ == '=') {
                pos_++;
                col_++;
                return Token(TokenType::LESS_EQUALS, "<=", line_, startCol);
            }
            return Token(TokenType::LESS, "<", line_, startCol);
        default:
            break;
    }

    if (c == '"')
        return readString();

    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '-' && std::isdigit(static_cast<unsigned char>(*(pos_ + 1)))))
        return readNumber();

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
        return readIdentifier();

    // Unknown character - skip it
    pos_++;
    col_++;
    return Token(TokenType::ERROR, std::string(1, c), line_, startCol);
}

Token Tokenizer::peek() {
    if (!hasPeeked_) {
        peeked_ = next();
        hasPeeked_ = true;
    }
    return peeked_;
}

bool Tokenizer::hasMore() const {
    if (hasPeeked_)
        return peeked_.type != TokenType::END_OF_INPUT;

    const char* p = pos_;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
        p++;
    return *p != '\0';
}

bool Tokenizer::expect(TokenType type) {
    Token t = next();
    return t.type == type;
}

bool Tokenizer::expect(const char* identifier) {
    Token t = next();
    return t.type == TokenType::IDENTIFIER && t.value == identifier;
}

Tokenizer::Position Tokenizer::savePosition() const {
    return {pos_, line_, col_, peeked_, hasPeeked_};
}

void Tokenizer::restorePosition(const Position& p) {
    pos_ = p.pos;
    line_ = p.line;
    col_ = p.col;
    peeked_ = p.peeked;
    hasPeeked_ = p.hasPeeked;
}

// ============================================================================
// Params Implementation
// ============================================================================

void Params::set(const std::string& key, const std::string& value) {
    params_[key] = value;
}

bool Params::has(const std::string& key) const {
    return params_.find(key) != params_.end();
}

std::string Params::get(const std::string& key, const std::string& def) const {
    auto it = params_.find(key);
    return (it != params_.end()) ? it->second : def;
}

int Params::getInt(const std::string& key, int def) const {
    auto it = params_.find(key);
    if (it == params_.end())
        return def;
    return std::atoi(it->second.c_str());
}

double Params::getFloat(const std::string& key, double def) const {
    auto it = params_.find(key);
    if (it == params_.end())
        return def;
    return std::atof(it->second.c_str());
}

bool Params::getBool(const std::string& key, bool def) const {
    auto it = params_.find(key);
    if (it == params_.end())
        return def;
    return it->second == "true" || it->second == "True" || it->second == "1";
}

// ============================================================================
// Interpreter Implementation
// ============================================================================

Interpreter::Interpreter(MagdaApi& api) : api_(api) {}

bool Interpreter::execute(const char* dslCode) {
    // Interpreter mutates TrackManager/ClipManager/SelectionManager, all of
    // which fire listeners that assume the message thread. Callers must
    // marshal onto the message thread before invoking execute().
    JUCE_ASSERT_MESSAGE_THREAD;

    if (!dslCode || !*dslCode) {
        ctx_.setError("Empty DSL code");
        return false;
    }

    ctx_ = InterpreterContext();

    // Seed implicit context from the UI selection so a bare statement like
    // `fx("reverb")` or `note(...)` targets the selected track/clip. Matches
    // InstructionExecutor's behaviour — the two paths must stay in sync or the
    // same request succeeds in one and fails in the other.
    auto& sm = api_.selection();
    auto selectedTrack = sm.getSelectedTrack();
    if (selectedTrack != INVALID_TRACK_ID && selectedTrack != MASTER_TRACK_ID)
        ctx_.currentTrackId = selectedTrack;

    auto selectedClip = sm.getSelectedClip();
    if (selectedClip != INVALID_CLIP_ID)
        ctx_.currentClipId = selectedClip;

    for (const auto& track : api_.tracks().getTracks())
        ctx_.trackIndexSnapshotIds.push_back(track.id);

    DBG("MAGDA DSL: Executing: " + juce::String(dslCode).substring(0, 200));

    Tokenizer tok(dslCode);

    int succeeded = 0;
    int failed = 0;
    juce::StringArray failureReasons;

    while (tok.hasMore()) {
        auto savedPos = tok.savePosition();

        if (!parseStatement(tok)) {
            // Log the error as a warning and skip to the next statement
            DBG("MAGDA DSL: statement failed: " + ctx_.error);
            failureReasons.add(ctx_.error);
            ctx_.addResult("[!] " + ctx_.error);
            ctx_.error.clear();
            ctx_.hasError = false;
            failed++;

            // Advance past the failed statement to the next one.
            // Statements start with a top-level DSL keyword at the beginning of a line.
            // Skip tokens until we find a statement-starting keyword or EOF.
            tok.restorePosition(savedPos);
            tok.next();  // skip past the keyword that started this failed statement
            while (tok.hasMore()) {
                auto next = tok.peek();
                if (next.is("track") || next.is("filter") || next.is("groove") ||
                    next.is("project") || next.type == TokenType::END_OF_INPUT)
                    break;
                tok.next();
            }
            continue;
        }

        succeeded++;

        if (tok.peek().is(TokenType::SEMICOLON))
            tok.next();
    }

    DBG("MAGDA DSL: Execution complete");

    if (succeeded == 0 && failed > 0) {
        // Surface the underlying reasons, not just the count, so failures like
        // an unresolved plugin alias are diagnosable from the error alone.
        auto reasons = failureReasons.joinIntoString("; ");
        const auto summary = failed == 1 ? juce::String("Command failed")
                                         : "All " + juce::String(failed) + " commands failed";
        ctx_.setError(summary + (reasons.isEmpty() ? juce::String() : ": " + reasons));
        return false;
    }

    return true;
}

bool Interpreter::parseStatement(Tokenizer& tok) {
    Token t = tok.peek();

    if (t.is("track"))
        return parseTrackStatement(tok);
    else if (t.is("filter"))
        return parseFilterStatement(tok);
    else if (t.is("groove"))
        return parseGrooveStatement(tok);
    else if (t.is("project"))
        return parseProjectStatement(tok);
    else if (t.type == TokenType::END_OF_INPUT) {
        return true;
    } else {
        ctx_.setError("Unexpected token '" + juce::String(t.value) + "' at line " +
                      juce::String(t.line));
        return false;
    }
}

bool Interpreter::parseProjectStatement(Tokenizer& tok) {
    tok.next();  // consume 'project'
    if (!tok.expect(TokenType::DOT) || !tok.expect("set")) {
        ctx_.setError("Expected '.set' after 'project'");
        return false;
    }
    if (!tok.expect(TokenType::LPAREN)) {
        ctx_.setError("Expected '(' after 'project.set'");
        return false;
    }

    Params params;
    if (!parseParams(tok, params))
        return false;
    if (!tok.expect(TokenType::RPAREN)) {
        ctx_.setError("Expected ')' after project.set parameters");
        return false;
    }
    return executeProjectSet(params);
}

bool Interpreter::parseTrackStatement(Tokenizer& tok) {
    tok.next();  // consume 'track'

    // A new track() head starts fresh device scope: clear any rack/chain
    // context left by a prior statement so a bare fx.add targets the track,
    // not a stale rack chain. rack.new re-establishes chain scope in-chain.
    ctx_.currentRackId = INVALID_RACK_ID;
    ctx_.currentChainId = INVALID_CHAIN_ID;

    if (!tok.expect(TokenType::LPAREN)) {
        ctx_.setError("Expected '(' after 'track'");
        return false;
    }

    Params params;
    if (!parseParams(tok, params))
        return false;

    if (!tok.expect(TokenType::RPAREN)) {
        ctx_.setError("Expected ')' after track parameters");
        return false;
    }

    auto& tm = api_.tracks();

    if (params.has("id")) {
        // Reference existing track by 1-based index
        int id = params.getInt("id");
        const int trackId = trackIndexToId(id);
        if (trackId < 0) {
            ctx_.setError("Track " + juce::String(id) + " not found");
            return false;
        }
        ctx_.currentTrackId = trackId;
    } else if (params.has("name")) {
        juce::String name(params.get("name"));
        bool forceNew = params.getBool("new", false);

        // An empty name that isn't explicitly `new` targets the current
        // selection rather than creating a blank track. This is how the
        // on-device command model expresses "add X to the current track" — it
        // emits no track name, so `track(name="").fx.add(...)`. currentTrackId
        // was seeded from the UI selection at the top of execute().
        if (name.isEmpty() && !forceNew && ctx_.currentTrackId >= 0) {
            DBG("MAGDA DSL: empty name -> targeting selected track " +
                juce::String(ctx_.currentTrackId));
        } else if (name.isEmpty() && !forceNew) {
            // Empty name means "the selected track" — with nothing selected
            // there is no target. Falling through would create a blank unnamed
            // track, which is both wrong and destructive-looking: "add @filter"
            // on an unselected project silently spawned a new track instead of
            // saying it had nowhere to put it.
            ctx_.setError("No track selected. Select a track first, or name one "
                          "(for example, \"add @filter to Bass\").");
            return false;
        } else {
            int existingId = forceNew ? -1 : findTrackByName(name);

            if (existingId >= 0) {
                ctx_.currentTrackId = existingId;
                DBG("MAGDA DSL: Found existing track '" + name + "'");
            } else {
                auto trackType = parseTrackType(params);
                auto trackId = tm.createTrack(name, trackType);
                ctx_.currentTrackId = trackId;
                api_.selection().selectTrack(trackId);
                ctx_.addResult("Created track '" + name + "'");
            }
        }
    } else {
        // track() with no params — create unnamed track
        auto trackType = parseTrackType(params);
        auto trackId = tm.createTrack("", trackType);
        ctx_.currentTrackId = trackId;
        api_.selection().selectTrack(trackId);
        ctx_.addResult("Created track");
    }

    return parseMethodChain(tok);
}

bool Interpreter::parseFilterStatement(Tokenizer& tok) {
    tok.next();  // consume 'filter'

    if (!tok.expect(TokenType::LPAREN)) {
        ctx_.setError("Expected '(' after 'filter'");
        return false;
    }

    Token collection = tok.next();
    if (!collection.is("tracks")) {
        ctx_.setError("Expected 'tracks' in filter, got '" + juce::String(collection.value) + "'");
        return false;
    }

    // Execute filter: find matching tracks
    auto& tm = api_.tracks();
    ctx_.filteredTrackIds.clear();

    if (tok.peek().is(TokenType::RPAREN)) {
        // filter(tracks) with no condition targets every track. This is the
        // master-selected fan-out: when the state snapshot carries
        // "scope":"all_tracks", the agent addresses all tracks at once
        // (e.g. filter(tracks).track.group(...) or .track.set(mute=true)).
        tok.next();  // consume ')'
        for (const auto& track : tm.getTracks())
            ctx_.filteredTrackIds.push_back(track.id);
    } else {
        if (!tok.expect(TokenType::COMMA)) {
            ctx_.setError("Expected ',' after 'tracks'");
            return false;
        }

        // Parse condition: track.field == "value"
        Token trackToken = tok.next();
        if (!trackToken.is("track")) {
            ctx_.setError("Expected 'track' in filter condition");
            return false;
        }

        if (!tok.expect(TokenType::DOT)) {
            ctx_.setError("Expected '.' after 'track'");
            return false;
        }

        Token field = tok.next();
        if (field.type != TokenType::IDENTIFIER) {
            ctx_.setError("Expected field name after 'track.'");
            return false;
        }

        Token op = tok.next();
        if (op.type != TokenType::EQUALS_EQUALS) {
            ctx_.setError("Expected '==' in filter condition");
            return false;
        }

        Token value = tok.next();
        if (value.type != TokenType::STRING) {
            ctx_.setError("Expected string value in filter condition");
            return false;
        }

        if (!tok.expect(TokenType::RPAREN)) {
            ctx_.setError("Expected ')' after filter condition");
            return false;
        }

        if (field.value == "name") {
            for (const auto& track : tm.getTracks()) {
                if (track.name == juce::String(value.value))
                    ctx_.filteredTrackIds.push_back(track.id);
            }
        }
    }

    ctx_.inFilterContext = true;
    ctx_.addResult("Filter matched " +
                   juce::String(static_cast<int>(ctx_.filteredTrackIds.size())) + " track(s)");

    bool result = parseMethodChain(tok);

    ctx_.inFilterContext = false;
    ctx_.filteredTrackIds.clear();

    return result;
}

bool Interpreter::parseMethodChain(Tokenizer& tok) {
    while (tok.peek().is(TokenType::DOT)) {
        tok.next();  // consume '.'

        Token first = tok.next();
        if (first.type != TokenType::IDENTIFIER) {
            ctx_.setError("Expected method name after '.'");
            return false;
        }

        // Check for dot-namespace syntax: namespace.method
        std::string methodKey;
        if (tok.peek().is(TokenType::DOT)) {
            tok.next();  // consume second '.'
            Token second = tok.next();
            if (second.type != TokenType::IDENTIFIER) {
                ctx_.setError("Expected method name after '" + juce::String(first.value) + ".'");
                return false;
            }
            methodKey = first.value + "." + second.value;
        } else {
            methodKey = first.value;
        }

        // Methods that parse their own syntax (no standard params)
        if (methodKey == "for_each") {
            if (!executeForEach(tok))
                return false;
            continue;
        }
        if (methodKey == "clips.select") {
            if (!executeSelectClips(tok))
                return false;
            continue;
        }
        if (methodKey == "notes.select") {
            if (!executeSelectNotes(tok))
                return false;
            continue;
        }

        if (!tok.expect(TokenType::LPAREN)) {
            ctx_.setError("Expected '(' after method '" + juce::String(methodKey) + "'");
            return false;
        }

        Params params;
        if (!parseParams(tok, params))
            return false;

        if (!tok.expect(TokenType::RPAREN)) {
            ctx_.setError("Expected ')' after method parameters");
            return false;
        }

        bool success = false;
        if (methodKey == "clip.new")
            success = executeNewClip(params);
        else if (methodKey == "track.set")
            success = executeSetTrack(params);
        else if (methodKey == "track.group")
            success = executeGroupTracks(params);
        else if (methodKey == "track.move")
            success = executeMoveTrack(params);
        else if (methodKey == "delete")
            success = executeDelete();
        else if (methodKey == "clip.delete")
            success = executeDeleteClip(params);
        else if (methodKey == "clip.rename")
            success = executeRenameClip(params);
        else if (methodKey == "clip.set")
            success = executeSetClip(params);
        else if (methodKey == "fx.add")
            success = executeAddFx(params);
        else if (methodKey == "rack.new")
            success = executeNewRack(params);
        else if (methodKey == "rack.delete")
            success = executeDeleteRack(params);
        else if (methodKey == "rack.set")
            success = executeSetRack(params);
        else if (methodKey == "rack.chain_new")
            success = executeNewRackChain(params);
        else if (methodKey == "rack.chain_delete")
            success = executeDeleteRackChain(params);
        else if (methodKey == "rack.chain_set")
            success = executeSetRackChain(params);
        else if (methodKey == "select")
            success = executeSelect();
        else if (methodKey == "notes.add")
            success = executeAddNote(params);
        else if (methodKey == "notes.add_chord")
            success = executeAddChord(params);
        else if (methodKey == "notes.add_arpeggio")
            success = executeAddArpeggio(params);
        else if (methodKey == "notes.delete")
            success = executeDeleteNotes();
        else if (methodKey == "notes.transpose")
            success = executeTranspose(params);
        else if (methodKey == "notes.set_pitch")
            success = executeSetPitch(params);
        else if (methodKey == "notes.set_velocity")
            success = executeSetVelocity(params);
        else if (methodKey == "notes.quantize")
            success = executeQuantize(params);
        else if (methodKey == "notes.resize")
            success = executeResizeNotes(params);
        else {
            ctx_.setError("Unknown method: " + juce::String(methodKey));
            return false;
        }

        if (!success)
            return false;
    }

    return true;
}

bool Interpreter::parseParams(Tokenizer& tok, Params& outParams) {
    outParams.clear();

    if (tok.peek().is(TokenType::RPAREN))
        return true;

    while (true) {
        Token key = tok.next();
        if (key.type != TokenType::IDENTIFIER) {
            ctx_.setError("Expected parameter name, got '" + juce::String(key.value) + "'");
            return false;
        }

        if (!tok.expect(TokenType::EQUALS)) {
            ctx_.setError("Expected '=' after parameter '" + juce::String(key.value) + "'");
            return false;
        }

        std::string value;
        if (!parseValue(tok, value))
            return false;

        outParams.set(key.value, value);

        if (tok.peek().is(TokenType::COMMA))
            tok.next();
        else
            break;
    }

    return true;
}

bool Interpreter::parseValue(Tokenizer& tok, std::string& outValue) {
    Token t = tok.next();

    if (t.type == TokenType::STRING || t.type == TokenType::NUMBER) {
        outValue = t.value;
        return true;
    }

    if (t.type == TokenType::IDENTIFIER) {
        // Check for function call: identifier(args...)
        if (tok.peek().is(TokenType::LPAREN)) {
            tok.next();  // consume '('
            return evaluateFunction(t.value, tok, outValue);
        }

        outValue = t.value;
        return true;
    }

    // '@' produces its own AT token; handle it as a sigil prefix.
    if (t.type == TokenType::AT) {
        // Consume pluginKey.paramKey as "@pluginKey.paramKey"
        Token pluginTok = tok.next();
        if (pluginTok.type != TokenType::IDENTIFIER) {
            ctx_.setError("Expected identifier after '@'");
            return false;
        }
        std::string sigilStr = "@" + pluginTok.value;
        if (tok.peek().is(TokenType::DOT)) {
            tok.next();  // consume '.'
            Token paramTok = tok.next();
            if (paramTok.type == TokenType::IDENTIFIER)
                sigilStr += "." + paramTok.value;
        }
        outValue = sigilStr;
        return true;
    }

    ctx_.setError("Expected value, got '" + juce::String(t.value) + "'");
    return false;
}

// ============================================================================
// Built-in Functions
// ============================================================================

bool Interpreter::evaluateFunction(const std::string& name, Tokenizer& tok, std::string& outValue) {
    // Parse arguments as comma-separated values
    std::vector<std::string> args;
    if (!tok.peek().is(TokenType::RPAREN)) {
        while (true) {
            std::string arg;
            if (!parseValue(tok, arg))
                return false;
            args.push_back(arg);
            if (tok.peek().is(TokenType::COMMA))
                tok.next();
            else
                break;
        }
    }
    if (!tok.expect(TokenType::RPAREN)) {
        ctx_.setError("Expected ')' after function arguments");
        return false;
    }

    if (name == "random") {
        if (args.size() != 2) {
            ctx_.setError("random() requires 2 arguments: random(min, max)");
            return false;
        }
        double lo = std::stod(args[0]);
        double hi = std::stod(args[1]);
        if (lo > hi)
            std::swap(lo, hi);

        static thread_local std::mt19937 rng{std::random_device{}()};

        // Integer range if both args are integers
        bool isInt =
            (args[0].find('.') == std::string::npos) && (args[1].find('.') == std::string::npos);
        if (isInt) {
            std::uniform_int_distribution<int> dist(static_cast<int>(lo), static_cast<int>(hi));
            outValue = std::to_string(dist(rng));
        } else {
            std::uniform_real_distribution<double> dist(lo, hi);
            outValue = std::to_string(dist(rng));
        }
        return true;
    }

    ctx_.setError("Unknown function: " + juce::String(name));
    return false;
}

// ============================================================================
// Execution Methods
// ============================================================================

bool Interpreter::executeNewClip(const Params& params) {
    if (ctx_.currentTrackId < 0) {
        ctx_.setError("No track context for clip.new");
        return false;
    }

    double lengthBars = params.getFloat("length_bars", 4.0);
    double bar;

    if (params.has("bar")) {
        bar = params.getFloat("bar", 1.0);
    } else {
        // No position specified — place after the last clip on this track
        bar = 1.0;
        const double beatsPerBar = barsToBeats(1.0);

        auto& cm = api_.clips();
        for (auto cid : cm.getClipsOnTrack(ctx_.currentTrackId)) {
            auto* clip = cm.getClip(cid);
            if (!clip)
                continue;
            // Placement is beat-authoritative, so use it directly rather than
            // assuming a four-beat bar through the legacy seconds cache.
            double clipEndBar = clip->placement.endBeat() / beatsPerBar + 1.0;
            double nextBar = std::ceil(clipEndBar - 0.001);  // tolerance for floating point
            if (nextBar > bar)
                bar = nextBar;
        }
    }

    if (bar < 1.0) {
        ctx_.setError("Bar number must be positive, got " + juce::String(bar));
        return false;
    }
    if (lengthBars <= 0.0) {
        ctx_.setError("Clip length must be positive, got " + juce::String(lengthBars));
        return false;
    }

    // Beats-authoritative: bars → beats via project time signature, no
    // seconds round-trip.
    double startBeats = barsToBeats(bar - 1.0);
    double lengthBeats = barsToBeats(lengthBars);

    auto& cm = api_.clips();
    auto clipId = cm.createMidiClipBeats(ctx_.currentTrackId, startBeats, lengthBeats);

    if (clipId < 0) {
        ctx_.setError("Failed to create clip");
        return false;
    }

    ctx_.currentClipId = clipId;
    api_.selection().selectClip(clipId);
    ctx_.addResult("Created MIDI clip at bar " + juce::String(bar, 2) + ", length " +
                   juce::String(lengthBars, 2) + " bars");
    return true;
}

bool Interpreter::executeProjectSet(const Params& params) {
    bool changed = false;

    if (params.has("tempo") || params.has("bpm")) {
        const double bpm = params.has("bpm") ? params.getFloat("bpm") : params.getFloat("tempo");
        if (bpm <= 0.0) {
            ctx_.setError("project.set tempo/bpm must be positive");
            return false;
        }
        api_.project().setTempo(bpm);
        ctx_.addResult("Set tempo to " +
                       juce::String(api_.project().getCurrentProjectInfo().tempo, 2) + " BPM");
        changed = true;
    }

    if (params.has("time_signature")) {
        const auto sig = juce::String(params.get("time_signature")).trim();
        const int slash = sig.indexOfChar('/');
        if (slash <= 0 || slash >= sig.length() - 1) {
            ctx_.setError("time_signature must be written as numerator/denominator, e.g. \"7/8\"");
            return false;
        }
        const int numerator = sig.substring(0, slash).getIntValue();
        const int denominator = sig.substring(slash + 1).getIntValue();
        if (numerator <= 0 || denominator <= 0) {
            ctx_.setError("time_signature values must be positive integers");
            return false;
        }
        api_.project().setTimeSignature(numerator, denominator);
        const auto& project = api_.project().getCurrentProjectInfo();
        ctx_.addResult("Set time signature to " + juce::String(project.timeSignatureNumerator) +
                       "/" + juce::String(project.timeSignatureDenominator));
        changed = true;
    }

    if (!changed) {
        ctx_.setError("project.set requires tempo, bpm, or time_signature");
        return false;
    }
    return true;
}

bool Interpreter::executeSetTrack(const Params& params) {
    auto& tm = api_.tracks();

    auto applyToTrack = [&](int trackId) {
        if (params.has("name"))
            tm.setTrackName(trackId, juce::String(params.get("name")));

        if (params.has("colour"))
            tm.setTrackColour(trackId, parseDslColour(params.get("colour")));
        else if (params.has("color"))
            tm.setTrackColour(trackId, parseDslColour(params.get("color")));

        if (params.has("volume_db")) {
            double db = params.getFloat("volume_db");
            float vol = static_cast<float>(std::pow(10.0, db / 20.0));
            tm.setTrackVolume(trackId, vol);
        }

        if (params.has("pan"))
            tm.setTrackPan(trackId, static_cast<float>(params.getFloat("pan")));

        if (params.has("mute"))
            tm.setTrackMuted(trackId, params.getBool("mute"));

        if (params.has("solo"))
            tm.setTrackSoloed(trackId, params.getBool("solo"));
    };

    if (ctx_.inFilterContext) {
        for (int trackId : ctx_.filteredTrackIds)
            applyToTrack(trackId);
        ctx_.addResult("Updated " + juce::String(static_cast<int>(ctx_.filteredTrackIds.size())) +
                       " track(s)");
    } else if (ctx_.currentTrackId >= 0) {
        applyToTrack(ctx_.currentTrackId);

        // Build result description
        juce::StringArray changes;
        if (params.has("name"))
            changes.add("name='" + juce::String(params.get("name")) + "'");
        if (params.has("colour"))
            changes.add("colour=" + juce::String(params.get("colour")));
        else if (params.has("color"))
            changes.add("color=" + juce::String(params.get("color")));
        if (params.has("volume_db"))
            changes.add("volume=" + juce::String(params.get("volume_db")) + "dB");
        if (params.has("pan"))
            changes.add("pan=" + juce::String(params.get("pan")));
        if (params.has("mute"))
            changes.add("mute=" + juce::String(params.get("mute")));
        if (params.has("solo"))
            changes.add("solo=" + juce::String(params.get("solo")));
        ctx_.addResult("Set track: " + changes.joinIntoString(", "));
    } else {
        ctx_.setError("No track context for track.set");
        return false;
    }

    return true;
}

bool Interpreter::executeGroupTracks(const Params& params) {
    std::vector<int> trackIds;
    if (params.has("tracks")) {
        std::stringstream ss(params.get("tracks"));
        std::string item;
        while (std::getline(ss, item, ',')) {
            juce::String token(item);
            const int oneBasedIndex = token.trim().getIntValue();
            const int trackId = trackIndexToId(oneBasedIndex);
            if (trackId < 0) {
                ctx_.setError("Track " + juce::String(oneBasedIndex) + " not found");
                return false;
            }
            if (std::find(trackIds.begin(), trackIds.end(), trackId) == trackIds.end())
                trackIds.push_back(trackId);
        }
    } else if (ctx_.inFilterContext) {
        trackIds = ctx_.filteredTrackIds;
    } else if (ctx_.currentTrackId >= 0) {
        trackIds.push_back(ctx_.currentTrackId);
    }

    if (trackIds.size() < 2) {
        ctx_.setError("track.group requires at least two tracks");
        return false;
    }

    const auto name = juce::String(params.get("name", "Group"));
    auto groupCmd = std::make_unique<GroupTracksCommand>(trackIds, name);
    auto* groupCmdPtr = groupCmd.get();
    api_.undo().executeCommand(std::move(groupCmd));
    const auto groupId = groupCmdPtr->getCreatedGroupId();
    if (groupId == INVALID_TRACK_ID) {
        ctx_.setError("Failed to group tracks");
        return false;
    }

    ctx_.currentTrackId = groupId;
    ctx_.inFilterContext = false;
    ctx_.filteredTrackIds.clear();
    api_.selection().selectTrack(groupId);
    ctx_.addResult("Grouped " + juce::String(static_cast<int>(trackIds.size())) + " track(s) as '" +
                   name + "'");
    return true;
}

bool Interpreter::executeMoveTrack(const Params& params) {
    if (!params.has("index")) {
        ctx_.setError("track.move requires index=N (1-based position)");
        return false;
    }
    const int position = params.getInt("index");

    // Apply to the filtered set or the current track. Moving every filtered
    // track to the same slot is rarely meaningful, but we honour it in order so
    // the final order is deterministic.
    if (ctx_.inFilterContext) {
        for (int trackId : ctx_.filteredTrackIds)
            api_.undo().executeCommand(std::make_unique<MoveTrackCommand>(trackId, position));
        ctx_.addResult("Moved " + juce::String(static_cast<int>(ctx_.filteredTrackIds.size())) +
                       " track(s) to position " + juce::String(position));
        return true;
    }

    if (ctx_.currentTrackId < 0) {
        ctx_.setError("No track context for track.move (use track(id=N) first)");
        return false;
    }

    api_.undo().executeCommand(std::make_unique<MoveTrackCommand>(ctx_.currentTrackId, position));
    ctx_.addResult("Moved track to position " + juce::String(position));
    return true;
}

bool Interpreter::executeDelete() {
    auto& tm = api_.tracks();

    if (ctx_.inFilterContext) {
        // Delete in reverse order to avoid index shifting issues
        auto ids = ctx_.filteredTrackIds;
        for (auto it = ids.rbegin(); it != ids.rend(); ++it)
            tm.deleteTrack(*it);
        ctx_.addResult("Deleted " + juce::String(static_cast<int>(ids.size())) + " track(s)");
        ctx_.filteredTrackIds.clear();
    } else if (ctx_.currentTrackId >= 0) {
        tm.deleteTrack(ctx_.currentTrackId);
        ctx_.addResult("Deleted track");
        ctx_.currentTrackId = -1;
    } else {
        ctx_.setError("No track context for delete");
        return false;
    }

    return true;
}

bool Interpreter::executeDeleteClip(const Params& params) {
    if (ctx_.currentTrackId < 0) {
        ctx_.setError("No track context for clip.delete");
        return false;
    }

    auto& cm = api_.clips();
    auto clipIds = cm.getClipsOnTrack(ctx_.currentTrackId);

    int index = params.getInt("index", 0);
    if (index >= 0 && index < static_cast<int>(clipIds.size())) {
        cm.deleteClip(clipIds[static_cast<size_t>(index)]);
        ctx_.addResult("Deleted clip at index " + juce::String(index));
    } else {
        ctx_.setError("Clip index " + juce::String(index) + " out of range");
        return false;
    }

    return true;
}

bool Interpreter::executeSetClip(const Params& params) {
    if (!params.has("enabled")) {
        ctx_.setError("clip.set requires 'enabled' parameter");
        return false;
    }
    const bool enabled = params.getBool("enabled", true);
    auto& cm = api_.clips();
    const juce::String verb = enabled ? "Enabled" : "Disabled";

    if (params.has("index")) {
        if (ctx_.currentTrackId < 0) {
            ctx_.setError("No track context for clip.set with index");
            return false;
        }
        auto clipIds = cm.getClipsOnTrack(ctx_.currentTrackId);
        int index = params.getInt("index");
        if (index < 0 || index >= static_cast<int>(clipIds.size())) {
            ctx_.setError("Clip index " + juce::String(index) + " out of range");
            return false;
        }
        cm.setClipEnabled(clipIds[static_cast<size_t>(index)], enabled);
        ctx_.addResult(verb + " clip at index " + juce::String(index));
        return true;
    }

    // Otherwise act on the current selection — which is what a preceding
    // clips.select(...) in the same statement will have just populated.
    auto& sm = api_.selection();
    const auto& selected = sm.getSelectedClips();
    if (!selected.empty()) {
        for (auto id : selected)
            cm.setClipEnabled(id, enabled);
        ctx_.addResult(verb + " " + juce::String(static_cast<int>(selected.size())) + " clips");
        return true;
    }
    if (auto single = sm.getSelectedClip(); single != INVALID_CLIP_ID) {
        cm.setClipEnabled(single, enabled);
        ctx_.addResult(verb + " clip");
        return true;
    }
    ctx_.setError("No clip selected for clip.set");
    return false;
}

bool Interpreter::executeRenameClip(const Params& params) {
    if (!params.has("name")) {
        ctx_.setError("clip.rename requires 'name' parameter");
        return false;
    }

    juce::String newName(params.get("name"));
    auto& cm = api_.clips();

    if (params.has("index")) {
        // Rename a specific clip by index on the current track
        if (ctx_.currentTrackId < 0) {
            ctx_.setError("No track context for clip.rename with index");
            return false;
        }
        auto clipIds = cm.getClipsOnTrack(ctx_.currentTrackId);
        int index = params.getInt("index");
        if (index < 0 || index >= static_cast<int>(clipIds.size())) {
            ctx_.setError("Clip index " + juce::String(index) + " out of range");
            return false;
        }
        cm.setClipName(clipIds[static_cast<size_t>(index)], newName);
        ctx_.addResult("Renamed clip at index " + juce::String(index) + " to '" + newName + "'");
    } else {
        // Rename all currently selected clips
        auto& sm = api_.selection();
        const auto& selected = sm.getSelectedClips();
        auto singleClip = sm.getSelectedClip();

        if (!selected.empty()) {
            // Sort clips by start time so {i} numbering follows timeline order
            std::vector<ClipId> sorted(selected.begin(), selected.end());
            std::sort(sorted.begin(), sorted.end(), [&](ClipId a, ClipId b) {
                auto* ca = cm.getClip(a);
                auto* cb = cm.getClip(b);
                if (!ca || !cb)
                    return a < b;
                return ca->startTime < cb->startTime;
            });

            int idx = 1;
            for (auto clipId : sorted) {
                juce::String name = newName.replace("{i}", juce::String(idx));
                cm.setClipName(clipId, name);
                idx++;
            }
            ctx_.addResult("Renamed " + juce::String(static_cast<int>(selected.size())) +
                           " selected clip(s)");
        } else if (singleClip != INVALID_CLIP_ID) {
            cm.setClipName(singleClip, newName);
            ctx_.addResult("Renamed selected clip to '" + newName + "'");
        } else {
            ctx_.setError("No clip index provided and no clips selected");
            return false;
        }
    }

    return true;
}

bool Interpreter::executeAddFx(const Params& params) {
    if (ctx_.currentTrackId < 0) {
        ctx_.setError("No track context for fx.add");
        return false;
    }

    if (!params.has("name")) {
        ctx_.setError("fx.add requires 'name' parameter");
        return false;
    }

    // Place the device into the active rack chain when one is in scope (set by a
    // preceding rack.new/chain_new), else onto the track's top-level chain. This
    // is what lets "create a rack with @serum" put the device inside the rack.
    auto placeDevice = [this](const DeviceInfo& dev) {
        if (ctx_.currentChainId != INVALID_CHAIN_ID && ctx_.currentRackId != INVALID_RACK_ID)
            return api_.tracks().addDeviceToChain(ctx_.currentTrackId, ctx_.currentRackId,
                                                  ctx_.currentChainId, dev);
        return api_.tracks().addDeviceToTrack(ctx_.currentTrackId, dev);
    };

    juce::String fxName(params.get("name"));
    juce::String formatHint(params.get("format"));

    // Strip <alias> token wrapper — resolve at execution time
    if (fxName.startsWith("<") && fxName.endsWith(">"))
        fxName = fxName.substring(1, fxName.length() - 1);

    DBG("MAGDA DSL fx.add: name=\"" + fxName + "\" track=" + juce::String(ctx_.currentTrackId));

    // --- Internal plugin lookup ---
    // Built-in plugins come from the audio registries through internal_plugins.hpp.
    // Display names, IDs and registry compatibility aliases are all accepted.
    if (const auto* match = lookupInternalPluginByAlias(fxName)) {
        DeviceInfo device;
        device.name = match->displayName;
        device.pluginId = match->pluginId;
        device.format = PluginFormat::Internal;
        device.deviceType = match->deviceType;
        device.isInstrument = (match->deviceType == DeviceType::Instrument);

        DBG("MAGDA DSL fx.add: resolved internal '" + match->displayName + "' (pluginId=" +
            match->pluginId + ", instrument=" + (device.isInstrument ? "yes" : "no") + ")");

        auto deviceId = placeDevice(device);
        if (deviceId == INVALID_DEVICE_ID) {
            juce::String why = "Failed to add internal device '" + match->displayName +
                               "' to track " + juce::String(ctx_.currentTrackId);
            if (device.isInstrument)
                why += " (track cannot host an instrument)";
            DBG("MAGDA DSL fx.add: " + why);
            ctx_.setError(why);
            return false;
        }
        ctx_.addResult("Added internal device '" + match->displayName + "'");
        return true;
    }

    DBG("MAGDA DSL fx.add: '" + fxName +
        "' not found in internal registry; trying scanned plugins");

    const auto pluginTypes = formatHint.isNotEmpty() ? api_.plugins().getAllExternalPlugins()
                                                     : api_.plugins().getExternalPlugins();
    const auto requestedFormat =
        formatHint.isNotEmpty() ? maybePluginFormatFromName(formatHint) : std::nullopt;
    const DeviceInfo* bestMatch = nullptr;

    DBG("DSL executeAddFx: looking for plugin fxName=\"" + fxName + "\"");
    for (const auto& desc : pluginTypes) {
        // Match by exact plugin name or generated alias (e.g. "pro_q_3" matches "Pro-Q 3")
        auto alias = magda::pluginNameToAlias(desc.name);
        bool nameMatch = desc.name.equalsIgnoreCase(fxName) || alias.equalsIgnoreCase(fxName);
        DBG("  comparing: desc.name=\"" + desc.name + "\" alias=\"" + alias +
            "\" match=" + juce::String(nameMatch ? "YES" : "no"));
        if (nameMatch) {
            // Filter by format hint if provided
            if (formatHint.isNotEmpty() && (!requestedFormat || desc.format != *requestedFormat))
                continue;
            bestMatch = &desc;
            break;
        }
    }

    if (!bestMatch) {
        ctx_.setError("FX plugin '" + fxName + "' not found (not internal or in scanned plugins)");
        return false;
    }

    DeviceInfo device = *bestMatch;
    juce::String matchedFormat = bestMatch->getFormatString();
    juce::String matchedName = bestMatch->name;
    juce::String matchedManufacturer = bestMatch->manufacturer;

    auto deviceId = placeDevice(device);
    if (deviceId == INVALID_DEVICE_ID) {
        ctx_.setError("Failed to add FX '" + fxName + "' to track");
        return false;
    }

    // If the track was named with the alias form, rename it to the real plugin name.
    auto& tm = api_.tracks();
    if (auto* trackInfo = tm.getTrack(ctx_.currentTrackId)) {
        if (trackInfo->name.equalsIgnoreCase(fxName) && !fxName.equalsIgnoreCase(matchedName))
            tm.setTrackName(ctx_.currentTrackId, matchedName);
    }

    ctx_.addResult("Added " + matchedFormat + " FX '" + matchedName + "' by " +
                   matchedManufacturer);
    return true;
}

bool Interpreter::executeNewRack(const Params& params) {
    if (ctx_.currentTrackId < 0) {
        ctx_.setError("No track context for rack.new");
        return false;
    }
    const auto rackId =
        api_.tracks().addRackToTrack(ctx_.currentTrackId, juce::String(params.get("name", "Rack")));
    if (rackId == INVALID_RACK_ID) {
        ctx_.setError("Failed to create rack");
        return false;
    }
    ctx_.currentRackId = rackId;
    ctx_.currentChainId = INVALID_CHAIN_ID;
    if (const auto* rack = api_.tracks().getRack(ctx_.currentTrackId, rackId);
        rack != nullptr && !rack->chains.empty())
        ctx_.currentChainId = rack->chains.front().id;
    ctx_.addResult("Created rack '" + juce::String(params.get("name", "Rack")) +
                   "' (id=" + juce::String(rackId) + ")");
    return true;
}

bool Interpreter::executeDeleteRack(const Params& params) {
    if (ctx_.currentTrackId < 0) {
        ctx_.setError("No track context for rack.delete");
        return false;
    }
    const RackId rackId = params.getInt("id", ctx_.currentRackId);
    if (rackId == INVALID_RACK_ID ||
        api_.tracks().getRack(ctx_.currentTrackId, rackId) == nullptr) {
        ctx_.setError("rack.delete requires a valid id or preceding rack.new");
        return false;
    }
    api_.tracks().removeRackFromTrack(ctx_.currentTrackId, rackId);
    if (ctx_.currentRackId == rackId) {
        ctx_.currentRackId = INVALID_RACK_ID;
        ctx_.currentChainId = INVALID_CHAIN_ID;
    }
    ctx_.addResult("Deleted rack " + juce::String(rackId));
    return true;
}

bool Interpreter::executeSetRack(const Params& params) {
    if (ctx_.currentTrackId < 0) {
        ctx_.setError("No track context for rack.set");
        return false;
    }
    const RackId rackId = params.getInt("id", ctx_.currentRackId);
    if (rackId == INVALID_RACK_ID ||
        api_.tracks().getRack(ctx_.currentTrackId, rackId) == nullptr) {
        ctx_.setError("rack.set requires a valid id or preceding rack.new");
        return false;
    }
    bool changed = false;
    if (params.has("bypassed")) {
        api_.tracks().setRackBypassed(ctx_.currentTrackId, rackId, params.getBool("bypassed"));
        changed = true;
    }
    if (params.has("volume_db")) {
        api_.tracks().setRackVolume(ctx_.currentTrackId, rackId,
                                    static_cast<float>(params.getFloat("volume_db")));
        changed = true;
    }
    if (!changed) {
        ctx_.setError("rack.set supports bypassed and volume_db");
        return false;
    }
    ctx_.currentRackId = rackId;
    ctx_.addResult("Updated rack " + juce::String(rackId));
    return true;
}

bool Interpreter::executeNewRackChain(const Params& params) {
    if (ctx_.currentTrackId < 0) {
        ctx_.setError("No track context for rack.chain_new");
        return false;
    }
    const RackId rackId = params.getInt("rack_id", ctx_.currentRackId);
    if (rackId == INVALID_RACK_ID ||
        api_.tracks().getRack(ctx_.currentTrackId, rackId) == nullptr) {
        ctx_.setError("rack.chain_new requires rack_id or preceding rack.new");
        return false;
    }
    const auto chainId = api_.tracks().addChainToRack(ctx_.currentTrackId, rackId,
                                                      juce::String(params.get("name", "Chain")));
    if (chainId == INVALID_CHAIN_ID) {
        ctx_.setError("Failed to create rack chain");
        return false;
    }
    ctx_.currentRackId = rackId;
    ctx_.currentChainId = chainId;
    ctx_.addResult("Created rack chain '" + juce::String(params.get("name", "Chain")) +
                   "' (id=" + juce::String(chainId) + ")");
    return true;
}

bool Interpreter::executeDeleteRackChain(const Params& params) {
    if (ctx_.currentTrackId < 0) {
        ctx_.setError("No track context for rack.chain_delete");
        return false;
    }
    const RackId rackId = params.getInt("rack_id", ctx_.currentRackId);
    const ChainId chainId = params.getInt("chain_id", ctx_.currentChainId);
    if (rackId == INVALID_RACK_ID || chainId == INVALID_CHAIN_ID ||
        api_.tracks().getChain(ctx_.currentTrackId, rackId, chainId) == nullptr) {
        ctx_.setError("rack.chain_delete requires valid rack_id and chain_id");
        return false;
    }
    api_.tracks().removeChainFromRack(ctx_.currentTrackId, rackId, chainId);
    if (ctx_.currentChainId == chainId)
        ctx_.currentChainId = INVALID_CHAIN_ID;
    ctx_.addResult("Deleted rack chain " + juce::String(chainId));
    return true;
}

bool Interpreter::executeSetRackChain(const Params& params) {
    if (ctx_.currentTrackId < 0) {
        ctx_.setError("No track context for rack.chain_set");
        return false;
    }
    const RackId rackId = params.getInt("rack_id", ctx_.currentRackId);
    const ChainId chainId = params.getInt("chain_id", ctx_.currentChainId);
    if (rackId == INVALID_RACK_ID || chainId == INVALID_CHAIN_ID ||
        api_.tracks().getChain(ctx_.currentTrackId, rackId, chainId) == nullptr) {
        ctx_.setError("rack.chain_set requires valid rack_id and chain_id");
        return false;
    }

    bool changed = false;
    if (params.has("name")) {
        api_.tracks().setChainName(ctx_.currentTrackId, rackId, chainId,
                                   juce::String(params.get("name")));
        changed = true;
    }
    if (params.has("muted")) {
        api_.tracks().setChainMuted(ctx_.currentTrackId, rackId, chainId, params.getBool("muted"));
        changed = true;
    }
    if (params.has("bypassed")) {
        api_.tracks().setChainBypassed(ctx_.currentTrackId, rackId, chainId,
                                       params.getBool("bypassed"));
        changed = true;
    }
    if (params.has("solo")) {
        api_.tracks().setChainSolo(ctx_.currentTrackId, rackId, chainId, params.getBool("solo"));
        changed = true;
    }
    if (params.has("volume_db")) {
        api_.tracks().setChainVolume(ctx_.currentTrackId, rackId, chainId,
                                     static_cast<float>(params.getFloat("volume_db")));
        changed = true;
    }
    if (params.has("pan")) {
        api_.tracks().setChainPan(ctx_.currentTrackId, rackId, chainId,
                                  static_cast<float>(params.getFloat("pan")));
        changed = true;
    }
    if (params.has("output")) {
        api_.tracks().setChainOutput(ctx_.currentTrackId, rackId, chainId, params.getInt("output"));
        changed = true;
    }
    if (!changed) {
        ctx_.setError("rack.chain_set requires one or more supported properties");
        return false;
    }
    ctx_.currentRackId = rackId;
    ctx_.currentChainId = chainId;
    ctx_.addResult("Updated rack chain " + juce::String(chainId));
    return true;
}

bool Interpreter::executeForEach(Tokenizer& tok) {
    if (!ctx_.inFilterContext) {
        ctx_.setError("for_each can only be used in a filter context");
        return false;
    }

    if (ctx_.filteredTrackIds.empty()) {
        // Skip the body — consume until matching closing paren
        if (!tok.expect(TokenType::LPAREN)) {
            ctx_.setError("Expected '(' after 'for_each'");
            return false;
        }
        int depth = 1;
        while (depth > 0 && tok.hasMore()) {
            Token t = tok.next();
            if (t.is(TokenType::LPAREN))
                depth++;
            else if (t.is(TokenType::RPAREN))
                depth--;
        }
        ctx_.addResult("for_each: no tracks matched filter");
        return true;
    }

    if (!tok.expect(TokenType::LPAREN)) {
        ctx_.setError("Expected '(' after 'for_each'");
        return false;
    }

    // Save tokenizer position at the start of the chain inside for_each(...)
    auto savedPos = tok.savePosition();
    int savedTrackId = ctx_.currentTrackId;
    bool wasInFilterContext = ctx_.inFilterContext;

    // Exit filter context so inner methods operate on single currentTrackId
    ctx_.inFilterContext = false;

    int successCount = 0;

    for (size_t i = 0; i < ctx_.filteredTrackIds.size(); ++i) {
        ctx_.currentTrackId = ctx_.filteredTrackIds[i];

        if (i > 0)
            tok.restorePosition(savedPos);

        if (!parseMethodChain(tok)) {
            ctx_.currentTrackId = savedTrackId;
            ctx_.inFilterContext = wasInFilterContext;
            return false;
        }

        successCount++;
    }

    // After the loop, tokenizer is positioned after the chain — consume the closing ')'
    if (!tok.expect(TokenType::RPAREN)) {
        ctx_.setError("Expected ')' after for_each body");
        ctx_.currentTrackId = savedTrackId;
        ctx_.inFilterContext = wasInFilterContext;
        return false;
    }

    ctx_.currentTrackId = savedTrackId;
    ctx_.inFilterContext = wasInFilterContext;
    ctx_.addResult("for_each: applied to " + juce::String(successCount) + " track(s)");
    return true;
}

bool Interpreter::executeSelect() {
    if (ctx_.inFilterContext) {
        // Select first matching track
        if (!ctx_.filteredTrackIds.empty()) {
            api_.selection().selectTrack(ctx_.filteredTrackIds.front());
            ctx_.addResult("Selected track");
        } else {
            ctx_.addResult("No tracks matched filter");
        }
    } else if (ctx_.currentTrackId >= 0) {
        api_.selection().selectTrack(ctx_.currentTrackId);
        ctx_.addResult("Selected track");
    } else {
        ctx_.setError("No track context for select");
        return false;
    }
    return true;
}

bool Interpreter::executeSelectClips(Tokenizer& tok) {
    // Parse: (clip.field op value) — or () meaning every clip on the track.
    if (!tok.expect(TokenType::LPAREN)) {
        ctx_.setError("Expected '(' after 'clips.select'");
        return false;
    }

    // `clips.select()` with no predicate = select all clips on the track. Both
    // command-model backends have always emitted this for "select all clips",
    // and it errored out with "Expected 'clip' in clips.select condition"
    // because the parser demanded a condition. An empty predicate is the
    // natural reading of "all", and the grammar allows it.
    const bool selectAll = tok.peek().is(TokenType::RPAREN);
    if (selectAll)
        tok.next();  // consume ')'

    Token field;
    Token op;
    std::string valueStr;
    if (!selectAll) {
        if (!tok.expect("clip")) {
            ctx_.setError("Expected 'clip' in clips.select condition");
            return false;
        }
        if (!tok.expect(TokenType::DOT)) {
            ctx_.setError("Expected '.' after 'clip'");
            return false;
        }

        field = tok.next();
        if (field.type != TokenType::IDENTIFIER) {
            ctx_.setError("Expected field name after 'clip.'");
            return false;
        }

        op = tok.next();
        if (op.type != TokenType::EQUALS_EQUALS && op.type != TokenType::NOT_EQUALS &&
            op.type != TokenType::GREATER && op.type != TokenType::GREATER_EQUALS &&
            op.type != TokenType::LESS && op.type != TokenType::LESS_EQUALS) {
            ctx_.setError("Expected comparison operator in clips.select condition");
            return false;
        }

        if (!parseValue(tok, valueStr))
            return false;

        if (!tok.expect(TokenType::RPAREN)) {
            ctx_.setError("Expected ')' after clips.select condition");
            return false;
        }
    }

    // Determine if this is a string or numeric field
    bool isStringField = (field.value == "name" || field.value == "type");

    const double beatsPerBar = barsToBeats(1.0);

    double numValue = isStringField ? 0.0 : std::atof(valueStr.c_str());

    // Numeric comparator
    auto compareNum = [&](double clipVal) -> bool {
        if (op.type == TokenType::EQUALS_EQUALS)
            return std::abs(clipVal - numValue) < 0.001;
        if (op.type == TokenType::NOT_EQUALS)
            return std::abs(clipVal - numValue) >= 0.001;
        if (op.type == TokenType::GREATER)
            return clipVal > numValue;
        if (op.type == TokenType::GREATER_EQUALS)
            return clipVal >= numValue;
        if (op.type == TokenType::LESS)
            return clipVal < numValue;
        if (op.type == TokenType::LESS_EQUALS)
            return clipVal <= numValue;
        return false;
    };

    // String comparator (== and != only, case-insensitive contains for ==)
    auto compareStr = [&](const juce::String& clipStr) -> bool {
        if (op.type == TokenType::EQUALS_EQUALS)
            return clipStr.equalsIgnoreCase(juce::String(valueStr));
        if (op.type == TokenType::NOT_EQUALS)
            return !clipStr.equalsIgnoreCase(juce::String(valueStr));
        return false;
    };

    auto& cm = api_.clips();

    // Resolve a clip field to either a numeric or string match
    auto matchClip = [&](const ClipInfo* clip) -> bool {
        if (selectAll)
            return true;  // no predicate = every clip on the target
        if (isStringField) {
            juce::String strVal;
            if (field.value == "name")
                strVal = clip->name;
            else if (field.value == "type")
                strVal = (clip->isAudio()) ? "audio" : "midi";
            return compareStr(strVal);
        }

        // Numeric fields
        double val = 0.0;
        if (field.value == "length_bars")
            val = clip->placement.lengthBeats / beatsPerBar;
        else if (field.value == "start_bar")
            val = clip->placement.startBeat / beatsPerBar + 1.0;
        else if (field.value == "length")
            val = clip->length;
        else if (field.value == "start")
            val = clip->startTime;
        else if (field.value == "start_beats")
            val = clip->startBeats;
        else if (field.value == "id")
            val = static_cast<double>(clip->id);
        else if (field.value == "track_id")
            val = static_cast<double>(clip->trackId);
        else {
            ctx_.setError("Unknown clip field: " + juce::String(field.value));
            return false;
        }
        return compareNum(val);
    };

    auto matchOnTrack = [&](int trackId) {
        auto clipIds = cm.getClipsOnTrack(trackId);
        std::unordered_set<ClipId> matched;

        for (auto clipId : clipIds) {
            auto* clip = cm.getClip(clipId);
            if (!clip)
                continue;
            if (matchClip(clip))
                matched.insert(clipId);
        }
        return matched;
    };

    std::unordered_set<ClipId> allMatched;

    if (ctx_.inFilterContext) {
        for (int trackId : ctx_.filteredTrackIds) {
            auto m = matchOnTrack(trackId);
            allMatched.insert(m.begin(), m.end());
        }
    } else if (ctx_.currentTrackId >= 0) {
        allMatched = matchOnTrack(ctx_.currentTrackId);
    } else {
        auto& tm = api_.tracks();
        for (const auto& track : tm.getTracks()) {
            auto m = matchOnTrack(track.id);
            allMatched.insert(m.begin(), m.end());
        }
    }

    if (ctx_.hasError)
        return false;

    if (allMatched.empty()) {
        ctx_.addResult("No clips matched the criteria");
    } else if (allMatched.size() == 1) {
        api_.selection().selectClip(*allMatched.begin());
        ctx_.addResult("Selected 1 clip");
    } else {
        api_.selection().selectClips(allMatched);
        ctx_.addResult("Selected " + juce::String(static_cast<int>(allMatched.size())) + " clips");
    }

    return true;
}

TrackType Interpreter::parseTrackType(const Params& params) {
    if (!params.has("type"))
        return TrackType::Audio;

    std::string typeStr = params.get("type");
    if (typeStr == "group")
        return TrackType::Group;
    if (typeStr == "aux")
        return TrackType::Aux;
    return TrackType::Audio;
}

int Interpreter::findTrackByName(const juce::String& name) const {
    auto& tm = api_.tracks();
    for (const auto& track : tm.getTracks()) {
        if (track.name.equalsIgnoreCase(name))
            return track.id;
    }
    return -1;
}

int Interpreter::trackIndexToId(int oneBasedIndex) const {
    const int index = oneBasedIndex - 1;
    if (index < 0)
        return -1;

    if (index < static_cast<int>(ctx_.trackIndexSnapshotIds.size()))
        return ctx_.trackIndexSnapshotIds[static_cast<size_t>(index)];

    auto& tm = api_.tracks();
    if (index >= tm.getNumTracks())
        return -1;
    return tm.getTracks()[static_cast<size_t>(index)].id;
}

double Interpreter::barsToTime(double bar) const {
    // Convert 1-based bar number to seconds
    double bpm = api_.project().getCurrentProjectInfo().tempo;
    if (!isValidBpm(bpm))
        bpm = 120.0;

    const double beatsPerBar = barsToBeats(1.0);
    return (bar - 1.0) * beatsPerBar * 60.0 / bpm;
}

double Interpreter::barsToBeats(double bars) const {
    // Use project time signature; never assume 4/4 here — the seconds round
    // trip is what got us into trouble under non-4/4 sigs.
    int beatsPerBar = api_.project().getCurrentProjectInfo().timeSignatureNumerator;
    if (beatsPerBar <= 0)
        beatsPerBar = 4;
    return bars * static_cast<double>(beatsPerBar);
}

// ============================================================================
// State Snapshot
// ============================================================================

namespace {
std::atomic<bool> g_contextEnabled{true};

constexpr int kMaxSelectedDeviceParameters = 24;
constexpr int kMaxSelectedTrackDevices = 64;

juce::String deviceTypeName(DeviceType type) {
    switch (type) {
        case DeviceType::Instrument:
            return "instrument";
        case DeviceType::Effect:
            return "effect";
        case DeviceType::MIDI:
            return "midi";
        case DeviceType::Analysis:
            return "analysis";
    }
    return "unknown";
}

juce::var capabilitySummary(const juce::String& pluginId) {
    auto* object = new juce::DynamicObject();
    const auto* catalogEntry = lookupInternalPluginById(pluginId);
    const auto& capabilities = getInternalPluginCapabilities(pluginId);
    object->setProperty("catalogued", catalogEntry != nullptr);
    object->setProperty("addable", capabilities.addable);
    object->setProperty("automatable", capabilities.automatable);
    object->setProperty("drum_role_provider", capabilities.drumRoleProvider);
    object->setProperty("sound_design_agent", capabilities.supportsSoundDesign());
    object->setProperty("coder_agent", capabilities.supportsCoder());

    juce::Array<juce::var> aliases;
    for (const auto& alias : capabilities.parameterAliases)
        aliases.add(alias);
    object->setProperty("parameter_aliases", aliases);
    return juce::var(object);
}

juce::var parameterSummary(const ParameterInfo& parameter) {
    auto* object = new juce::DynamicObject();
    object->setProperty("index", parameter.paramIndex);
    object->setProperty("name", parameter.name.substring(0, 96));
    if (parameter.unit.isNotEmpty())
        object->setProperty("unit", parameter.unit.substring(0, 32));
    if (std::isfinite(parameter.currentValue))
        object->setProperty("value", parameter.currentValue);
    if (std::isfinite(parameter.minValue))
        object->setProperty("min", parameter.minValue);
    if (std::isfinite(parameter.maxValue))
        object->setProperty("max", parameter.maxValue);
    object->setProperty("modulatable", parameter.modulatable);
    return juce::var(object);
}

juce::var deviceSummary(const DeviceInfo& device, const ChainNodePath& path,
                        bool includeParameters) {
    auto* object = new juce::DynamicObject();
    object->setProperty("id", device.id);
    object->setProperty("path", path.toString());
    object->setProperty("display_name", device.name.substring(0, 128));
    object->setProperty("plugin_id", device.pluginId.substring(0, 256));
    object->setProperty("type", deviceTypeName(device.deviceType));
    object->setProperty("bypassed", device.bypassed);
    object->setProperty("parameter_count", static_cast<int>(device.parameters.size()));
    object->setProperty("capabilities", capabilitySummary(device.pluginId));

    if (includeParameters) {
        juce::Array<juce::var> parameters;
        for (const auto& parameter : device.parameters) {
            if (parameter.hidden)
                continue;
            parameters.add(parameterSummary(parameter));
            if (parameters.size() >= kMaxSelectedDeviceParameters)
                break;
        }
        object->setProperty("parameters", parameters);
        object->setProperty("parameters_truncated", static_cast<int>(device.parameters.size()) >
                                                        kMaxSelectedDeviceParameters);
    }
    return juce::var(object);
}

struct DeviceAtPath {
    const DeviceInfo* device = nullptr;
    ChainNodePath path;
};

void collectRackDevices(const RackInfo& rack, const ChainNodePath& rackPath,
                        std::vector<DeviceAtPath>& devices) {
    for (const auto& chain : rack.chains) {
        const auto chainPath = rackPath.withChain(chain.id);
        for (const auto& element : chain.elements) {
            if (isDevice(element)) {
                const auto& device = getDevice(element);
                devices.push_back({&device, chainPath.withDevice(device.id)});
            } else {
                const auto& nestedRack = getRack(element);
                const auto nestedPath = chainPath.withRack(nestedRack.id);
                collectRackDevices(nestedRack, nestedPath, devices);
            }
        }
    }
}

std::vector<DeviceAtPath> collectTrackDevices(const TrackInfo& track) {
    std::vector<DeviceAtPath> devices;
    for (const auto& element : track.chain.fxChainElements) {
        if (isDevice(element)) {
            const auto& device = getDevice(element);
            devices.push_back({&device, ChainNodePath::topLevelDevice(track.id, device.id)});
        } else {
            const auto& rack = getRack(element);
            collectRackDevices(rack, ChainNodePath::rack(track.id, rack.id), devices);
        }
    }
    for (const auto& element : track.chain.postFxChainElements) {
        devices.push_back(
            {&element.device, ChainNodePath::postFxDevice(track.id, element.device.id)});
    }
    for (const auto& element : track.chain.mixerAnalysisElements) {
        devices.push_back(
            {&element.device, ChainNodePath::mixerAnalysisDevice(track.id, element.device.id)});
    }
    return devices;
}
}  // namespace

void Interpreter::setContextEnabled(bool enabled) {
    g_contextEnabled.store(enabled, std::memory_order_relaxed);
}

bool Interpreter::isContextEnabled() {
    return g_contextEnabled.load(std::memory_order_relaxed);
}

juce::String Interpreter::buildStateSnapshot(MagdaApi& api) {
    auto& tm = api.tracks();

    auto* root = new juce::DynamicObject();

    const auto& project = api.project().getCurrentProjectInfo();
    auto* projectObj = new juce::DynamicObject();
    projectObj->setProperty("tempo_bpm", project.tempo);
    projectObj->setProperty("time_signature", juce::String(project.timeSignatureNumerator) + "/" +
                                                  juce::String(project.timeSignatureDenominator));
    projectObj->setProperty("beats_per_bar", project.timeSignatureNumerator);
    root->setProperty("project", juce::var(projectObj));

    // Tracks — lightweight: just id, name, type
    juce::Array<juce::var> tracksArray;
    int index = 1;
    for (const auto& track : tm.getTracks()) {
        auto* trackObj = new juce::DynamicObject();
        trackObj->setProperty("id", index);
        trackObj->setProperty("name", track.name);
        trackObj->setProperty("type", juce::String(getTrackTypeName(track.type)));

        juce::Array<juce::var> racksArray;
        for (const auto& element : track.chain.fxChainElements) {
            if (!isRack(element))
                continue;
            const auto& rack = getRack(element);
            auto* rackObj = new juce::DynamicObject();
            rackObj->setProperty("id", rack.id);
            rackObj->setProperty("name", rack.name);
            rackObj->setProperty("bypassed", rack.bypassed);
            rackObj->setProperty("volume_db", rack.volume);
            juce::Array<juce::var> chainsArray;
            for (const auto& chain : rack.chains) {
                auto* chainObj = new juce::DynamicObject();
                chainObj->setProperty("id", chain.id);
                chainObj->setProperty("name", chain.name);
                chainObj->setProperty("muted", chain.muted);
                chainObj->setProperty("solo", chain.solo);
                chainObj->setProperty("bypassed", chain.bypassed);
                chainObj->setProperty("output", chain.outputIndex);
                chainsArray.add(juce::var(chainObj));
            }
            rackObj->setProperty("chains", chainsArray);
            racksArray.add(juce::var(rackObj));
        }
        trackObj->setProperty("racks", racksArray);
        tracksArray.add(juce::var(trackObj));
        index++;
    }
    root->setProperty("tracks", tracksArray);
    root->setProperty("track_count", tm.getNumTracks());

    // Selection context — only exposed to the LLM when the UI's context
    // toggle is enabled. When disabled, the snapshot still lists the tracks
    // but omits the selection so the LLM has no bias signal.
    if (!g_contextEnabled.load(std::memory_order_relaxed))
        return juce::JSON::toString(juce::var(root), true);

    auto& sm = api.selection();
    auto selTrack = sm.getSelectedTrack();
    const auto selectedChainNode = sm.getSelectedChainNode();
    if (selTrack == INVALID_TRACK_ID && selectedChainNode.isValid())
        selTrack = selectedChainNode.trackId;
    if (selTrack == MASTER_TRACK_ID) {
        // Master selected = operate on every track. Emit a scope marker so the
        // LLM emits implicit-target ops (MUTE/SET/FX with no ref) that the
        // executor fans out across all tracks, instead of a per-track id.
        // (The master is not in getTracks(), so the index loop below would
        // otherwise fall through and write a bogus selected_track_id.)
        root->setProperty("scope", "all_tracks");
    } else if (selTrack != INVALID_TRACK_ID) {
        // Find 1-based index for the selected track
        int selIndex = 1;
        bool found = false;
        for (const auto& track : tm.getTracks()) {
            if (track.id == selTrack) {
                found = true;
                break;
            }
            selIndex++;
        }
        if (found) {
            root->setProperty("selected_track_id", selIndex);
            if (const auto* selectedTrack = tm.getTrack(selTrack)) {
                auto* selectedTrackObj = new juce::DynamicObject();
                selectedTrackObj->setProperty("id", selIndex);
                selectedTrackObj->setProperty("model_id", selectedTrack->id);
                selectedTrackObj->setProperty("name", selectedTrack->name.substring(0, 128));
                selectedTrackObj->setProperty("type",
                                              juce::String(getTrackTypeName(selectedTrack->type)));

                const auto devices = collectTrackDevices(*selectedTrack);
                juce::Array<juce::var> deviceArray;
                for (const auto& item : devices) {
                    deviceArray.add(deviceSummary(*item.device, item.path, false));
                    if (deviceArray.size() >= kMaxSelectedTrackDevices)
                        break;
                }
                selectedTrackObj->setProperty("devices", deviceArray);
                selectedTrackObj->setProperty("devices_truncated",
                                              static_cast<int>(devices.size()) >
                                                  kMaxSelectedTrackDevices);
                root->setProperty("selected_track", juce::var(selectedTrackObj));

                if (selectedChainNode.getType() == ChainNodeType::Device ||
                    selectedChainNode.getType() == ChainNodeType::TopLevelDevice) {
                    for (const auto& item : devices) {
                        if (item.path == selectedChainNode) {
                            root->setProperty("selected_device",
                                              deviceSummary(*item.device, item.path, true));
                            break;
                        }
                    }
                }
            }
        }
    }

    // Selected clip context
    auto& cm = api.clips();
    auto selClip = sm.getSelectedClip();
    if (selClip != INVALID_CLIP_ID) {
        auto* clip = cm.getClip(selClip);
        if (clip) {
            auto clipIds = cm.getClipsOnTrack(clip->trackId);
            int clipIdx = 1;
            for (auto cid : clipIds) {
                if (cid == selClip)
                    break;
                clipIdx++;
            }
            root->setProperty("selected_clip_index", clipIdx);
            int ownerIdx = 1;
            for (const auto& t : tm.getTracks()) {
                if (t.id == clip->trackId)
                    break;
                ownerIdx++;
            }
            root->setProperty("selected_clip_track_id", ownerIdx);
        }
    }

    return juce::JSON::toString(juce::var(root), true);
}

// ============================================================================
// Note Name Parsing
// ============================================================================

int Interpreter::parseNoteName(const std::string& name) {
    return music::parseNoteName(name);
}

// ============================================================================
// Helper: Get Selected Clip
// ============================================================================

ClipId Interpreter::getSelectedClipId() const {
    // Prefer clip set during current DSL execution (e.g. by clip.new or clips.select)
    if (ctx_.currentClipId >= 0)
        return ctx_.currentClipId;

    // Fall back to UI selection
    auto& sm = api_.selection();
    auto clipId = sm.getSelectedClip();
    if (clipId != INVALID_CLIP_ID)
        return clipId;

    // Also check multi-selection — if exactly one clip selected, use it
    const auto& selected = sm.getSelectedClips();
    if (selected.size() == 1)
        return *selected.begin();

    // Fall back to the first clip on the current track
    if (ctx_.currentTrackId >= 0) {
        auto& cm = api_.clips();
        auto clips = cm.getClipsOnTrack(ctx_.currentTrackId);
        if (!clips.empty())
            return clips.front();
    }

    return INVALID_CLIP_ID;
}

// ============================================================================
// Helper: Ensure Note Selection
// ============================================================================

bool Interpreter::ensureNoteSelection() {
    auto& sm = api_.selection();
    if (sm.hasNoteSelection())
        return true;

    // No notes selected — try to auto-select all notes in the current clip
    auto clipId = getSelectedClipId();
    if (clipId == INVALID_CLIP_ID)
        return false;

    auto& cm = api_.clips();
    auto* clip = cm.getClip(clipId);
    if (!clip || clip->midiNotes.empty())
        return false;

    std::vector<size_t> allIndices;
    allIndices.reserve(clip->midiNotes.size());
    for (size_t i = 0; i < clip->midiNotes.size(); i++)
        allIndices.push_back(i);

    sm.selectNotes(clipId, allIndices);
    return sm.hasNoteSelection();
}

// ============================================================================
// Note Operation Methods
// ============================================================================

bool Interpreter::executeSelectNotes(Tokenizer& tok) {
    // Parse: (note.field op value)
    if (!tok.expect(TokenType::LPAREN)) {
        ctx_.setError("Expected '(' after 'notes.select'");
        return false;
    }

    if (!tok.expect("note")) {
        ctx_.setError("Expected 'note' in notes.select condition");
        return false;
    }
    if (!tok.expect(TokenType::DOT)) {
        ctx_.setError("Expected '.' after 'note'");
        return false;
    }

    Token field = tok.next();
    if (field.type != TokenType::IDENTIFIER) {
        ctx_.setError("Expected field name after 'note.'");
        return false;
    }

    Token op = tok.next();
    if (op.type != TokenType::EQUALS_EQUALS && op.type != TokenType::NOT_EQUALS &&
        op.type != TokenType::GREATER && op.type != TokenType::GREATER_EQUALS &&
        op.type != TokenType::LESS && op.type != TokenType::LESS_EQUALS) {
        ctx_.setError("Expected comparison operator in notes.select condition");
        return false;
    }

    Token value = tok.next();
    if (value.type != TokenType::NUMBER && value.type != TokenType::IDENTIFIER &&
        value.type != TokenType::STRING) {
        ctx_.setError("Expected value in notes.select condition");
        return false;
    }

    if (!tok.expect(TokenType::RPAREN)) {
        ctx_.setError("Expected ')' after notes.select condition");
        return false;
    }

    // Get the selected clip
    auto clipId = getSelectedClipId();
    if (clipId == INVALID_CLIP_ID) {
        ctx_.setError("No clip selected for notes.select");
        return false;
    }

    auto& cm = api_.clips();
    auto* clip = cm.getClip(clipId);
    if (!clip) {
        ctx_.setError("Selected clip not found");
        return false;
    }

    // Resolve value — for pitch field, try note name parsing
    double numValue = 0.0;
    if (field.value == "pitch") {
        int midiNote = parseNoteName(value.value);
        if (midiNote < 0) {
            ctx_.setError("Invalid note name or MIDI number: " + juce::String(value.value));
            return false;
        }
        numValue = static_cast<double>(midiNote);
    } else {
        numValue = std::atof(value.value.c_str());
    }

    // Comparator
    auto compare = [&](double noteVal) -> bool {
        if (op.type == TokenType::EQUALS_EQUALS)
            return std::abs(noteVal - numValue) < 0.001;
        if (op.type == TokenType::NOT_EQUALS)
            return std::abs(noteVal - numValue) >= 0.001;
        if (op.type == TokenType::GREATER)
            return noteVal > numValue;
        if (op.type == TokenType::GREATER_EQUALS)
            return noteVal >= numValue;
        if (op.type == TokenType::LESS)
            return noteVal < numValue;
        if (op.type == TokenType::LESS_EQUALS)
            return noteVal <= numValue;
        return false;
    };

    // Match notes
    std::vector<size_t> matched;
    for (size_t i = 0; i < clip->midiNotes.size(); i++) {
        const auto& note = clip->midiNotes[i];
        double noteVal = 0.0;

        if (field.value == "pitch")
            noteVal = static_cast<double>(note.noteNumber);
        else if (field.value == "velocity")
            noteVal = static_cast<double>(note.velocity);
        else if (field.value == "start_beat")
            noteVal = note.startBeat;
        else if (field.value == "length_beats")
            noteVal = note.lengthBeats;
        else {
            ctx_.setError("Unknown note field: " + juce::String(field.value));
            return false;
        }

        if (compare(noteVal))
            matched.push_back(i);
    }

    auto& sm = api_.selection();
    if (matched.empty()) {
        sm.clearNoteSelection();
        ctx_.addResult("No notes matched the criteria");
    } else {
        sm.selectNotes(clipId, matched);
        ctx_.addResult("Selected " + juce::String(static_cast<int>(matched.size())) + " note(s)");
    }

    return true;
}

bool Interpreter::executeAddNote(const Params& params) {
    auto clipId = getSelectedClipId();
    if (clipId == INVALID_CLIP_ID) {
        ctx_.setError("No clip selected for notes.add");
        return false;
    }

    // Parse pitch (required)
    if (!params.has("pitch")) {
        ctx_.setError("notes.add requires 'pitch' parameter");
        return false;
    }

    int noteNumber = parseNoteName(params.get("pitch"));
    if (noteNumber < 0 || noteNumber > 127) {
        ctx_.setError("Invalid pitch: " + juce::String(params.get("pitch")));
        return false;
    }

    double beat = params.getFloat("beat", 0.0);
    double length = params.getFloat("length", 1.0);
    int velocity = params.getInt("velocity", 100);

    api_.undo().executeCommand(
        std::make_unique<AddMidiNoteCommand>(clipId, beat, noteNumber, length, velocity));

    ctx_.addResult("Added note (pitch=" + juce::String(noteNumber) +
                   ", beat=" + juce::String(beat, 2) + ", length=" + juce::String(length, 2) +
                   ", velocity=" + juce::String(velocity) + ")");
    return true;
}

bool Interpreter::resolveChordNotes(const Params& params, std::vector<int>& outNotes) {
    if (!params.has("root")) {
        ctx_.setError("notes.add_chord requires 'root' parameter");
        return false;
    }
    if (!params.has("quality")) {
        ctx_.setError("notes.add_chord requires 'quality' parameter");
        return false;
    }

    juce::String error;
    if (!music::resolveChordNotes(params.get("root"), params.get("quality"),
                                  params.getInt("inversion", 0), outNotes, error)) {
        ctx_.setError(error);
        return false;
    }
    return true;
}

bool Interpreter::executeAddChord(const Params& params) {
    auto clipId = getSelectedClipId();
    if (clipId == INVALID_CLIP_ID) {
        ctx_.setError("No clip selected for notes.add_chord");
        return false;
    }

    std::vector<int> midiNotes;
    if (!resolveChordNotes(params, midiNotes))
        return false;

    double beat = params.getFloat("beat", 0.0);
    double length = params.getFloat("length", 1.0);
    int velocity = params.getInt("velocity", 100);
    std::string quality = params.get("quality");

    // Build MidiNote objects
    std::vector<MidiNote> notes;
    juce::StringArray noteNames;
    for (int n : midiNotes) {
        MidiNote mn;
        mn.noteNumber = n;
        mn.startBeat = beat;
        mn.lengthBeats = length;
        mn.velocity = velocity;
        notes.push_back(mn);
        noteNames.add(juce::MidiMessage::getMidiNoteName(n, true, true, 4));
    }

    api_.undo().executeCommand(std::make_unique<AddMultipleMidiNotesCommand>(
        clipId, std::move(notes),
        "Add " + juce::String(quality) + " chord at beat " + juce::String(beat, 2)));

    ctx_.addResult("Added " + juce::String(quality) + " chord [" + noteNames.joinIntoString(", ") +
                   "] at beat " + juce::String(beat, 2));
    return true;
}

bool Interpreter::executeAddArpeggio(const Params& params) {
    auto clipId = getSelectedClipId();
    if (clipId == INVALID_CLIP_ID) {
        ctx_.setError("No clip selected for notes.add_arpeggio");
        return false;
    }

    std::vector<int> midiNotes;
    if (!resolveChordNotes(params, midiNotes))
        return false;

    double beat = params.getFloat("beat", 0.0);
    double step = params.getFloat("step", 0.5);
    double noteLength = params.has("note_length") ? params.getFloat("note_length") : step;
    int velocity = params.getInt("velocity", 100);
    std::string pattern = params.get("pattern");
    if (pattern.empty())
        pattern = "up";
    std::string quality = params.get("quality");

    // Validate timing parameters to avoid infinite loops
    if (step <= 0.0) {
        ctx_.setError("notes.add_arpeggio: step must be > 0");
        return false;
    }
    if (noteLength <= 0.0) {
        ctx_.setError("notes.add_arpeggio: note_length must be > 0");
        return false;
    }
    if (params.has("beats") && params.getFloat("beats") <= 0.0) {
        ctx_.setError("notes.add_arpeggio: beats must be > 0");
        return false;
    }

    // Sort pitches ascending for pattern application
    std::sort(midiNotes.begin(), midiNotes.end());

    // Apply pattern ordering
    std::vector<int> ordered;
    if (pattern == "down") {
        ordered.assign(midiNotes.rbegin(), midiNotes.rend());
    } else if (pattern == "updown") {
        ordered = midiNotes;
        // Add descending without repeating top and bottom
        for (int i = static_cast<int>(midiNotes.size()) - 2; i > 0; --i)
            ordered.push_back(midiNotes[static_cast<size_t>(i)]);
    } else {
        // "up" (default)
        ordered = midiNotes;
    }

    // Determine fill boundary
    // beats=N fills exactly N beats; fill=true fills the entire clip
    bool fill = params.has("beats") || params.getBool("fill", false);
    double fillBeats = 0.0;
    if (params.has("beats")) {
        fillBeats = beat + params.getFloat("beats");
    } else if (fill) {
        auto* clip = api_.clips().getClip(clipId);
        if (clip) {
            double bpm = api_.project().getCurrentProjectInfo().tempo;
            if (!isValidBpm(bpm))
                bpm = 120.0;
            fillBeats = clip->length * bpm / 60.0;
        }
    }

    // Build MidiNote objects with sequential beat offsets
    std::vector<MidiNote> notes;
    juce::StringArray noteNames;
    double currentBeat = beat;
    size_t idx = 0;
    size_t count = fill ? std::numeric_limits<size_t>::max() : ordered.size();
    while (idx < count) {
        if (fill && currentBeat >= fillBeats)
            break;
        int n = ordered[idx % ordered.size()];
        MidiNote mn;
        mn.noteNumber = n;
        mn.startBeat = currentBeat;
        mn.lengthBeats = noteLength;
        mn.velocity = velocity;
        notes.push_back(mn);
        noteNames.add(juce::MidiMessage::getMidiNoteName(n, true, true, 4));
        currentBeat += step;
        idx++;
    }

    api_.undo().executeCommand(std::make_unique<AddMultipleMidiNotesCommand>(
        clipId, std::move(notes),
        "Add " + juce::String(quality) + " arpeggio at beat " + juce::String(beat, 2)));

    ctx_.addResult("Added " + juce::String(quality) + " arpeggio (" + juce::String(pattern) +
                   ") [" + noteNames.joinIntoString(", ") + "] at beat " + juce::String(beat, 2));
    return true;
}

bool Interpreter::executeDeleteNotes() {
    if (!ensureNoteSelection()) {
        ctx_.setError("No notes selected for notes.delete");
        return false;
    }

    auto& sm = api_.selection();
    auto noteSelClipId = sm.getNoteSelectionClipId();
    const auto& noteSelIndices = sm.getNoteSelectionIndices();

    api_.undo().executeCommand(
        std::make_unique<DeleteMultipleMidiNotesCommand>(noteSelClipId, noteSelIndices));

    int count = static_cast<int>(noteSelIndices.size());
    sm.clearNoteSelection();
    ctx_.addResult("Deleted " + juce::String(count) + " note(s)");
    return true;
}

bool Interpreter::executeTranspose(const Params& params) {
    int semitones = params.getInt("semitones", 0);
    if (semitones == 0) {
        ctx_.addResult("Transpose by 0 semitones (no change)");
        return true;
    }

    if (!ensureNoteSelection()) {
        ctx_.setError("No notes selected for notes.transpose");
        return false;
    }

    auto& sm = api_.selection();
    auto noteSelClipId = sm.getNoteSelectionClipId();
    const auto& noteSelIndices = sm.getNoteSelectionIndices();

    auto& cm = api_.clips();
    auto* clip = cm.getClip(noteSelClipId);
    if (!clip) {
        ctx_.setError("Clip not found for notes.transpose");
        return false;
    }

    std::vector<MoveMultipleMidiNotesCommand::NoteMove> moves;
    for (auto idx : noteSelIndices) {
        if (idx < clip->midiNotes.size()) {
            const auto& note = clip->midiNotes[idx];
            int newNote = juce::jlimit(0, 127, note.noteNumber + semitones);
            moves.push_back({idx, note.startBeat, newNote});
        }
    }

    if (!moves.empty()) {
        api_.undo().executeCommand(
            std::make_unique<MoveMultipleMidiNotesCommand>(noteSelClipId, std::move(moves)));
    }

    ctx_.addResult("Transposed " + juce::String(static_cast<int>(noteSelIndices.size())) +
                   " note(s) by " + juce::String(semitones) + " semitones");
    return true;
}

bool Interpreter::executeSetPitch(const Params& params) {
    if (!params.has("pitch")) {
        ctx_.setError("notes.set_pitch requires 'pitch' parameter");
        return false;
    }

    int targetPitch = parseNoteName(params.get("pitch"));
    if (targetPitch < 0 || targetPitch > 127) {
        ctx_.setError("Invalid pitch: " + juce::String(params.get("pitch")));
        return false;
    }

    if (!ensureNoteSelection()) {
        ctx_.setError("No notes selected for notes.set_pitch");
        return false;
    }

    auto& sm = api_.selection();
    auto noteSelClipId = sm.getNoteSelectionClipId();
    const auto& noteSelIndices = sm.getNoteSelectionIndices();

    auto& cm = api_.clips();
    auto* clip = cm.getClip(noteSelClipId);
    if (!clip) {
        ctx_.setError("Clip not found for notes.set_pitch");
        return false;
    }

    std::vector<MoveMultipleMidiNotesCommand::NoteMove> moves;
    for (auto idx : noteSelIndices) {
        if (idx < clip->midiNotes.size()) {
            const auto& note = clip->midiNotes[idx];
            moves.push_back({idx, note.startBeat, targetPitch});
        }
    }

    if (!moves.empty()) {
        api_.undo().executeCommand(
            std::make_unique<MoveMultipleMidiNotesCommand>(noteSelClipId, std::move(moves)));
    }

    ctx_.addResult("Set pitch to " + juce::String(params.get("pitch")) + " on " +
                   juce::String(static_cast<int>(noteSelIndices.size())) + " note(s)");
    return true;
}

bool Interpreter::executeSetVelocity(const Params& params) {
    int velocity = params.getInt("value", 100);
    velocity = juce::jlimit(0, 127, velocity);

    if (!ensureNoteSelection()) {
        ctx_.setError("No notes selected for notes.set_velocity");
        return false;
    }

    auto& sm = api_.selection();
    auto noteSelClipId = sm.getNoteSelectionClipId();
    const auto& noteSelIndices = sm.getNoteSelectionIndices();

    std::vector<std::pair<size_t, int>> noteVelocities;
    for (auto idx : noteSelIndices)
        noteVelocities.emplace_back(idx, velocity);

    api_.undo().executeCommand(std::make_unique<SetMultipleNoteVelocitiesCommand>(
        noteSelClipId, std::move(noteVelocities)));

    ctx_.addResult("Set velocity to " + juce::String(velocity) + " on " +
                   juce::String(static_cast<int>(noteSelIndices.size())) + " note(s)");
    return true;
}

bool Interpreter::executeQuantize(const Params& params) {
    double grid = params.getFloat("grid", 0.25);

    if (!ensureNoteSelection()) {
        ctx_.setError("No notes selected for notes.quantize");
        return false;
    }

    auto& sm = api_.selection();
    auto noteSelClipId = sm.getNoteSelectionClipId();
    const auto& noteSelIndices = sm.getNoteSelectionIndices();

    api_.undo().executeCommand(std::make_unique<QuantizeMidiNotesCommand>(
        noteSelClipId, noteSelIndices, grid, QuantizeMode::StartOnly));

    juce::String gridName;
    if (std::abs(grid - 0.25) < 0.001)
        gridName = "16th notes";
    else if (std::abs(grid - 0.5) < 0.001)
        gridName = "8th notes";
    else if (std::abs(grid - 1.0) < 0.001)
        gridName = "quarter notes";
    else
        gridName = juce::String(grid, 2) + " beats";

    ctx_.addResult("Quantized " + juce::String(static_cast<int>(noteSelIndices.size())) +
                   " note(s) to " + gridName);
    return true;
}

bool Interpreter::executeResizeNotes(const Params& params) {
    double length = params.getFloat("length", 1.0);
    if (length <= 0.0) {
        ctx_.setError("Note length must be positive");
        return false;
    }

    if (!ensureNoteSelection()) {
        ctx_.setError("No notes selected for notes.resize");
        return false;
    }

    auto& sm = api_.selection();
    auto noteSelClipId = sm.getNoteSelectionClipId();
    const auto& noteSelIndices = sm.getNoteSelectionIndices();

    std::vector<std::pair<size_t, double>> noteLengths;
    for (auto idx : noteSelIndices)
        noteLengths.emplace_back(idx, length);

    api_.undo().executeCommand(
        std::make_unique<ResizeMultipleMidiNotesCommand>(noteSelClipId, std::move(noteLengths)));

    ctx_.addResult("Resized " + juce::String(static_cast<int>(noteSelIndices.size())) +
                   " note(s) to " + juce::String(length, 2) + " beats");
    return true;
}

// ============================================================================
// Groove Commands
// ============================================================================

bool Interpreter::parseGrooveStatement(Tokenizer& tok) {
    tok.next();  // consume 'groove'

    if (!tok.peek().is(TokenType::DOT)) {
        ctx_.setError("Expected '.' after 'groove'");
        return false;
    }
    tok.next();  // consume '.'

    Token method = tok.next();
    if (method.type != TokenType::IDENTIFIER) {
        ctx_.setError("Expected method name after 'groove.'");
        return false;
    }

    // groove.list() — no params
    if (method.value == "list") {
        if (!tok.expect(TokenType::LPAREN) || !tok.expect(TokenType::RPAREN)) {
            ctx_.setError("Expected '()' after 'groove.list'");
            return false;
        }
        return executeGrooveList();
    }

    if (!tok.expect(TokenType::LPAREN)) {
        ctx_.setError("Expected '(' after 'groove." + juce::String(method.value) + "'");
        return false;
    }

    Params params;
    if (!parseParams(tok, params))
        return false;

    if (!tok.expect(TokenType::RPAREN)) {
        ctx_.setError("Expected ')' after groove parameters");
        return false;
    }

    if (method.value == "new")
        return executeGrooveNew(params);
    else if (method.value == "extract")
        return executeGrooveExtract(params);
    else if (method.value == "set")
        return executeGrooveSet(params);
    else {
        ctx_.setError("Unknown groove method: " + juce::String(method.value));
        return false;
    }
}

// groove.new(name="My Swing", notesPerBeat=4, shifts="0.0,0.15,-0.05,0.4", parameterized=true)
bool Interpreter::executeGrooveNew(const Params& params) {
    auto name = params.get("name");
    if (name.empty()) {
        ctx_.setError("groove.new requires 'name' parameter");
        return false;
    }

    auto shiftsStr = params.get("shifts");
    if (shiftsStr.empty()) {
        ctx_.setError("groove.new requires 'shifts' parameter (comma-separated lateness values)");
        return false;
    }

    int notesPerBeat = params.getInt("notesPerBeat", 2);
    bool parameterized = params.getBool("parameterized", true);

    // Parse shifts string: "0.0,0.15,-0.05,0.4"
    juce::StringArray shiftTokens;
    shiftTokens.addTokens(juce::String(shiftsStr), ",", "");

    if (shiftTokens.isEmpty()) {
        ctx_.setError("groove.new: 'shifts' must contain at least one value");
        return false;
    }

    const juce::String grooveName(name);
    std::vector<float> lateness;
    lateness.reserve(static_cast<size_t>(shiftTokens.size()));
    for (const auto& token : shiftTokens)
        lateness.push_back(token.trim().getFloatValue());

    const bool existed = api_.grooves().getTemplateNames().contains(grooveName);
    if (!api_.grooves().upsertTemplate(grooveName, notesPerBeat, parameterized, lateness)) {
        ctx_.setError("Failed to save groove template");
        return false;
    }

    if (existed) {
        ctx_.addResult("Updated groove template: " + juce::String(name));
    } else {
        ctx_.addResult("Created groove template: " + juce::String(name) + " (" +
                       juce::String(shiftTokens.size()) + " steps, " + juce::String(notesPerBeat) +
                       " per beat)");
    }

    // Refresh clip inspector so new template appears in dropdown immediately
    auto clipId = getSelectedClipId();
    if (clipId != INVALID_CLIP_ID) {
        auto* clip = api_.clips().getClip(clipId);
        if (clip && clip->isMidi())
            api_.clips().setGrooveTemplate(clipId, clip->grooveTemplate);
    }

    return true;
}

// groove.extract(clip=0, resolution=16, name="Extracted Groove")
bool Interpreter::executeGrooveExtract(const Params& params) {
    auto name = params.get("name", "Extracted Groove");
    int resolution = params.getInt("resolution", 16);  // 8 or 16

    // Get clip — use param or current selection
    ClipId clipId;
    if (params.has("clip")) {
        int clipIndex = params.getInt("clip", 0);
        // Find clip by index on current track
        if (ctx_.currentTrackId < 0) {
            ctx_.setError(
                "groove.extract: no track in context. Use track(...).groove.extract(...)");
            return false;
        }
        auto& cm = api_.clips();
        auto trackClips = cm.getClipsOnTrack(ctx_.currentTrackId);
        if (clipIndex < 0 || clipIndex >= static_cast<int>(trackClips.size())) {
            ctx_.setError("groove.extract: clip index " + juce::String(clipIndex) +
                          " out of range");
            return false;
        }
        clipId = trackClips[static_cast<size_t>(clipIndex)];
    } else {
        clipId = getSelectedClipId();
    }

    if (clipId == INVALID_CLIP_ID) {
        ctx_.setError("groove.extract: no clip selected or specified");
        return false;
    }

    const auto* clip = api_.clips().getClip(clipId);
    if (!clip || !clip->isAudio()) {
        ctx_.setError("groove.extract: clip must be an audio clip");
        return false;
    }

    // Get cached transients via the api
    const auto* transients =
        api_.clips().getCachedTransients(audioEventRef(*clip).sourceFilePath());
    if (!transients || transients->isEmpty()) {
        ctx_.setError("groove.extract: no transients detected for this clip. "
                      "Enable warp or detect transients first.");
        return false;
    }

    // Determine clip parameters
    const auto& event = audioEventRef(*clip);
    double clipBPM = event.interpBpm > 0.0 ? event.interpBpm : 120.0;
    double beatDuration = 60.0 / clipBPM;         // seconds per beat
    double clipStartSec = event.anchorSeconds();  // source read position

    // notesPerBeat: 2 for 8th notes, 4 for 16th notes
    int notesPerBeat = (resolution >= 16) ? 4 : 2;
    double gridStepSec = beatDuration / notesPerBeat;

    // Compute how many grid steps fit in the clip
    double clipLengthSec = clip->lengthBeats * beatDuration;
    int numSteps = static_cast<int>(std::round(clipLengthSec / gridStepSec));
    if (numSteps < 2) {
        ctx_.setError("groove.extract: clip too short for groove extraction");
        return false;
    }

    // For each transient, find the nearest grid position and compute delta
    std::vector<float> latenesses(static_cast<size_t>(numSteps), 0.0f);
    std::vector<bool> hasTransient(static_cast<size_t>(numSteps), false);

    for (int i = 0; i < transients->size(); ++i) {
        double transientSec = (*transients)[i] - clipStartSec;
        if (transientSec < 0.0 || transientSec >= clipLengthSec)
            continue;

        // Find nearest grid position
        double gridIndex = transientSec / gridStepSec;
        int nearestGrid = static_cast<int>(std::round(gridIndex));
        if (nearestGrid < 0 || nearestGrid >= numSteps)
            continue;

        // Delta = how far the transient is from the grid, as proportion of grid step
        double delta = (transientSec - nearestGrid * gridStepSec) / gridStepSec;
        latenesses[static_cast<size_t>(nearestGrid)] = static_cast<float>(delta);
        hasTransient[static_cast<size_t>(nearestGrid)] = true;
    }

    // Determine repeating pattern length (try to find the smallest cycle)
    // Default: use all steps, but try 1 bar (notesPerBeat * beatsPerBar)
    int beatsPerBar = api_.project().getCurrentProjectInfo().timeSignatureNumerator;
    if (beatsPerBar <= 0)
        beatsPerBar = 4;
    int stepsPerBar = notesPerBeat * beatsPerBar;
    int patternLength = (numSteps >= stepsPerBar) ? stepsPerBar : numSteps;

    const std::vector<float> grooveLateness(latenesses.begin(), latenesses.begin() + patternLength);
    if (!api_.grooves().upsertTemplate(juce::String(name), notesPerBeat, true, grooveLateness)) {
        ctx_.setError("Failed to save extracted groove");
        return false;
    }

    // Build result summary
    int transientCount = 0;
    for (size_t i = 0; i < static_cast<size_t>(patternLength); ++i)
        if (hasTransient[i])
            transientCount++;

    ctx_.addResult("Extracted groove '" + juce::String(name) + "' from " +
                   juce::String(transientCount) + " transients (" + juce::String(patternLength) +
                   " steps, " + juce::String(notesPerBeat) + " per beat)");

    // Refresh clip inspector so new template appears in dropdown immediately
    auto selClipId = getSelectedClipId();
    if (selClipId != INVALID_CLIP_ID) {
        auto* selClip = api_.clips().getClip(selClipId);
        if (selClip && selClip->isMidi())
            api_.clips().setGrooveTemplate(selClipId, selClip->grooveTemplate);
    }
    return true;
}

// groove.set(template="Basic 8th Swing", strength=0.8)
// Applies to current clip context, or clip=0 param
bool Interpreter::executeGrooveSet(const Params& params) {
    auto templateName = params.get("template");
    // strength is optional — only set if provided

    ClipId clipId = getSelectedClipId();
    if (clipId == INVALID_CLIP_ID) {
        ctx_.setError("groove.set: no clip in context");
        return false;
    }

    const auto* clip = api_.clips().getClip(clipId);
    if (!clip || !clip->isMidi()) {
        ctx_.setError("groove.set: clip must be a MIDI clip");
        return false;
    }

    if (params.has("template")) {
        api_.undo().executeCommand(
            std::make_unique<SetClipGrooveTemplateCommand>(clipId, juce::String(templateName)));
    }

    if (params.has("strength")) {
        float strength = static_cast<float>(params.getFloat("strength", 0.5));
        api_.undo().executeCommand(
            std::make_unique<SetClipGrooveStrengthCommand>(clipId, strength));
    }

    ctx_.addResult(
        "Set groove on clip: template=\"" + juce::String(templateName) +
        "\", strength=" + juce::String(params.getFloat("strength", clip->grooveStrength), 2));
    return true;
}

// groove.list() — show all available groove templates
bool Interpreter::executeGrooveList() {
    auto names = api_.grooves().getTemplateNames();

    if (names.isEmpty()) {
        ctx_.addResult("No groove templates available");
    } else {
        ctx_.addResult("Available groove templates (" + juce::String(names.size()) + "):");
        for (const auto& n : names)
            ctx_.addResult("  " + n);
    }
    return true;
}

}  // namespace magda::dsl
