#include "ThemePrompt.hpp"

#include "DarkTheme.hpp"
#include "ThemeSerialization.hpp"
#include "UserTheme.hpp"

namespace magda {

juce::String buildThemeSystemPrompt() {
    juce::String p;
    p << "You are a UI theme designer for the MAGDA digital audio workstation. Given a "
         "description, design a cohesive colour theme and return it as structured JSON.\n\n";

    p << "OUTPUT SHAPE:\n"
         "{\n"
         "  \"name\": \"<2-4 word theme name>\",\n"
         "  \"base\": \"dark\" | \"light\",\n"
         "  \"colours\": [ { \"role\": \"<roleKey>\", \"value\": \"#RRGGBB\" }, ... ],\n"
         "  \"syntaxColours\": [ { \"role\": \"<syntaxRoleKey>\", \"value\": \"#RRGGBB\" }, ... ]\n"
         "}\n"
         "Each entry sets one role. Omit any role you want to inherit from the base table.\n\n";

    p << "RULES:\n"
         "- Choose base \"dark\" or \"light\" as the starting point; unset roles inherit from it.\n"
         "- Colours are \"#RRGGBB\" (opaque) or \"#AARRGGBB\" (alpha first). A few overlay roles "
         "read their alpha (timeSelection, loopRegion) - keep those translucent.\n"
         "- Design a COHESIVE palette: pick 1-2 base hues; keep e0..e3 as a tight elevation ramp "
         "(each step slightly lighter on dark / darker on light); make sure text has strong "
         "contrast against the surfaces behind it.\n"
         "- Set at least: e0, e1, e2, e3, background, panelBackground, surface, surfaceHover, "
         "hairline, border, textPrimary, textSecondary, textDim, and accent1..accent6.\n"
         "- accent1..accent6 are a hue-neutral accent ramp (NOT tied to a fixed hue). accent1 is "
         "the primary accent (selection / active), accent2 attention, accent3 positive, accent4 "
         "modulation, accent5 info, accent6 a soft variant of accent1.\n"
         "- syntaxColours theme the code editor and the AI console. Keep editorBackground and "
         "lineNumberBackground in the same surface family as the app, make every token colour "
         "legible against editorBackground, and give keyword / method / param / number / string / "
         "noteName visibly different hues so code stays readable. editorSelection is drawn over "
         "text - give it an \"#AARRGGBB\" value with roughly 30-40% alpha.\n"
         "- Any syntax role you omit is derived from your palette, so a partial list is safe; "
         "prefer designing them.\n\n";

    p << "ROLE KEYS (with the DARK base value for reference - use these exact keys):\n";
    const auto& dark = ThemeManager::builtInPalette(ThemeManager::kDarkThemeId);
    for (std::size_t i = 0; i < dark.size(); ++i)
        p << "  " << colourRoleName(static_cast<ColourRole>(i)) << ": "
          << colourToHexString(juce::Colour(dark[i])) << "\n";

    p << "\nSYNTAX ROLE KEYS (same idea, DARK base values shown):\n";
    const auto& darkSyntax = ThemeManager::builtInSyntaxPalette(ThemeManager::kDarkThemeId);
    for (std::size_t i = 0; i < darkSyntax.size(); ++i)
        p << "  " << syntaxColourRoleName(static_cast<SyntaxColourRole>(i)) << ": "
          << colourToHexString(juce::Colour(darkSyntax[i])) << "\n";

    p << "\nEXAMPLE\n"
         "User: \"warm sunset, dark\"\n"
         "{\"name\":\"Sunset Dusk\",\"base\":\"dark\",\"colours\":["
         "{\"role\":\"e0\",\"value\":\"#1A1114\"},{\"role\":\"e1\",\"value\":\"#211519\"},"
         "{\"role\":\"e2\",\"value\":\"#2A1B20\"},{\"role\":\"e3\",\"value\":\"#341F26\"},"
         "{\"role\":\"background\",\"value\":\"#1A1114\"},"
         "{\"role\":\"panelBackground\",\"value\":\"#211519\"},"
         "{\"role\":\"surface\",\"value\":\"#2A1B20\"},"
         "{\"role\":\"surfaceHover\",\"value\":\"#341F26\"},"
         "{\"role\":\"hairline\",\"value\":\"#4A2E36\"},{\"role\":\"border\",\"value\":\"#4A2E36\"}"
         ","
         "{\"role\":\"textPrimary\",\"value\":\"#F4E3DA\"},"
         "{\"role\":\"textSecondary\",\"value\":\"#C9A99D\"},"
         "{\"role\":\"textDim\",\"value\":\"#9A7D74\"},"
         "{\"role\":\"accent1\",\"value\":\"#E8743B\"},{\"role\":\"accent2\",\"value\":\"#F2A65A\"}"
         ","
         "{\"role\":\"accent3\",\"value\":\"#C4553D\"},{\"role\":\"accent4\",\"value\":\"#B5657F\"}"
         ","
         "{\"role\":\"accent5\",\"value\":\"#D98C4A\"},{\"role\":\"accent6\",\"value\":\"#F0B27A\"}"
         "],"
         "\"syntaxColours\":["
         "{\"role\":\"editorBackground\",\"value\":\"#1A1114\"},"
         "{\"role\":\"editorDefaultText\",\"value\":\"#F4E3DA\"},"
         "{\"role\":\"editorSelection\",\"value\":\"#59E8743B\"},"
         "{\"role\":\"dslTokenKeyword\",\"value\":\"#F2A65A\"},"
         "{\"role\":\"dslTokenString\",\"value\":\"#E8A0B4\"},"
         "{\"role\":\"dslTokenNumber\",\"value\":\"#C9D18A\"},"
         "{\"role\":\"dslTokenComment\",\"value\":\"#9A7D74\"}"
         "]}\n";

    return p;
}

juce::var buildThemeSchema() {
    // One {role, value} array per vocabulary: the role enum is the only thing
    // that differs, so both are built from the same shape.
    const auto makeColourArray = [](const juce::Array<juce::var>& roleEnum) {
        auto* roleProp = new juce::DynamicObject();
        roleProp->setProperty("type", "string");
        roleProp->setProperty("enum", roleEnum);

        auto* valueProp = new juce::DynamicObject();
        valueProp->setProperty("type", "string");

        auto* entryProps = new juce::DynamicObject();
        entryProps->setProperty("role", juce::var(roleProp));
        entryProps->setProperty("value", juce::var(valueProp));

        juce::Array<juce::var> entryRequired;
        entryRequired.add("role");
        entryRequired.add("value");

        auto* entry = new juce::DynamicObject();
        entry->setProperty("type", "object");
        entry->setProperty("properties", juce::var(entryProps));
        entry->setProperty("required", entryRequired);
        entry->setProperty("additionalProperties", false);

        auto* arrayProp = new juce::DynamicObject();
        arrayProp->setProperty("type", "array");
        arrayProp->setProperty("items", juce::var(entry));
        return arrayProp;
    };

    juce::Array<juce::var> roleEnum;
    for (std::size_t i = 0; i < static_cast<std::size_t>(ColourRole::count); ++i)
        roleEnum.add(juce::String(colourRoleName(static_cast<ColourRole>(i))));
    auto* coloursProp = makeColourArray(roleEnum);

    juce::Array<juce::var> syntaxRoleEnum;
    for (std::size_t i = 0; i < static_cast<std::size_t>(SyntaxColourRole::count); ++i)
        syntaxRoleEnum.add(juce::String(syntaxColourRoleName(static_cast<SyntaxColourRole>(i))));
    auto* syntaxColoursProp = makeColourArray(syntaxRoleEnum);

    juce::Array<juce::var> baseEnum;
    baseEnum.add("dark");
    baseEnum.add("light");

    auto* baseProp = new juce::DynamicObject();
    baseProp->setProperty("type", "string");
    baseProp->setProperty("enum", baseEnum);

    auto* nameProp = new juce::DynamicObject();
    nameProp->setProperty("type", "string");

    auto* props = new juce::DynamicObject();
    props->setProperty("name", juce::var(nameProp));
    props->setProperty("base", juce::var(baseProp));
    props->setProperty("colours", juce::var(coloursProp));
    props->setProperty("syntaxColours", juce::var(syntaxColoursProp));

    // Strict structured-output modes want every property listed as required;
    // an empty syntaxColours array is still a valid answer (the validator
    // derives the missing roles from the palette).
    juce::Array<juce::var> rootRequired;
    rootRequired.add("name");
    rootRequired.add("base");
    rootRequired.add("colours");
    rootRequired.add("syntaxColours");

    auto* schema = new juce::DynamicObject();
    schema->setProperty("type", "object");
    schema->setProperty("properties", juce::var(props));
    schema->setProperty("required", rootRequired);
    schema->setProperty("additionalProperties", false);

    return juce::var(schema);
}

std::optional<GeneratedTheme> validateGeneratedTheme(const juce::String& llmText,
                                                     std::string& error) {
    // Structured-output providers return clean JSON, but providers that ignore
    // the schema (older models, non-JSON-mode) may still wrap it in a fence -
    // strip it defensively.
    juce::String trimmed = llmText.trim();
    if (trimmed.startsWith("```")) {
        const auto firstNewline = trimmed.indexOf("\n");
        if (firstNewline > 0)
            trimmed = trimmed.substring(firstNewline + 1);
        if (trimmed.endsWith("```"))
            trimmed = trimmed.dropLastCharacters(3).trim();
    }

    auto parsed = juce::JSON::parse(trimmed);
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) {
        error = "theme JSON must be an object";
        return std::nullopt;
    }

    juce::String name = obj->getProperty("name").toString().trim();
    if (name.isEmpty())
        name = "AI Theme";
    juce::String base = obj->getProperty("base").toString().trim().toLowerCase();
    if (base != "light")
        base = "dark";

    // Walks either output form: the structured [{role, value}] array or the
    // older {key: value} object map. Returns false only when the property is
    // neither, so a missing section can be told apart from an empty one.
    const auto forEachEntry = [&obj](const char* property, auto&& fn) {
        const auto value = obj->getProperty(property);
        if (auto* array = value.getArray()) {
            for (const auto& entry : *array)
                if (auto* entryObj = entry.getDynamicObject())
                    fn(entryObj->getProperty("role").toString(),
                       entryObj->getProperty("value").toString());
            return true;
        }
        if (auto* mapObj = value.getDynamicObject()) {
            for (const auto& kv : mapObj->getProperties())
                fn(kv.name.toString(), kv.value.toString());
            return true;
        }
        return false;
    };

    // Fold the model's colours into the file's {roleKey: value} map, keyed on
    // the CANONICAL role name so a loosely-spelled role normalizes on the way
    // in. The parsed values also build the resolved palette, which is what the
    // syntax colours are derived from below.
    auto palette = ThemeManager::builtInPalette(base == "light" ? ThemeManager::kLightThemeId
                                                                : ThemeManager::kDarkThemeId);
    juce::DynamicObject::Ptr coloursMap = new juce::DynamicObject();
    int recognized = 0;
    const auto addColour = [&](const juce::String& roleKey, const juce::String& value) {
        const auto role = findColourRole(roleKey);
        const auto colour = parseColourString(value);
        if (!role || !colour)
            return;
        coloursMap->setProperty(juce::Identifier(colourRoleName(*role)), value);
        palette[static_cast<std::size_t>(*role)] = colour->getARGB();
        ++recognized;
    };

    if (!forEachEntry("colours", addColour)) {
        error = "theme JSON is missing a 'colours' array";
        return std::nullopt;
    }

    if (recognized == 0) {
        error = "theme JSON has no recognized colour roles";
        return std::nullopt;
    }

    // Syntax colours are always written out in full: whatever the model
    // designed, over a palette-derived default for the rest. A theme file
    // therefore always carries a complete, editable syntaxColours section, and
    // the code editor can never keep the base theme's colours by accident.
    auto syntaxPalette = deriveSyntaxPalette(palette);
    int syntaxRecognized = 0;
    forEachEntry("syntaxColours", [&](const juce::String& roleKey, const juce::String& value) {
        const auto role = findSyntaxColourRole(roleKey);
        const auto colour = parseColourString(value);
        if (!role || !colour)
            return;
        syntaxPalette[static_cast<std::size_t>(*role)] = colour->getARGB();
        ++syntaxRecognized;
    });

    juce::DynamicObject::Ptr syntaxMap = new juce::DynamicObject();
    for (std::size_t i = 0; i < syntaxPalette.size(); ++i)
        syntaxMap->setProperty(
            juce::Identifier(syntaxColourRoleName(static_cast<SyntaxColourRole>(i))),
            colourToHexString(juce::Colour(syntaxPalette[i])));

    juce::DynamicObject::Ptr clean = new juce::DynamicObject();
    clean->setProperty("schemaVersion", kThemeSchemaVersion);
    clean->setProperty("name", name);
    clean->setProperty("base", base);
    clean->setProperty("colours", juce::var(coloursMap.get()));
    clean->setProperty("syntaxColours", juce::var(syntaxMap.get()));

    GeneratedTheme out;
    out.name = name.toStdString();
    out.base = base.toStdString();
    out.json = juce::JSON::toString(juce::var(clean.get())).toStdString();
    out.colourCount = recognized;
    out.syntaxCount = syntaxRecognized;
    return out;
}

}  // namespace magda
