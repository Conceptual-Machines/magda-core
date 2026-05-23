# Localization

MAGDA's interface can be displayed in multiple languages. Simplified Chinese (`zh-CN`) is the first non-English locale; more are added as community translations are completed.

## Switching Language

Set the UI language from **[Preferences](interface/preferences.md) > Language**. The dropdown lists every language MAGDA ships with. A restart is required after switching.

## Contributing Translations

MAGDA's translations are managed on Crowdin:

[https://crowdin.com/project/magda](https://crowdin.com/project/magda)

To contribute:

1. Request access on the Crowdin project page.
2. Once approved, translate strings directly in the browser editor.
3. Approved translations sync back to MAGDA through an automated pull request — no manual file handling required.

New locales appear in **Preferences > Language** once their translations are complete and shipped in a release.

## What is Not Translated

Some strings are deliberately kept in English across every locale:

- Brand names (plugin names, company names, MAGDA's own name)
- File-format designators such as "WAV 24-bit", "FLAC", "MIDI"
- The MAGDA subtitle

Leave these untouched when translating — they are excluded from the Crowdin source strings.
