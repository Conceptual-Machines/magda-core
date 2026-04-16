#include "automation_parser.hpp"

namespace magda {

namespace {

/** Parse "key=value" into two trimmed halves. Returns false if no '='. */
bool splitKV(const juce::String& token, juce::String& key, juce::String& value) {
    auto eq = token.indexOfChar('=');
    if (eq < 0)
        return false;
    key = token.substring(0, eq).trim();
    value = token.substring(eq + 1).trim();
    return true;
}

/** Parse target spec into AutoTarget. */
bool parseTarget(const juce::String& value, AutoTarget& out, juce::String& err) {
    auto v = value.trim();
    if (v.equalsIgnoreCase("selected")) {
        out.kind = AutoTarget::Kind::Selected;
        return true;
    }
    if (v.equalsIgnoreCase("trackVolume") || v.equalsIgnoreCase("volume")) {
        out.kind = AutoTarget::Kind::TrackVolume;
        return true;
    }
    if (v.equalsIgnoreCase("trackPan") || v.equalsIgnoreCase("pan")) {
        out.kind = AutoTarget::Kind::TrackPan;
        return true;
    }
    if (v.startsWithIgnoreCase("laneId:")) {
        auto num = v.fromFirstOccurrenceOf(":", false, false);
        out.kind = AutoTarget::Kind::LaneId;
        out.laneId = num.getIntValue();
        if (out.laneId == INVALID_AUTOMATION_LANE_ID) {
            err = "Invalid laneId: " + v;
            return false;
        }
        return true;
    }
    err = "Unknown target: " + v;
    return false;
}

AutoShape parseShape(const juce::String& s, bool& ok) {
    ok = true;
    if (s.equalsIgnoreCase("sin"))
        return AutoShape::Sin;
    if (s.equalsIgnoreCase("tri"))
        return AutoShape::Tri;
    if (s.equalsIgnoreCase("saw"))
        return AutoShape::Saw;
    if (s.equalsIgnoreCase("square"))
        return AutoShape::Square;
    if (s.equalsIgnoreCase("exp"))
        return AutoShape::Exp;
    if (s.equalsIgnoreCase("log"))
        return AutoShape::Log;
    if (s.equalsIgnoreCase("line"))
        return AutoShape::Line;
    ok = false;
    return AutoShape::Sin;
}

/** Split a line by whitespace, respecting that `points=(...)(...)` should stay
    glued. Simplest approach: tokenise, then re-join tokens that belong to a
    points= list (opened at '(' and closed at matching ')'). */
juce::StringArray tokeniseRespectingParens(const juce::String& line) {
    juce::StringArray out;
    juce::String cur;
    int paren = 0;
    for (int i = 0; i < line.length(); ++i) {
        auto c = line[i];
        if (c == '(')
            ++paren;
        else if (c == ')')
            --paren;

        if (juce::CharacterFunctions::isWhitespace(c) && paren == 0) {
            if (cur.isNotEmpty()) {
                out.add(cur);
                cur.clear();
            }
        } else {
            cur += juce::String::charToString(c);
        }
    }
    if (cur.isNotEmpty())
        out.add(cur);
    return out;
}

/** Parse "(0,0.1)(1,0.5)(2,0.9)" into points. */
bool parseFreeformPoints(const juce::String& value, std::vector<AutoFreeformPoint>& out,
                         juce::String& err) {
    auto s = value.trim();
    int i = 0;
    while (i < s.length()) {
        while (i < s.length() && juce::CharacterFunctions::isWhitespace(s[i]))
            ++i;
        if (i >= s.length())
            break;
        if (s[i] != '(') {
            err = "Expected '(' in points list, got '" + juce::String::charToString(s[i]) + "'";
            return false;
        }
        ++i;
        int close = s.indexOfChar(i, ')');
        if (close < 0) {
            err = "Unclosed '(' in points list";
            return false;
        }
        auto inner = s.substring(i, close).trim();
        auto comma = inner.indexOfChar(',');
        if (comma < 0) {
            err = "Expected 'beat,value' in '" + inner + "'";
            return false;
        }
        AutoFreeformPoint p;
        p.beat = inner.substring(0, comma).getDoubleValue();
        p.value = inner.substring(comma + 1).getDoubleValue();
        out.push_back(p);
        i = close + 1;
    }
    if (out.empty()) {
        err = "Empty points list";
        return false;
    }
    return true;
}

}  // namespace

std::vector<AutoInstruction> AutomationParser::parse(const juce::String& text) {
    std::vector<AutoInstruction> out;
    lastError_.clear();

    auto lines = juce::StringArray::fromLines(text);
    for (auto rawLine : lines) {
        auto line = rawLine.trim();
        if (line.isEmpty() || line.startsWith("#") || line.startsWith("//"))
            continue;

        auto tokens = tokeniseRespectingParens(line);
        if (tokens.isEmpty())
            continue;

        if (!tokens[0].equalsIgnoreCase("AUTO")) {
            lastError_ = "Expected 'AUTO' at start of line: " + line;
            return {};
        }
        if (tokens.size() < 2) {
            lastError_ = "Missing shape after AUTO: " + line;
            return {};
        }

        auto shapeStr = tokens[1];

        // Clear op
        if (shapeStr.equalsIgnoreCase("clear")) {
            AutoClearOp op;
            for (int t = 2; t < tokens.size(); ++t) {
                juce::String k, v;
                if (!splitKV(tokens[t], k, v))
                    continue;
                if (k.equalsIgnoreCase("target")) {
                    juce::String err;
                    if (!parseTarget(v, op.target, err)) {
                        lastError_ = err;
                        return {};
                    }
                }
            }
            out.push_back({op});
            continue;
        }

        // Freeform op
        if (shapeStr.equalsIgnoreCase("freeform")) {
            AutoFreeformOp op;
            for (int t = 2; t < tokens.size(); ++t) {
                juce::String k, v;
                if (!splitKV(tokens[t], k, v))
                    continue;
                if (k.equalsIgnoreCase("target")) {
                    juce::String err;
                    if (!parseTarget(v, op.target, err)) {
                        lastError_ = err;
                        return {};
                    }
                } else if (k.equalsIgnoreCase("points")) {
                    juce::String err;
                    if (!parseFreeformPoints(v, op.points, err)) {
                        lastError_ = err;
                        return {};
                    }
                }
            }
            if (op.points.empty()) {
                lastError_ = "freeform missing points=(...) list";
                return {};
            }
            out.push_back({op});
            continue;
        }

        // Shape op
        bool shapeOk = false;
        AutoShape shape = parseShape(shapeStr, shapeOk);
        if (!shapeOk) {
            lastError_ = "Unknown shape: " + shapeStr;
            return {};
        }

        AutoShapeOp op;
        op.shape = shape;
        for (int t = 2; t < tokens.size(); ++t) {
            juce::String k, v;
            if (!splitKV(tokens[t], k, v))
                continue;
            if (k.equalsIgnoreCase("target")) {
                juce::String err;
                if (!parseTarget(v, op.target, err)) {
                    lastError_ = err;
                    return {};
                }
            } else if (k.equalsIgnoreCase("start"))
                op.startBeat = v.getDoubleValue();
            else if (k.equalsIgnoreCase("end"))
                op.endBeat = v.getDoubleValue();
            else if (k.equalsIgnoreCase("min"))
                op.minV = v.getDoubleValue();
            else if (k.equalsIgnoreCase("max"))
                op.maxV = v.getDoubleValue();
            else if (k.equalsIgnoreCase("from"))
                op.fromV = v.getDoubleValue();
            else if (k.equalsIgnoreCase("to"))
                op.toV = v.getDoubleValue();
            else if (k.equalsIgnoreCase("cycles"))
                op.cycles = v.getDoubleValue();
            else if (k.equalsIgnoreCase("duty"))
                op.duty = v.getDoubleValue();
        }

        if (op.endBeat <= op.startBeat) {
            lastError_ = "end must be greater than start";
            return {};
        }

        out.push_back({op});
    }

    return out;
}

}  // namespace magda
