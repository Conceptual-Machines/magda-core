#include "remote_scopes.hpp"

#include <algorithm>

namespace magda::remote {
namespace {

struct ScopeNaming {
    Scope scope;
    const char* name;
};

/// The one table. `scopeName` and `scopeFromName` both read it, so a new scope
/// cannot be added to one direction and forgotten in the other.
constexpr ScopeNaming kScopeNames[] = {
    {Scope::Read, "read"},
    {Scope::Edit, "edit"},
    {Scope::Transport, "transport"},
    {Scope::Session, "session"},
    {Scope::HardwareMidi, "hardware-midi"},
};

static_assert(std::size(kScopeNames) == SCOPE_COUNT,
              "every Scope needs a wire name, and no name may outlive its Scope");

}  // namespace

juce::String scopeName(Scope scope) {
    for (const auto& entry : kScopeNames) {
        if (entry.scope == scope)
            return entry.name;
    }
    jassertfalse;  // A Scope with no name, which the static_assert should have caught.
    return {};
}

std::optional<Scope> scopeFromName(const juce::String& name) {
    for (const auto& entry : kScopeNames) {
        if (name == entry.name)
            return entry.scope;
    }
    return std::nullopt;
}

const std::vector<Scope>& allScopeValues() {
    static const std::vector<Scope> values = [] {
        std::vector<Scope> result;
        result.reserve(SCOPE_COUNT);
        for (const auto& entry : kScopeNames)
            result.push_back(entry.scope);
        return result;
    }();
    return values;
}

ScopeSet allScopes() {
    ScopeSet result;
    for (const auto scope : allScopeValues())
        result.add(scope);
    return result;
}

ScopeSet defaultClientScopes() {
    return ScopeSet{Scope::Read};
}

std::vector<juce::String> scopeNames(ScopeSet scopes) {
    std::vector<juce::String> names;
    for (const auto scope : allScopeValues()) {
        if (scopes.has(scope))
            names.push_back(scopeName(scope));
    }
    return names;
}

juce::var scopesToJson(ScopeSet scopes) {
    juce::Array<juce::var> array;
    for (const auto& name : scopeNames(scopes))
        array.add(name);
    return array;
}

ScopeSet scopesFromJson(const juce::var& value) {
    ScopeSet result;
    if (const auto* array = value.getArray()) {
        for (const auto& entry : *array) {
            if (const auto scope = scopeFromName(entry.toString()))
                result.add(*scope);
        }
    }
    return result;
}

juce::String describeScopes(ScopeSet scopes) {
    const auto names = scopeNames(scopes);
    if (names.empty())
        return "none";

    juce::String result;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index > 0)
            result += ", ";
        result += names[index];
    }
    return result;
}

juce::String normaliseClientName(const juce::String& name) {
    juce::String result;
    result.preallocateBytes(static_cast<std::size_t>(MAX_CLIENT_NAME_LENGTH));

    // Deliberately a permit-list rather than a strip-list. This string ends up
    // as a config key, a settings table row, and an audit field, and the set of
    // characters that are awkward in at least one of those is much larger and
    // much less obvious than the set that is fine in all three.
    bool lastWasSeparator = false;
    for (auto character = name.getCharPointer(); !character.isEmpty(); ++character) {
        const auto letter = juce::CharacterFunctions::toLowerCase(*character);
        if (juce::CharacterFunctions::isLetterOrDigit(letter) || letter == '.') {
            result += letter;
            lastWasSeparator = false;
            continue;
        }
        // Runs of anything else collapse to one hyphen, and a leading one is
        // dropped: `Claude Code (v2)` is `claude-code-v2`, not
        // `claude-code--v2-`.
        if (!lastWasSeparator && result.isNotEmpty())
            result += '-';
        lastWasSeparator = true;
    }

    while (result.endsWithChar('-'))
        result = result.dropLastCharacters(1);

    if (result.length() > MAX_CLIENT_NAME_LENGTH) {
        result = result.substring(0, MAX_CLIENT_NAME_LENGTH);
        while (result.endsWithChar('-'))
            result = result.dropLastCharacters(1);
    }

    return result.isEmpty() ? juce::String(ANONYMOUS_CLIENT) : result;
}

}  // namespace magda::remote
