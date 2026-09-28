#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <set>

#include "magda/daw/ui/themes/ThemePrompt.hpp"
#include "magda/daw/ui/themes/ThemeSerialization.hpp"
#include "magda/daw/ui/themes/UserTheme.hpp"

// Coverage for the user-defined JSON theme layer (#88): colour/role
// serialization, base inheritance, forgiving-but-safe loading of malformed
// files, and template round-tripping.
class UserThemeTest final : public juce::UnitTest {
  public:
    UserThemeTest() : juce::UnitTest("User Theme Tests", "magda") {}

    static std::size_t idx(magda::ColourRole role) {
        return static_cast<std::size_t>(role);
    }
    static std::size_t idx(magda::SyntaxColourRole role) {
        return static_cast<std::size_t>(role);
    }

    void runTest() override {
        using namespace magda;

        beginTest("Colour strings parse in #RRGGBB and #AARRGGBB forms");
        expect(parseColourString("#FF6B35").has_value());
        expect(parseColourString("#FF6B35")->getARGB() == 0xFFFF6B35);  // 6-digit -> opaque
        expect(parseColourString("FF6B35")->getARGB() == 0xFFFF6B35);   // leading # optional
        expect(parseColourString("0xFF6B35")->getARGB() == 0xFFFF6B35);
        expect(parseColourString("#802E668C")->getARGB() == 0x802E668C);  // 8-digit keeps alpha
        expect(!parseColourString("#GG0000").has_value());                // non-hex
        expect(!parseColourString("#FFF").has_value());                   // wrong length
        expect(!parseColourString("").has_value());

        beginTest("Role lookup ignores case and separators");
        expect(findColourRole("textPrimary") == ColourRole::TEXT_PRIMARY);
        expect(findColourRole("TEXT_PRIMARY") == ColourRole::TEXT_PRIMARY);
        expect(findColourRole("text-primary") == ColourRole::TEXT_PRIMARY);
        expect(findColourRole("Text Primary") == ColourRole::TEXT_PRIMARY);
        expect(findColourRole("accent1") == ColourRole::ACCENT_PRIMARY);
        expect(findColourRole("accent2") == ColourRole::ACCENT_ATTENTION);
        expect(!findColourRole("accentBlue").has_value());  // hue names are gone
        expect(!findColourRole("noSuchRole").has_value());
        expect(!findColourRole("").has_value());
        expect(juce::String(colourRoleName(ColourRole::TEXT_PRIMARY)) == "textPrimary");
        expect(findSyntaxColourRole("dslTokenKeyword") == SyntaxColourRole::DSL_TOKEN_KEYWORD);
        expect(findSyntaxColourRole("DSL_TOKEN_KEYWORD") == SyntaxColourRole::DSL_TOKEN_KEYWORD);

        beginTest("Every canonical role name round-trips and is unique");
        {
            std::set<juce::String> seen;
            for (std::size_t i = 0; i < static_cast<std::size_t>(ColourRole::count); ++i) {
                const auto role = static_cast<ColourRole>(i);
                const juce::String name(colourRoleName(role));
                expect(name.isNotEmpty(), "role " + juce::String((int)i) + " has a name");
                expect(seen.insert(name).second, "name is unique: " + name);
                expect(findColourRole(name) == role, "round-trips: " + name);
            }
            std::set<juce::String> seenSyntax;
            for (std::size_t i = 0; i < static_cast<std::size_t>(SyntaxColourRole::count); ++i) {
                const auto role = static_cast<SyntaxColourRole>(i);
                const juce::String name(syntaxColourRoleName(role));
                expect(name.isNotEmpty());
                expect(seenSyntax.insert(name).second, "syntax name is unique: " + name);
                expect(findSyntaxColourRole(name) == role, "syntax round-trips: " + name);
            }
        }

        beginTest("User theme overrides layer over the base, inheriting the rest");
        {
            const auto& darkBase = ThemeManager::builtInPalette("dark");
            auto file = juce::File::createTempFile(".json");
            file.replaceWithText(R"({
                "schemaVersion": 1,
                "name": "Test Theme",
                "base": "dark",
                "colours": {
                    "background": "#101014",
                    "accent1": "#FF6B35",
                    "bogusRole": "#123456",
                    "textPrimary": "not-a-colour"
                }
            })");

            const auto loaded = loadThemeFile(file);
            file.deleteFile();

            expect(loaded.has_value());
            expect(loaded->name == "Test Theme");
            expect(loaded->base == "dark");
            // Overridden roles take the file's value.
            expect(loaded->palette[idx(ColourRole::BACKGROUND)] == 0xFF101014);
            expect(loaded->palette[idx(ColourRole::ACCENT_PRIMARY)] == 0xFFFF6B35);
            // A role not mentioned inherits the base verbatim.
            expect(loaded->palette[idx(ColourRole::TEXT_SECONDARY)] ==
                   darkBase[idx(ColourRole::TEXT_SECONDARY)]);
            // An unknown key is skipped with a warning, nothing else disturbed.
            // An invalid colour keeps the base value rather than blanking.
            expect(loaded->palette[idx(ColourRole::TEXT_PRIMARY)] ==
                   darkBase[idx(ColourRole::TEXT_PRIMARY)]);
            expect(loaded->warnings.size() == 2, "one unknown key + one bad colour");
        }

        beginTest("base:light inherits the light table");
        {
            const auto& lightBase = ThemeManager::builtInPalette("light");
            auto file = juce::File::createTempFile(".json");
            file.replaceWithText(R"({
                "base": "light",
                "colours": { "accent1": "#123456" },
                "syntaxColours": { "dslTokenKeyword": "#0055AA" }
            })");

            const auto loaded = loadThemeFile(file);
            file.deleteFile();

            expect(loaded.has_value());
            expect(loaded->base == "light");
            expect(loaded->name == loaded->id, "missing name falls back to file stem");
            expect(loaded->palette[idx(ColourRole::ACCENT_PRIMARY)] == 0xFF123456);
            expect(loaded->palette[idx(ColourRole::BACKGROUND)] ==
                   lightBase[idx(ColourRole::BACKGROUND)]);
            expect(loaded->syntaxPalette[idx(SyntaxColourRole::DSL_TOKEN_KEYWORD)] == 0xFF0055AA);
        }

        beginTest("Malformed input never blanks - it fails cleanly to nullopt");
        {
            auto file = juce::File::createTempFile(".json");
            file.replaceWithText("this is not json");
            expect(!loadThemeFile(file).has_value());
            file.replaceWithText("[1, 2, 3]");  // valid JSON, but not an object
            expect(!loadThemeFile(file).has_value());
            file.deleteFile();
            expect(!loadThemeFile(file).has_value(), "non-existent file");
        }

        beginTest("Template round-trips to an identical base palette");
        {
            auto file = juce::File::createTempFile(".json");
            expect(writeThemeTemplate(file, "dark", "My Theme"));

            const auto loaded = loadThemeFile(file);
            file.deleteFile();

            expect(loaded.has_value());
            expect(loaded->name == "My Theme");
            expect(loaded->base == "dark");
            expect(loaded->warnings.empty(), "a generated template has no warnings");
            expect(loaded->palette == ThemeManager::builtInPalette("dark"),
                   "every colour role survives the round-trip");
            expect(loaded->syntaxPalette == ThemeManager::builtInSyntaxPalette("dark"));
        }

        beginTest("Theme schema constrains role to the enum and value to a string");
        {
            const auto schema = buildThemeSchema();
            auto* schemaObj = schema.getDynamicObject();
            expect(schemaObj != nullptr);
            expect(schemaObj->getProperty("type").toString() == "object");
            // colours -> array -> items -> properties -> role -> enum
            auto items = schemaObj->getProperty("properties")["colours"]["items"];
            auto roleEnum = items["properties"]["role"]["enum"];
            auto* enumArray = roleEnum.getArray();
            expect(enumArray != nullptr);
            expect(enumArray->size() == static_cast<int>(ColourRole::count),
                   "every colour role is a valid enum value");
            expect(enumArray->contains(juce::var("accent1")));

            // The syntax vocabulary is a second array with its own role enum.
            auto syntaxItems = schemaObj->getProperty("properties")["syntaxColours"]["items"];
            auto* syntaxEnum = syntaxItems["properties"]["role"]["enum"].getArray();
            expect(syntaxEnum != nullptr);
            expect(syntaxEnum->size() == static_cast<int>(SyntaxColourRole::count));
            expect(syntaxEnum->contains(juce::var("dslTokenKeyword")));
            expect(!syntaxEnum->contains(juce::var("accent1")), "vocabularies stay separate");
        }

        beginTest("Derived syntax palette follows the palette it came from");
        {
            auto palette = ThemeManager::builtInPalette("dark");
            palette[idx(ColourRole::E0)] = 0xFF1A1114;
            palette[idx(ColourRole::TEXT_PRIMARY)] = 0xFFF4E3DA;
            palette[idx(ColourRole::TEXT_DIM)] = 0xFF9A7D74;
            palette[idx(ColourRole::ACCENT_PRIMARY)] = 0xFFE8743B;

            const auto syntax = deriveSyntaxPalette(palette);
            expect(syntax[idx(SyntaxColourRole::EDITOR_BACKGROUND)] == 0xFF1A1114);
            expect(syntax[idx(SyntaxColourRole::EDITOR_DEFAULT_TEXT)] == 0xFFF4E3DA);
            expect(syntax[idx(SyntaxColourRole::DSL_TOKEN_COMMENT)] == 0xFF9A7D74);
            expect(syntax[idx(SyntaxColourRole::DSL_TOKEN_NOTE_NAME)] == 0xFFE8743B);
            // The editor selection is drawn over text, so it stays translucent.
            expect(juce::Colour(syntax[idx(SyntaxColourRole::EDITOR_SELECTION)]).getAlpha() < 255);
            expect(juce::Colour(syntax[idx(SyntaxColourRole::DSL_SELECTION)]).getAlpha() == 255);
            // Status text has to read against whatever the status fill became.
            const juce::Colour statusBg(syntax[idx(SyntaxColourRole::DSL_STATUS_BACKGROUND)]);
            const juce::Colour statusText(syntax[idx(SyntaxColourRole::DSL_STATUS_TEXT)]);
            expect(std::abs(statusBg.getPerceivedBrightness() -
                            statusText.getPerceivedBrightness()) > 0.4f);
        }

        beginTest("Generated themes always carry a complete syntaxColours section");
        {
            std::string err;
            const auto theme = validateGeneratedTheme(
                R"({"name":"Ember","base":"dark",)"
                R"("colours":[{"role":"accent1","value":"#FF6B35"},)"
                R"({"role":"e0","value":"#120C0A"}],)"
                R"("syntaxColours":[{"role":"dslTokenKeyword","value":"#FFAA55"},)"
                R"({"role":"bogusSyntaxRole","value":"#123456"},)"        // dropped
                R"({"role":"dslTokenString","value":"not-a-colour"}]})",  // dropped
                err);
            expect(theme.has_value());
            expect(theme->syntaxCount == 1, "one recognized syntax override");

            auto parsed = juce::JSON::parse(juce::String(theme->json));
            auto* syntax = parsed["syntaxColours"].getDynamicObject();
            expect(syntax != nullptr);
            expect(syntax->getProperties().size() == static_cast<int>(SyntaxColourRole::count),
                   "every syntax role is written out");
            expect(syntax->getProperty("dslTokenKeyword").toString() == "#FFAA55",
                   "the model's choice wins");
            expect(syntax->getProperty("editorBackground").toString() == "#120C0A",
                   "an omitted role derives from the generated palette");
            expect(!syntax->hasProperty("bogusSyntaxRole"));

            // The file round-trips: loading it back reproduces both palettes.
            auto file = juce::File::createTempFile(".json");
            file.replaceWithText(juce::String(theme->json));
            const auto loaded = loadThemeFile(file);
            file.deleteFile();
            expect(loaded.has_value());
            expect(loaded->warnings.empty());
            expect(loaded->syntaxPalette[idx(SyntaxColourRole::DSL_TOKEN_KEYWORD)] == 0xFFFFAA55);
            expect(loaded->syntaxPalette[idx(SyntaxColourRole::EDITOR_BACKGROUND)] == 0xFF120C0A);
        }

        beginTest("A model that skips syntaxColours still gets a derived section");
        {
            std::string err;
            const auto theme = validateGeneratedTheme(
                R"({"name":"Plain","base":"light","colours":{"accent1":"#00AA55"}})", err);
            expect(theme.has_value());
            expect(theme->syntaxCount == 0);

            auto parsed = juce::JSON::parse(juce::String(theme->json));
            auto* syntax = parsed["syntaxColours"].getDynamicObject();
            expect(syntax != nullptr);
            expect(syntax->getProperties().size() == static_cast<int>(SyntaxColourRole::count));
            // Derived from the light base + the one override.
            expect(syntax->getProperty("dslTokenNoteName").toString() == "#00AA55");
        }

        beginTest("Structured [{role,value}] output folds into the file colour map");
        {
            std::string err;
            const auto theme = validateGeneratedTheme(
                R"({"name":"Ember","base":"dark","colours":[)"
                R"({"role":"accent1","value":"#FF6B35"},)"
                R"({"role":"TEXT_PRIMARY","value":"#EDEDED"},)"  // loose spelling normalizes
                R"({"role":"bogus","value":"#123456"},)"         // unknown role dropped
                R"({"role":"background","value":"nope"}])"       // bad colour dropped
                R"(})",
                err);
            expect(theme.has_value());
            expect(theme->name == "Ember");
            expect(theme->base == "dark");
            expect(theme->colourCount == 2, "two recognized, two dropped");
            // The emitted file JSON is the {roleKey: value} map form, keyed on
            // the canonical name (TEXT_PRIMARY -> textPrimary).
            auto parsed = juce::JSON::parse(juce::String(theme->json));
            auto* colours = parsed["colours"].getDynamicObject();
            expect(colours != nullptr);
            expect(colours->hasProperty("accent1"));
            expect(colours->hasProperty("textPrimary"));
            expect(!colours->hasProperty("bogus"));
        }

        beginTest("Validator still accepts the object colour-map form");
        {
            std::string err;
            const auto theme = validateGeneratedTheme(
                R"({"name":"Obj","base":"light","colours":{"accent1":"#00FF00"}})", err);
            expect(theme.has_value());
            expect(theme->base == "light");
            expect(theme->colourCount == 1);
        }

        beginTest("Malformed agent output fails validation");
        {
            std::string err;
            expect(!validateGeneratedTheme("not json", err).has_value());
            expect(!validateGeneratedTheme(R"({"name":"x","base":"dark"})", err).has_value(),
                   "missing colours");
            expect(!validateGeneratedTheme(
                        R"({"name":"x","base":"dark","colours":[{"role":"bogus","value":"#111"}]})",
                        err)
                        .has_value(),
                   "no recognized roles");
        }
    }
};

static UserThemeTest userThemeTest;
