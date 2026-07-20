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
         "  \"colours\": [ { \"role\": \"<roleKey>\", \"value\": \"#RRGGBB\" }, ... ]\n"
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
         "modulation, accent5 info, accent6 a soft variant of accent1.\n\n";

    p << "ROLE KEYS (with the DARK base value for reference - use these exact keys):\n";
    const auto& dark = ThemeManager::builtInPalette(ThemeManager::kDarkThemeId);
    for (std::size_t i = 0; i < dark.size(); ++i)
        p << "  " << colourRoleName(static_cast<ColourRole>(i)) << ": "
          << colourToHexString(juce::Colour(dark[i])) << "\n";

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
         "]}\n";

    return p;
}

juce::var buildThemeSchema() {
    juce::Array<juce::var> roleEnum;
    for (std::size_t i = 0; i < static_cast<std::size_t>(ColourRole::count); ++i)
        roleEnum.add(juce::String(colourRoleName(static_cast<ColourRole>(i))));

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

    auto* coloursProp = new juce::DynamicObject();
    coloursProp->setProperty("type", "array");
    coloursProp->setProperty("items", juce::var(entry));

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

    juce::Array<juce::var> rootRequired;
    rootRequired.add("name");
    rootRequired.add("base");
    rootRequired.add("colours");

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

    // Fold the model's colours into the file's {roleKey: value} map, keyed on
    // the CANONICAL role name so a loosely-spelled role normalizes on the way
    // in. Accepts the structured [{role, value}] form and the older {key: value}
    // object form interchangeably.
    juce::DynamicObject::Ptr coloursMap = new juce::DynamicObject();
    int recognized = 0;
    const auto addColour = [&](const juce::String& roleKey, const juce::String& value) {
        if (auto role = findColourRole(roleKey); role && parseColourString(value)) {
            coloursMap->setProperty(juce::Identifier(colourRoleName(*role)), value);
            ++recognized;
        }
    };

    const auto coloursVar = obj->getProperty("colours");
    if (auto* array = coloursVar.getArray()) {
        for (const auto& entry : *array)
            if (auto* entryObj = entry.getDynamicObject())
                addColour(entryObj->getProperty("role").toString(),
                          entryObj->getProperty("value").toString());
    } else if (auto* mapObj = coloursVar.getDynamicObject()) {
        for (const auto& kv : mapObj->getProperties())
            addColour(kv.name.toString(), kv.value.toString());
    } else {
        error = "theme JSON is missing a 'colours' array";
        return std::nullopt;
    }

    if (recognized == 0) {
        error = "theme JSON has no recognized colour roles";
        return std::nullopt;
    }

    juce::String name = obj->getProperty("name").toString().trim();
    if (name.isEmpty())
        name = "AI Theme";
    juce::String base = obj->getProperty("base").toString().trim().toLowerCase();
    if (base != "light")
        base = "dark";

    juce::DynamicObject::Ptr clean = new juce::DynamicObject();
    clean->setProperty("schemaVersion", kThemeSchemaVersion);
    clean->setProperty("name", name);
    clean->setProperty("base", base);
    clean->setProperty("colours", juce::var(coloursMap.get()));

    GeneratedTheme out;
    out.name = name.toStdString();
    out.base = base.toStdString();
    out.json = juce::JSON::toString(juce::var(clean.get())).toStdString();
    out.colourCount = recognized;
    return out;
}

}  // namespace magda
