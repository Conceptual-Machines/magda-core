#include "compact_parser.hpp"

namespace magda {

bool CompactParser::isInteger(const juce::String& s) {
    if (s.isEmpty())
        return false;
    int start = (s[0] == '-') ? 1 : 0;
    for (int i = start; i < s.length(); ++i)
        if (!juce::CharacterFunctions::isDigit(s[i]))
            return false;
    return s.length() > start;
}

TrackRef CompactParser::parseRef(const juce::String& token) {
    TrackRef ref;
    if (isInteger(token))
        ref.id = token.getIntValue();
    else
        ref.name = token;
    return ref;
}

std::vector<Instruction> CompactParser::parse(const juce::String& compact) {
    lastError_ = {};

    juce::StringArray lines;
    lines.addLines(compact);

    std::vector<Instruction> instructions;

    for (auto& raw : lines) {
        auto line = raw.trim();
        if (line.isEmpty())
            continue;

        juce::StringArray parts;
        parts.addTokens(line, " ", "\"");
        parts.removeEmptyStrings();

        if (parts.isEmpty())
            continue;

        auto op = parts[0].toUpperCase();

        if (op == "TRACK") {
            if (parts.size() < 2) {
                lastError_ = "TRACK requires a name";
                return {};
            }
            TrackOp payload;
            // TRACK FX <alias> — create track named after plugin + add plugin
            if (parts[1].toUpperCase() == "FX" && parts.size() >= 3) {
                payload.fxAlias = parts[2];
                for (int i = 3; i < parts.size(); ++i)
                    payload.fxAlias += " " + parts[i];
                // name left empty — executor will resolve from plugin
            } else {
                payload.name = parts[1];
                for (int i = 2; i < parts.size(); ++i)
                    payload.name += " " + parts[i];
            }
            instructions.push_back({OpCode::Track, payload});
        } else if (op == "DEL") {
            if (parts.size() < 2) {
                lastError_ = "DEL requires a track ref";
                return {};
            }
            instructions.push_back({OpCode::Del, DelOp{parseRef(parts[1])}});
        } else if (op == "MUTE") {
            if (parts.size() < 2) {
                lastError_ = "MUTE requires a track name";
                return {};
            }
            MuteOp payload;
            payload.name = parts[1];
            for (int i = 2; i < parts.size(); ++i)
                payload.name += " " + parts[i];
            instructions.push_back({OpCode::Mute, payload});
        } else if (op == "SOLO") {
            if (parts.size() < 2) {
                lastError_ = "SOLO requires a track name";
                return {};
            }
            SoloOp payload;
            payload.name = parts[1];
            for (int i = 2; i < parts.size(); ++i)
                payload.name += " " + parts[i];
            instructions.push_back({OpCode::Solo, payload});
        } else if (op == "SET") {
            // SET key=val ...          (implicit track)
            // SET <ref> key=val ...    (explicit track)
            if (parts.size() < 2) {
                lastError_ = "SET requires at least one key=value";
                return {};
            }
            SetOp payload;
            int kvStart = 1;
            if (parts[1].contains("=")) {
                // First arg is a key=value → implicit
                payload.target.implicit = true;
            } else {
                payload.target = parseRef(parts[1]);
                kvStart = 2;
            }
            for (int i = kvStart; i < parts.size(); ++i) {
                auto kv = parts[i];
                auto eqIdx = kv.indexOfChar('=');
                if (eqIdx > 0)
                    payload.props.set(kv.substring(0, eqIdx), kv.substring(eqIdx + 1));
            }
            instructions.push_back({OpCode::Set, payload});
        } else if (op == "CLIP") {
            // CLIP <bar> <length_bars>         (implicit track, 2 args)
            // CLIP <ref> <bar> <length_bars>   (explicit track, 3 args)
            if (parts.size() < 3) {
                lastError_ = "CLIP requires <bar> <length_bars>";
                return {};
            }
            ClipOp payload;
            if (isInteger(parts[1]) && parts.size() >= 4) {
                // Numeric first arg → explicit track: CLIP <ref> <bar> <len> [name...]
                payload.target = parseRef(parts[1]);
                payload.bar = parts[2].getDoubleValue();
                payload.lengthBars = parts[3].getDoubleValue();
                if (parts.size() > 4) {
                    payload.name = parts[4];
                    for (int i = 5; i < parts.size(); ++i)
                        payload.name += " " + parts[i];
                }
            } else {
                // Implicit track: CLIP <bar> <len> [name...]
                payload.target.implicit = true;
                payload.bar = parts[1].getDoubleValue();
                payload.lengthBars = parts[2].getDoubleValue();
                if (parts.size() > 3) {
                    payload.name = parts[3];
                    for (int i = 4; i < parts.size(); ++i)
                        payload.name += " " + parts[i];
                }
            }
            instructions.push_back({OpCode::Clip, payload});
        } else if (op == "SELECT") {
            // SELECT CLIPS [WHERE <field> <op> <value>]
            // SELECT TRACKS [WHERE <field> <op> <value>]
            if (parts.size() < 2) {
                lastError_ = "SELECT requires CLIPS or TRACKS";
                return {};
            }
            SelectOp payload;
            auto target = parts[1].toUpperCase();
            if (target == "CLIPS")
                payload.target = SelectOp::Target::Clips;
            else if (target == "TRACKS")
                payload.target = SelectOp::Target::Tracks;
            else {
                lastError_ = "SELECT target must be CLIPS or TRACKS";
                return {};
            }
            // Optional WHERE clause
            if (parts.size() >= 5 && parts[2].toUpperCase() == "WHERE") {
                payload.field = parts[3].toLowerCase();
                payload.op = parts[4];
                if (parts.size() > 5) {
                    payload.value = parts[5];
                    for (int i = 6; i < parts.size(); ++i)
                        payload.value += " " + parts[i];
                }
            }
            instructions.push_back({OpCode::Select, payload});
        } else if (op == "FX") {
            // FX <fx_name>          (implicit track)
            // FX <ref> <fx_name>    (explicit track)
            if (parts.size() < 2) {
                lastError_ = "FX requires <fx_name>";
                return {};
            }
            FxOp payload;
            if (parts.size() == 2) {
                // Single arg → implicit track, arg is the fx name
                payload.target.implicit = true;
                payload.fxName = parts[1];
            } else if (isInteger(parts[1])) {
                // Numeric first arg → explicit track by index
                payload.target = parseRef(parts[1]);
                payload.fxName = parts[2];
                for (int i = 3; i < parts.size(); ++i)
                    payload.fxName += " " + parts[i];
            } else {
                // Ambiguous: could be "FX TrackName fx_name" or "FX fx_name_with_spaces"
                // If there's exactly 2 remaining tokens and last one looks like an alias (has
                // underscore or is a known keyword), treat first as ref. Otherwise treat all as fx
                // name implicitly. For safety, treat single-word first arg as ref, rest as fx name
                // (old behavior with explicit ref) but mark that we need at least 2 non-op tokens
                // for explicit ref.
                payload.target.implicit = true;
                payload.fxName = parts[1];
                for (int i = 2; i < parts.size(); ++i)
                    payload.fxName += " " + parts[i];
            }
            instructions.push_back({OpCode::Fx, payload});
        } else if (op == "ARP") {
            if (parts.size() < 5) {
                lastError_ = "ARP requires <root> <quality> <beat> <step>";
                return {};
            }
            ArpOp payload;
            payload.root = parts[1];
            payload.quality = parts[2];
            payload.beat = parts[3].getDoubleValue();
            payload.step = parts[4].getDoubleValue();
            if (parts.size() > 5)
                payload.beats = parts[5].getDoubleValue();
            instructions.push_back({OpCode::Arp, payload});
        } else if (op == "CHORD") {
            if (parts.size() < 5) {
                lastError_ = "CHORD requires <root> <quality> <beat> <length>";
                return {};
            }
            ChordOp payload;
            payload.root = parts[1];
            payload.quality = parts[2];
            payload.beat = parts[3].getDoubleValue();
            payload.length = parts[4].getDoubleValue();
            if (parts.size() > 5)
                payload.velocity = parts[5].getIntValue();
            instructions.push_back({OpCode::Chord, payload});
        } else if (op == "NOTE") {
            if (parts.size() < 4) {
                lastError_ = "NOTE requires <pitch> <beat> <length>";
                return {};
            }
            NoteOp payload;
            payload.pitch = parts[1];
            payload.beat = parts[2].getDoubleValue();
            payload.length = parts[3].getDoubleValue();
            if (parts.size() > 4)
                payload.velocity = parts[4].getIntValue();
            instructions.push_back({OpCode::Note, payload});
        } else {
            lastError_ = "Unknown instruction: " + op;
            return {};
        }
    }

    return instructions;
}

}  // namespace magda
