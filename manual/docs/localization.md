# Localization

MAGDA's interface can be displayed in several languages. Alongside English (`en`), MAGDA ships with community translations into Spanish (`es`), Japanese (`ja`), Russian (`ru`) and Simplified Chinese (`zh`), and more are added as translations are completed.

## Switching Language

Set the UI language in **[Settings > General](interface/preferences.md#language)**, under **Language**, with **UI Language**. The dropdown lists every language MAGDA ships with, by language code. The new language is loaded when you click **Apply** or **OK**; restart MAGDA for it to apply everywhere.

Chinese and Japanese text is drawn a little larger by default. Adjust it with **Localized Font Size** in the same group.

## Contributing Translations

MAGDA's translations are managed on Crowdin:

[https://crowdin.com/project/magda](https://crowdin.com/project/magda)

To contribute:

1. Request access on the Crowdin project page.
2. Once approved, translate strings directly in the browser editor.
3. Approved translations sync back to MAGDA through an automated pull request — no manual file handling required.

New locales appear in the **UI Language** dropdown once their translations are complete and shipped in a release.

## What is Not Translated

Some strings are deliberately kept in English across every locale:

- Brand names (plugin names, company names, MAGDA's own name)
- File-format designators such as "WAV 24-bit", "FLAC", "MIDI"
- The MAGDA subtitle

Leave these untouched when translating — they are excluded from the Crowdin source strings.

Some parts of the interface, such as parts of the AI settings, have not been moved into the translation files yet, so they show in English whatever the language.
