#include "dsl_interpreter.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "../daw/core/ClipManager.hpp"
#include "../daw/core/DeviceInfo.hpp"
#include "../daw/core/MidiNoteCommands.hpp"
#include "../daw/core/SelectionManager.hpp"
#include "../daw/core/TrackManager.hpp"
#include "../daw/core/UndoManager.hpp"
#include "../daw/engine/AudioEngine.hpp"
#include "../daw/engine/TracktionEngineWrapper.hpp"

namespace magda::dsl {

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

    while (*pos_ &&
           (std::isalnum(static_cast<unsigned char>(*pos_)) || *pos_ == '_' || *pos_ == '#')) {
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

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '#')
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

Interpreter::Interpreter() {}

bool Interpreter::execute(const char* dslCode) {
    if (!dslCode || !*dslCode) {
        ctx_.setError("Empty DSL code");
        return false;
    }

    ctx_ = InterpreterContext();

    // Inject selected clip from UI so note operations work without explicit selection
    auto& sm = SelectionManager::getInstance();
    auto selectedClip = sm.getSelectedClip();
    if (selectedClip != INVALID_CLIP_ID)
        ctx_.currentClipId = selectedClip;

    DBG("MAGDA DSL: Executing: " + juce::String(dslCode).substring(0, 200));

    Tokenizer tok(dslCode);

    int succeeded = 0;
    int failed = 0;

    while (tok.hasMore()) {
        auto savedPos = tok.savePosition();

        if (!parseStatement(tok)) {
            // Log the error as a warning and skip to the next statement
            ctx_.addResult("[!] " + ctx_.error);
            ctx_.error.clear();
            ctx_.hasError = false;
            failed++;

            // Advance past the failed statement to the next one.
            // Statements start with 'track' or 'filter' at the beginning of a line.
            // Skip tokens until we find a statement-starting keyword or EOF.
            tok.restorePosition(savedPos);
            tok.next();  // skip past the keyword that started this failed statement
            while (tok.hasMore()) {
                auto next = tok.peek();
                if (next.is("track") || next.is("filter") || next.type == TokenType::END_OF_INPUT)
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
        ctx_.setError("All " + juce::String(failed) + " statement(s) failed");
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
    else if (t.type == TokenType::END_OF_INPUT) {
        return true;
    } else {
        ctx_.setError("Unexpected token '" + juce::String(t.value) + "' at line " +
                      juce::String(t.line));
        return false;
    }
}

bool Interpreter::parseTrackStatement(Tokenizer& tok) {
    tok.next();  // consume 'track'

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

    auto& tm = TrackManager::getInstance();

    if (params.has("id")) {
        // Reference existing track by 1-based index
        int id = params.getInt("id");
        int index = id - 1;
        if (index < 0 || index >= tm.getNumTracks()) {
            ctx_.setError("Track " + juce::String(id) + " not found");
            return false;
        }
        ctx_.currentTrackId = tm.getTracks()[static_cast<size_t>(index)].id;
    } else if (params.has("name")) {
        juce::String name(params.get("name"));
        bool forceNew = params.getBool("new", false);

        int existingId = forceNew ? -1 : findTrackByName(name);

        if (existingId >= 0) {
            ctx_.currentTrackId = existingId;
            DBG("MAGDA DSL: Found existing track '" + name + "'");
        } else {
            auto trackType = parseTrackType(params);
            auto trackId = tm.createTrack(name, trackType);
            ctx_.currentTrackId = trackId;
            ctx_.addResult("Created track '" + name + "'");
        }
    } else {
        // track() with no params — create unnamed track
        auto trackType = parseTrackType(params);
        auto trackId = tm.createTrack("", trackType);
        ctx_.currentTrackId = trackId;
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

    // Execute filter: find matching tracks
    auto& tm = TrackManager::getInstance();
    ctx_.filteredTrackIds.clear();

    if (field.value == "name") {
        for (const auto& track : tm.getTracks()) {
            if (track.name == juce::String(value.value))
                ctx_.filteredTrackIds.push_back(track.id);
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
        else if (methodKey == "delete")
            success = executeDelete();
        else if (methodKey == "clip.delete")
            success = executeDeleteClip(params);
        else if (methodKey == "clip.rename")
            success = executeRenameClip(params);
        else if (methodKey == "fx.add")
            success = executeAddFx(params);
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

    if (t.type == TokenType::STRING || t.type == TokenType::NUMBER ||
        t.type == TokenType::IDENTIFIER) {
        outValue = t.value;
        return true;
    }

    ctx_.setError("Expected value, got '" + juce::String(t.value) + "'");
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
        double bpm = 120.0;
        auto* engine = TrackManager::getInstance().getAudioEngine();
        if (engine)
            bpm = engine->getTempo();
        double secondsPerBar = 4.0 * 60.0 / bpm;

        auto& cm = ClipManager::getInstance();
        for (auto cid : cm.getClipsOnTrack(ctx_.currentTrackId)) {
            auto* clip = cm.getClip(cid);
            if (!clip)
                continue;
            // Convert clip end time to 1-based bar, round up to next full bar
            double clipEndBar = (clip->startTime + clip->length) / secondsPerBar + 1.0;
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

    double startTime = barsToTime(bar);
    double length = barsToTime(bar + lengthBars) - startTime;

    auto& cm = ClipManager::getInstance();
    auto clipId = cm.createMidiClip(ctx_.currentTrackId, startTime, length);

    if (clipId < 0) {
        ctx_.setError("Failed to create clip");
        return false;
    }

    ctx_.currentClipId = clipId;
    ctx_.addResult("Created MIDI clip at bar " + juce::String(bar, 2) + ", length " +
                   juce::String(lengthBars, 2) + " bars");
    return true;
}

bool Interpreter::executeSetTrack(const Params& params) {
    auto& tm = TrackManager::getInstance();

    auto applyToTrack = [&](int trackId) {
        if (params.has("name"))
            tm.setTrackName(trackId, juce::String(params.get("name")));

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

bool Interpreter::executeDelete() {
    auto& tm = TrackManager::getInstance();

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

    auto& cm = ClipManager::getInstance();
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

bool Interpreter::executeRenameClip(const Params& params) {
    if (!params.has("name")) {
        ctx_.setError("clip.rename requires 'name' parameter");
        return false;
    }

    juce::String newName(params.get("name"));
    auto& cm = ClipManager::getInstance();

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
        auto& sm = SelectionManager::getInstance();
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

    juce::String fxName(params.get("name"));
    juce::String formatHint(params.get("format"));

    // --- Internal plugin lookup (case-insensitive alias map) ---
    static const std::map<juce::String, juce::String> internalAliases = {
        {"eq", "eq"},
        {"equaliser", "eq"},
        {"equalizer", "eq"},
        {"compressor", "compressor"},
        {"reverb", "reverb"},
        {"delay", "delay"},
        {"chorus", "chorus"},
        {"phaser", "phaser"},
        {"filter", "lowpass"},
        {"lowpass", "lowpass"},
        {"utility", "utility"},
        {"pitch shift", "pitchshift"},
        {"pitchshift", "pitchshift"},
        {"ir reverb", "impulseresponse"},
        {"impulse response", "impulseresponse"},
    };

    auto lowerName = fxName.toLowerCase();
    auto aliasIt = internalAliases.find(lowerName);
    if (aliasIt != internalAliases.end()) {
        DeviceInfo device;
        device.name = fxName;
        device.pluginId = aliasIt->second;
        device.format = PluginFormat::Internal;
        device.isInstrument = false;

        auto deviceId = TrackManager::getInstance().addDeviceToTrack(ctx_.currentTrackId, device);
        if (deviceId == INVALID_DEVICE_ID) {
            ctx_.setError("Failed to add internal FX '" + fxName + "' to track");
            return false;
        }
        ctx_.addResult("Added internal FX '" + fxName + "'");
        return true;
    }

    // --- External plugin lookup via KnownPluginList ---
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (!engine) {
        ctx_.setError("Audio engine not available");
        return false;
    }

    auto* teWrapper = dynamic_cast<TracktionEngineWrapper*>(engine);
    if (!teWrapper) {
        ctx_.setError("Engine does not support plugin scanning");
        return false;
    }

    const auto& knownPlugins = teWrapper->getKnownPluginList();
    const juce::PluginDescription* bestMatch = nullptr;

    for (const auto& desc : knownPlugins.getTypes()) {
        if (desc.name.equalsIgnoreCase(fxName)) {
            // Filter by format hint if provided
            if (formatHint.isNotEmpty()) {
                auto descFormat = desc.pluginFormatName.toLowerCase();
                if (!descFormat.contains(formatHint.toLowerCase()))
                    continue;
            }
            bestMatch = &desc;
            break;
        }
    }

    if (!bestMatch) {
        ctx_.setError("FX plugin '" + fxName + "' not found (not internal or in scanned plugins)");
        return false;
    }

    // Copy fields before addDeviceToTrack — loading the plugin can mutate the
    // KnownPluginList, invalidating the bestMatch pointer.
    DeviceInfo device;
    device.name = bestMatch->name;
    device.pluginId = bestMatch->createIdentifierString();
    device.manufacturer = bestMatch->manufacturerName;
    device.uniqueId = bestMatch->createIdentifierString();
    device.fileOrIdentifier = bestMatch->fileOrIdentifier;
    device.isInstrument = bestMatch->isInstrument;

    juce::String matchedFormat = bestMatch->pluginFormatName;
    juce::String matchedName = bestMatch->name;
    juce::String matchedManufacturer = bestMatch->manufacturerName;

    if (matchedFormat == "VST3")
        device.format = PluginFormat::VST3;
    else if (matchedFormat == "AudioUnit" || matchedFormat == "AU")
        device.format = PluginFormat::AU;
    else if (matchedFormat == "VST")
        device.format = PluginFormat::VST;
    else
        device.format = PluginFormat::VST3;

    bestMatch = nullptr;  // no longer safe to dereference

    auto deviceId = TrackManager::getInstance().addDeviceToTrack(ctx_.currentTrackId, device);
    if (deviceId == INVALID_DEVICE_ID) {
        ctx_.setError("Failed to add FX '" + fxName + "' to track");
        return false;
    }

    ctx_.addResult("Added " + matchedFormat + " FX '" + matchedName + "' by " +
                   matchedManufacturer);
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
            SelectionManager::getInstance().selectTrack(ctx_.filteredTrackIds.front());
            ctx_.addResult("Selected track");
        } else {
            ctx_.addResult("No tracks matched filter");
        }
    } else if (ctx_.currentTrackId >= 0) {
        SelectionManager::getInstance().selectTrack(ctx_.currentTrackId);
        ctx_.addResult("Selected track");
    } else {
        ctx_.setError("No track context for select");
        return false;
    }
    return true;
}

bool Interpreter::executeSelectClips(Tokenizer& tok) {
    // Parse: (clip.field op value)
    if (!tok.expect(TokenType::LPAREN)) {
        ctx_.setError("Expected '(' after 'clips.select'");
        return false;
    }

    if (!tok.expect("clip")) {
        ctx_.setError("Expected 'clip' in clips.select condition");
        return false;
    }
    if (!tok.expect(TokenType::DOT)) {
        ctx_.setError("Expected '.' after 'clip'");
        return false;
    }

    Token field = tok.next();
    if (field.type != TokenType::IDENTIFIER) {
        ctx_.setError("Expected field name after 'clip.'");
        return false;
    }

    Token op = tok.next();
    if (op.type != TokenType::EQUALS_EQUALS && op.type != TokenType::NOT_EQUALS &&
        op.type != TokenType::GREATER && op.type != TokenType::GREATER_EQUALS &&
        op.type != TokenType::LESS && op.type != TokenType::LESS_EQUALS) {
        ctx_.setError("Expected comparison operator in clips.select condition");
        return false;
    }

    Token value = tok.next();
    if (value.type != TokenType::NUMBER && value.type != TokenType::STRING) {
        ctx_.setError("Expected value in clips.select condition");
        return false;
    }

    if (!tok.expect(TokenType::RPAREN)) {
        ctx_.setError("Expected ')' after clips.select condition");
        return false;
    }

    double numValue = std::atof(value.value.c_str());

    // Get tempo for bar/time conversions
    double bpm = 120.0;
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (engine)
        bpm = engine->getTempo();
    double secondsPerBar = 4.0 * 60.0 / bpm;

    // Comparator lambda
    auto compare = [&](double clipVal) -> bool {
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

    auto& cm = ClipManager::getInstance();

    auto matchOnTrack = [&](int trackId) {
        auto clipIds = cm.getClipsOnTrack(trackId);
        std::unordered_set<ClipId> matched;

        for (auto clipId : clipIds) {
            auto* clip = cm.getClip(clipId);
            if (!clip)
                continue;

            double clipVal = 0.0;
            if (field.value == "length_bars")
                clipVal = clip->length / secondsPerBar;
            else if (field.value == "start_bar")
                clipVal = (clip->startTime / secondsPerBar) + 1.0;
            else if (field.value == "length")
                clipVal = clip->length;
            else if (field.value == "start")
                clipVal = clip->startTime;
            else {
                ctx_.setError("Unknown clip field: " + juce::String(field.value));
                return matched;  // empty
            }

            if (compare(clipVal))
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
        auto& tm = TrackManager::getInstance();
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
        SelectionManager::getInstance().selectClip(*allMatched.begin());
        ctx_.addResult("Selected 1 clip");
    } else {
        SelectionManager::getInstance().selectClips(allMatched);
        ctx_.addResult("Selected " + juce::String(static_cast<int>(allMatched.size())) + " clips");
    }

    return true;
}

TrackType Interpreter::parseTrackType(const Params& params) {
    if (!params.has("type"))
        return TrackType::Audio;

    std::string typeStr = params.get("type");
    if (typeStr == "midi")
        return TrackType::MIDI;
    if (typeStr == "instrument")
        return TrackType::Instrument;
    if (typeStr == "group")
        return TrackType::Group;
    if (typeStr == "aux")
        return TrackType::Aux;
    return TrackType::Audio;
}

int Interpreter::findTrackByName(const juce::String& name) const {
    auto& tm = TrackManager::getInstance();
    for (const auto& track : tm.getTracks()) {
        if (track.name.equalsIgnoreCase(name))
            return track.id;
    }
    return -1;
}

double Interpreter::barsToTime(double bar) const {
    // Convert 1-based bar number to seconds
    double bpm = 120.0;  // fallback
    auto* engine = TrackManager::getInstance().getAudioEngine();
    if (engine)
        bpm = engine->getTempo();

    constexpr double beatsPerBar = 4.0;
    return (bar - 1.0) * beatsPerBar * 60.0 / bpm;
}

// ============================================================================
// State Snapshot
// ============================================================================

juce::String Interpreter::buildStateSnapshot() {
    auto& tm = TrackManager::getInstance();

    auto* root = new juce::DynamicObject();

    // Tracks — lightweight: just id, name, type
    juce::Array<juce::var> tracksArray;
    int index = 1;
    for (const auto& track : tm.getTracks()) {
        auto* trackObj = new juce::DynamicObject();
        trackObj->setProperty("id", index);
        trackObj->setProperty("name", track.name);
        trackObj->setProperty("type", juce::String(getTrackTypeName(track.type)));
        tracksArray.add(juce::var(trackObj));
        index++;
    }
    root->setProperty("tracks", tracksArray);
    root->setProperty("track_count", tm.getNumTracks());

    // Current selection context
    auto& sm = SelectionManager::getInstance();
    auto selTrack = sm.getSelectedTrack();
    if (selTrack != INVALID_TRACK_ID) {
        // Find 1-based index for the selected track
        int selIndex = 1;
        for (const auto& track : tm.getTracks()) {
            if (track.id == selTrack)
                break;
            selIndex++;
        }
        root->setProperty("selected_track_id", selIndex);
    }

    // Selected clip context
    auto& cm = ClipManager::getInstance();
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
    // Parse note names like C4, C#4, Db3, Bb3 → MIDI number
    // C4 = 60
    if (name.empty())
        return -1;

    // If it's a plain number, return it directly
    bool allDigits = true;
    size_t start = (name[0] == '-') ? 1 : 0;
    for (size_t i = start; i < name.size(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
            allDigits = false;
            break;
        }
    }
    if (allDigits && !name.empty())
        return std::atoi(name.c_str());

    // Parse note letter
    static const int noteOffsets[] = {
        9, 11, 0, 2, 4, 5, 7  // A=9, B=11, C=0, D=2, E=4, F=5, G=7
    };

    char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    if (letter < 'A' || letter > 'G')
        return -1;

    int semitone = noteOffsets[letter - 'A'];
    size_t pos = 1;

    // Check for sharp/flat
    if (pos < name.size() && name[pos] == '#') {
        semitone++;
        pos++;
    } else if (pos < name.size() && (name[pos] == 'b' || name[pos] == 'B')) {
        // Distinguish 'b' (flat) from 'B' at start — here pos>0 so it's a modifier
        // But watch for "Bb3" — first B is note, second b is flat
        semitone--;
        pos++;
    }

    // Parse octave number
    if (pos >= name.size())
        return -1;

    bool negative = false;
    if (name[pos] == '-') {
        negative = true;
        pos++;
    }
    if (pos >= name.size())
        return -1;

    int octave = 0;
    while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos]))) {
        octave = octave * 10 + (name[pos] - '0');
        pos++;
    }
    if (negative)
        octave = -octave;

    // MIDI: C4 = 60, so octave 4 base = (4+1)*12 = 60, C offset = 0 → 60
    return (octave + 1) * 12 + semitone;
}

// ============================================================================
// Helper: Get Selected Clip
// ============================================================================

ClipId Interpreter::getSelectedClipId() const {
    // Prefer clip set during current DSL execution (e.g. by clip.new or clips.select)
    if (ctx_.currentClipId >= 0)
        return ctx_.currentClipId;

    // Fall back to UI selection
    auto& sm = SelectionManager::getInstance();
    auto clipId = sm.getSelectedClip();
    if (clipId != INVALID_CLIP_ID)
        return clipId;

    // Also check multi-selection — if exactly one clip selected, use it
    const auto& selected = sm.getSelectedClips();
    if (selected.size() == 1)
        return *selected.begin();

    // Fall back to the first clip on the current track
    if (ctx_.currentTrackId >= 0) {
        auto& cm = ClipManager::getInstance();
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
    auto& sm = SelectionManager::getInstance();
    if (sm.getNoteSelection().isValid())
        return true;

    // No notes selected — try to auto-select all notes in the current clip
    auto clipId = getSelectedClipId();
    if (clipId == INVALID_CLIP_ID)
        return false;

    auto& cm = ClipManager::getInstance();
    auto* clip = cm.getClip(clipId);
    if (!clip || clip->midiNotes.empty())
        return false;

    std::vector<size_t> allIndices;
    allIndices.reserve(clip->midiNotes.size());
    for (size_t i = 0; i < clip->midiNotes.size(); i++)
        allIndices.push_back(i);

    sm.selectNotes(clipId, allIndices);
    return sm.getNoteSelection().isValid();
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

    auto& cm = ClipManager::getInstance();
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

    auto& sm = SelectionManager::getInstance();
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

    UndoManager::getInstance().executeCommand(
        std::make_unique<AddMidiNoteCommand>(clipId, beat, noteNumber, length, velocity));

    ctx_.addResult("Added note (pitch=" + juce::String(noteNumber) +
                   ", beat=" + juce::String(beat, 2) + ", length=" + juce::String(length, 2) +
                   ", velocity=" + juce::String(velocity) + ")");
    return true;
}

bool Interpreter::resolveChordNotes(const Params& params, std::vector<int>& outNotes) {
    // Parse root (required)
    if (!params.has("root")) {
        ctx_.setError("notes.add_chord requires 'root' parameter");
        return false;
    }
    int rootNote = parseNoteName(params.get("root"));
    if (rootNote < 0 || rootNote > 127) {
        ctx_.setError("Invalid root note: " + juce::String(params.get("root")));
        return false;
    }

    // Parse quality (required)
    if (!params.has("quality")) {
        ctx_.setError("notes.add_chord requires 'quality' parameter");
        return false;
    }

    // Chord quality → semitone intervals
    static const std::map<std::string, std::vector<int>> chordQualities = {
        {"major", {0, 4, 7}},
        {"maj", {0, 4, 7}},
        {"minor", {0, 3, 7}},
        {"min", {0, 3, 7}},
        {"dim", {0, 3, 6}},
        {"aug", {0, 4, 8}},
        {"sus2", {0, 2, 7}},
        {"sus4", {0, 5, 7}},
        {"dom7", {0, 4, 7, 10}},
        {"7", {0, 4, 7, 10}},
        {"maj7", {0, 4, 7, 11}},
        {"min7", {0, 3, 7, 10}},
        {"dim7", {0, 3, 6, 9}},
        // 9th chords
        {"dom9", {0, 4, 7, 10, 14}},
        {"9", {0, 4, 7, 10, 14}},
        {"maj9", {0, 4, 7, 11, 14}},
        {"min9", {0, 3, 7, 10, 14}},
        // 11th chords
        {"dom11", {0, 4, 7, 10, 14, 17}},
        {"11", {0, 4, 7, 10, 14, 17}},
        {"min11", {0, 3, 7, 10, 14, 17}},
        {"maj11", {0, 4, 7, 11, 14, 17}},
        // 13th chords
        {"dom13", {0, 4, 7, 10, 14, 21}},
        {"13", {0, 4, 7, 10, 14, 21}},
        {"min13", {0, 3, 7, 10, 14, 21}},
        {"maj13", {0, 4, 7, 11, 14, 21}},
        // Add chords
        {"add9", {0, 4, 7, 14}},
        {"add11", {0, 4, 7, 17}},
        {"add13", {0, 4, 7, 21}},
        {"madd9", {0, 3, 7, 14}},
        // 6th chords
        {"6", {0, 4, 7, 9}},
        {"maj6", {0, 4, 7, 9}},
        {"min6", {0, 3, 7, 9}},
        // Altered dominants
        {"7b5", {0, 4, 6, 10}},
        {"7sharp5", {0, 4, 8, 10}},
        {"7b9", {0, 4, 7, 10, 13}},
        {"7sharp9", {0, 4, 7, 10, 15}},
        // Half-diminished
        {"min7b5", {0, 3, 6, 10}},
        {"half_dim", {0, 3, 6, 10}},
        // Other
        {"power", {0, 7}},
        {"5", {0, 7}},
    };

    std::string quality = params.get("quality");
    auto it = chordQualities.find(quality);
    if (it == chordQualities.end()) {
        ctx_.setError("Unknown chord quality: " + juce::String(quality));
        return false;
    }

    const auto& intervals = it->second;
    int inversion = params.getInt("inversion", 0);

    // Build MIDI note numbers from root + intervals
    outNotes.clear();
    for (int interval : intervals)
        outNotes.push_back(rootNote + interval);

    // Apply inversions: rotate the lowest N notes up an octave
    for (int i = 0; i < inversion && i < static_cast<int>(outNotes.size()); i++)
        outNotes[static_cast<size_t>(i)] += 12;

    // Clamp to valid MIDI range
    for (auto& n : outNotes)
        n = juce::jlimit(0, 127, n);

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

    UndoManager::getInstance().executeCommand(std::make_unique<AddMultipleMidiNotesCommand>(
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
        auto* clip = ClipManager::getInstance().getClip(clipId);
        if (clip) {
            double bpm = 120.0;
            auto* engine = TrackManager::getInstance().getAudioEngine();
            if (engine)
                bpm = engine->getTempo();
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

    UndoManager::getInstance().executeCommand(std::make_unique<AddMultipleMidiNotesCommand>(
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

    auto& sm = SelectionManager::getInstance();
    const auto& noteSel = sm.getNoteSelection();

    UndoManager::getInstance().executeCommand(
        std::make_unique<DeleteMultipleMidiNotesCommand>(noteSel.clipId, noteSel.noteIndices));

    int count = static_cast<int>(noteSel.noteIndices.size());
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

    auto& sm = SelectionManager::getInstance();
    const auto& noteSel = sm.getNoteSelection();

    auto& cm = ClipManager::getInstance();
    auto* clip = cm.getClip(noteSel.clipId);
    if (!clip) {
        ctx_.setError("Clip not found for notes.transpose");
        return false;
    }

    std::vector<MoveMultipleMidiNotesCommand::NoteMove> moves;
    for (auto idx : noteSel.noteIndices) {
        if (idx < clip->midiNotes.size()) {
            const auto& note = clip->midiNotes[idx];
            int newNote = juce::jlimit(0, 127, note.noteNumber + semitones);
            moves.push_back({idx, note.startBeat, newNote});
        }
    }

    if (!moves.empty()) {
        UndoManager::getInstance().executeCommand(
            std::make_unique<MoveMultipleMidiNotesCommand>(noteSel.clipId, std::move(moves)));
    }

    ctx_.addResult("Transposed " + juce::String(static_cast<int>(noteSel.noteIndices.size())) +
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

    auto& sm = SelectionManager::getInstance();
    const auto& noteSel = sm.getNoteSelection();

    auto& cm = ClipManager::getInstance();
    auto* clip = cm.getClip(noteSel.clipId);
    if (!clip) {
        ctx_.setError("Clip not found for notes.set_pitch");
        return false;
    }

    std::vector<MoveMultipleMidiNotesCommand::NoteMove> moves;
    for (auto idx : noteSel.noteIndices) {
        if (idx < clip->midiNotes.size()) {
            const auto& note = clip->midiNotes[idx];
            moves.push_back({idx, note.startBeat, targetPitch});
        }
    }

    if (!moves.empty()) {
        UndoManager::getInstance().executeCommand(
            std::make_unique<MoveMultipleMidiNotesCommand>(noteSel.clipId, std::move(moves)));
    }

    ctx_.addResult("Set pitch to " + juce::String(params.get("pitch")) + " on " +
                   juce::String(static_cast<int>(noteSel.noteIndices.size())) + " note(s)");
    return true;
}

bool Interpreter::executeSetVelocity(const Params& params) {
    int velocity = params.getInt("value", 100);
    velocity = juce::jlimit(0, 127, velocity);

    if (!ensureNoteSelection()) {
        ctx_.setError("No notes selected for notes.set_velocity");
        return false;
    }

    auto& sm = SelectionManager::getInstance();
    const auto& noteSel = sm.getNoteSelection();

    std::vector<std::pair<size_t, int>> noteVelocities;
    for (auto idx : noteSel.noteIndices)
        noteVelocities.emplace_back(idx, velocity);

    UndoManager::getInstance().executeCommand(std::make_unique<SetMultipleNoteVelocitiesCommand>(
        noteSel.clipId, std::move(noteVelocities)));

    ctx_.addResult("Set velocity to " + juce::String(velocity) + " on " +
                   juce::String(static_cast<int>(noteSel.noteIndices.size())) + " note(s)");
    return true;
}

bool Interpreter::executeQuantize(const Params& params) {
    double grid = params.getFloat("grid", 0.25);

    if (!ensureNoteSelection()) {
        ctx_.setError("No notes selected for notes.quantize");
        return false;
    }

    auto& sm = SelectionManager::getInstance();
    const auto& noteSel = sm.getNoteSelection();

    UndoManager::getInstance().executeCommand(std::make_unique<QuantizeMidiNotesCommand>(
        noteSel.clipId, noteSel.noteIndices, grid, QuantizeMode::StartOnly));

    juce::String gridName;
    if (std::abs(grid - 0.25) < 0.001)
        gridName = "16th notes";
    else if (std::abs(grid - 0.5) < 0.001)
        gridName = "8th notes";
    else if (std::abs(grid - 1.0) < 0.001)
        gridName = "quarter notes";
    else
        gridName = juce::String(grid, 2) + " beats";

    ctx_.addResult("Quantized " + juce::String(static_cast<int>(noteSel.noteIndices.size())) +
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

    auto& sm = SelectionManager::getInstance();
    const auto& noteSel = sm.getNoteSelection();

    std::vector<std::pair<size_t, double>> noteLengths;
    for (auto idx : noteSel.noteIndices)
        noteLengths.emplace_back(idx, length);

    UndoManager::getInstance().executeCommand(
        std::make_unique<ResizeMultipleMidiNotesCommand>(noteSel.clipId, std::move(noteLengths)));

    ctx_.addResult("Resized " + juce::String(static_cast<int>(noteSel.noteIndices.size())) +
                   " note(s) to " + juce::String(length, 2) + " beats");
    return true;
}

}  // namespace magda::dsl
