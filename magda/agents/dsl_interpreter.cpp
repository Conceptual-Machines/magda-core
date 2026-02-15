#include "dsl_interpreter.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include "../daw/core/ClipManager.hpp"
#include "../daw/core/TrackManager.hpp"

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

    DBG("MAGDA DSL: Executing: " + juce::String(dslCode).substring(0, 200));

    Tokenizer tok(dslCode);

    while (tok.hasMore()) {
        if (!parseStatement(tok))
            return false;

        if (tok.peek().is(TokenType::SEMICOLON))
            tok.next();
    }

    DBG("MAGDA DSL: Execution complete");
    return true;
}

bool Interpreter::parseStatement(Tokenizer& tok) {
    Token t = tok.peek();

    if (t.is("track"))
        return parseTrackStatement(tok);
    else if (t.is("filter"))
        return parseFilterStatement(tok);
    else if (t.is("note") || t.is("chord") || t.is("arpeggio") || t.is("progression") ||
             t.is("pattern")) {
        // Musical content — not yet supported in MVP
        // Skip the entire statement: consume identifier + parenthesized args
        tok.next();  // consume keyword
        if (!tok.expect(TokenType::LPAREN)) {
            ctx_.setError("Expected '(' after musical statement");
            return false;
        }
        // Skip everything until matching RPAREN
        int depth = 1;
        while (depth > 0 && tok.hasMore()) {
            Token inner = tok.next();
            if (inner.is(TokenType::LPAREN))
                depth++;
            else if (inner.is(TokenType::RPAREN))
                depth--;
        }
        ctx_.addResult("(Musical content not yet supported in MVP)");
        return true;
    } else if (t.type == TokenType::END_OF_INPUT) {
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
        // Try to find existing track by name, create if not found
        juce::String name(params.get("name"));
        int existingId = findTrackByName(name);

        if (existingId >= 0) {
            ctx_.currentTrackId = existingId;
            DBG("MAGDA DSL: Found existing track '" + name + "'");
        } else {
            auto trackType = parseTrackType(params);
            auto trackId = tm.createTrack(name, trackType);
            ctx_.currentTrackId = trackId;
            ctx_.addResult("Created " + juce::String(getTrackTypeName(trackType)) + " track '" +
                           name + "'");
        }
    } else {
        // track() with no params — create unnamed track
        auto trackType = parseTrackType(params);
        auto trackId = tm.createTrack("", trackType);
        ctx_.currentTrackId = trackId;
        ctx_.addResult("Created " + juce::String(getTrackTypeName(trackType)) + " track");
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

        Token method = tok.next();
        if (method.type != TokenType::IDENTIFIER) {
            ctx_.setError("Expected method name after '.'");
            return false;
        }

        if (!tok.expect(TokenType::LPAREN)) {
            ctx_.setError("Expected '(' after method '" + juce::String(method.value) + "'");
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
        if (method.value == "new_clip")
            success = executeNewClip(params);
        else if (method.value == "set_track")
            success = executeSetTrack(params);
        else if (method.value == "delete")
            success = executeDelete();
        else if (method.value == "delete_clip")
            success = executeDeleteClip(params);
        else if (method.value == "add_fx" || method.value == "addAutomation" ||
                 method.value == "add_automation") {
            ctx_.addResult("'" + juce::String(method.value) + "' not yet supported in MVP");
            success = true;  // Don't fail, just skip
        } else {
            ctx_.setError("Unknown method: " + juce::String(method.value));
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
        ctx_.setError("No track context for new_clip");
        return false;
    }

    int bar = params.getInt("bar", 1);
    int lengthBars = params.getInt("length_bars", 4);

    if (bar < 1) {
        ctx_.setError("Bar number must be positive, got " + juce::String(bar));
        return false;
    }
    if (lengthBars < 1) {
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

    ctx_.addResult("Created MIDI clip at bar " + juce::String(bar) + ", length " +
                   juce::String(lengthBars) + " bars");
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
        ctx_.setError("No track context for set_track");
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
        ctx_.setError("No track context for delete_clip");
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

double Interpreter::barsToTime(int bar) const {
    // Convert 1-based bar number to seconds
    // For MVP, use 120 BPM and 4/4 time
    constexpr double bpm = 120.0;
    constexpr double beatsPerBar = 4.0;
    return (bar - 1) * beatsPerBar * 60.0 / bpm;
}

// ============================================================================
// State Snapshot
// ============================================================================

juce::String Interpreter::buildStateSnapshot() {
    auto& tm = TrackManager::getInstance();
    auto& cm = ClipManager::getInstance();

    auto* root = new juce::DynamicObject();

    // Tracks
    juce::Array<juce::var> tracksArray;
    int index = 1;
    for (const auto& track : tm.getTracks()) {
        auto* trackObj = new juce::DynamicObject();
        trackObj->setProperty("id", index);
        trackObj->setProperty("name", track.name);
        trackObj->setProperty("type", juce::String(getTrackTypeName(track.type)));
        trackObj->setProperty("volume", track.volume);
        trackObj->setProperty("pan", track.pan);
        trackObj->setProperty("muted", track.muted);
        trackObj->setProperty("soloed", track.soloed);

        // Clips on this track
        auto clipIds = cm.getClipsOnTrack(track.id);
        if (!clipIds.empty()) {
            juce::Array<juce::var> clipsArray;
            for (auto clipId : clipIds) {
                auto* clip = cm.getClip(clipId);
                if (clip) {
                    auto* clipObj = new juce::DynamicObject();
                    clipObj->setProperty("name", clip->name);
                    clipObj->setProperty("type", clip->type == ClipType::Audio ? "audio" : "midi");
                    clipObj->setProperty("start", clip->startTime);
                    clipObj->setProperty("length", clip->length);
                    clipsArray.add(juce::var(clipObj));
                }
            }
            trackObj->setProperty("clips", clipsArray);
        }

        tracksArray.add(juce::var(trackObj));
        index++;
    }
    root->setProperty("tracks", tracksArray);
    root->setProperty("track_count", tm.getNumTracks());

    return juce::JSON::toString(juce::var(root), true);
}

}  // namespace magda::dsl
