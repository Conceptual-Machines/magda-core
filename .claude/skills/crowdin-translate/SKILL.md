---
name: crowdin-translate
description: Manage MAGDA translations via Crowdin - how to add source strings, push sources, push a specific locale's translation via CLI, and the rules about who writes what.
---

# Crowdin / Translations

Translation pipeline for MAGDA. Source of truth is `lang/en.json`; Crowdin holds every other locale.

## Architecture

- `lang/en.json` - English source, the only file edited by hand in this repo for new keys.
- `lang/{two-letter-code}.json` - per-locale files (e.g. `zh.json`). Auto-managed by the Crowdin sync workflow most of the time; can be edited locally for one-off direct uploads.
- `crowdin.yml` at repo root - maps source/translation paths and points at project / token env vars.
- `.github/workflows/crowdin.yml` - GitHub Action that runs on push to `main` / `dev/**` when `lang/en.json` changes, OR on `workflow_dispatch`. Uploads sources, downloads approved translations, opens a PR back into the source branch.

## The standing rule

**By default, don't write non-English translations yourself.** The default workflow is:
1. Edit `lang/en.json` with the new key + English text.
2. Push the branch - GitHub Action uploads sources to Crowdin automatically.
3. Crowdin translators (humans) fill in the other locales.
4. Workflow opens a `l10n_crowdin_*` PR with the new translations.

The default exists because the project has volunteer translators on Crowdin, and silently drafting locales they own bypasses them.

## When Luca asks for a locale, write it

**This is not a rule to re-litigate.** If Luca names locales - "add the missing string in ja/zh/ru", "translate this into Japanese", "fill the gap" - write them and push them. Do not ask whether he is sure, do not offer leaving it to the translators as an alternative, do not raise that you cannot personally verify the language. He knows. He is asking anyway, and being asked the same question every time is the friction this section exists to remove.

Write the translation, say plainly what you wrote and what it means in English so it can be checked at a glance, and push it via the **Push a specific translation** flow below. One line of "here is the back-translation" is useful. A paragraph of hedging is not.

The one thing still worth raising, because it changes what the correct text *is* rather than whether to write it: **when an English source string is rewritten, existing translations often describe the old behaviour, not just the old wording.** Check what the locale text actually says before editing it. Carrying a stale sentence forward with a placeholder swapped in fills the gap on Crowdin while shipping the wrong instruction, which is worse than the gap - it looks finished. Retranslate from the current English instead.

Real example: `mcp.bridge_missing` was rewritten from "Download magda-mcp from the {1} release..." to "{1} could not find {0}. Reinstalling {1} puts it back...". The ja/ru/zh entries still said "download it from the release" long after the English stopped saying so.

## Credentials

Stored in `.env` at the magda-core repo root:

```
CROWDIN_PROJECT_ID=...
CROWDIN_PERSONAL_TOKEN=...
```

The CLI reads them from the environment. Either export them in the shell or inline them on the command:

```bash
set -a; source .env; set +a   # one-shot for the session
```

Don't echo or paste the token value back in chat - it's a personal access token.

## Common operations

### Add a new English source string

1. Edit `lang/en.json`, add the key under the right block (e.g. `"preferences": { "font_scale.label": "Font Size" }`).
2. Reference it from C++ via `tr("preferences.font_scale.label")`.
3. Commit + push the branch. The Crowdin Sync workflow fires automatically because `lang/en.json` changed.

### Trigger the sync workflow manually

```bash
gh workflow run crowdin.yml --ref <branch>
gh run list --workflow=crowdin.yml --limit 5
```

### Push a specific locale's translation via CLI (override path)

When the user explicitly wants a translation in a locale set without waiting on the Crowdin translators:

1. Edit the local locale file (e.g. `lang/zh.json`) to add or update the keys.
2. Load credentials:
   ```bash
   set -a; source .env; set +a
   ```
3. Upload that locale only:
   ```bash
   crowdin upload translations -l zh-CN     # or fr / de / es / etc.
   ```
   Crowdin language codes are the full BCP-47 form (`zh-CN`, `pt-BR`); the CLI maps them to the two-letter local file via `%two_letters_code%` in `crowdin.yml`.

### Remove a key from translation

Drop it from `lang/en.json` and push. The Crowdin Sync workflow removes it from the Crowdin project on the next run, which then deletes it from every locale.

## Gotchas

- **Branch trampling**: both `main` and `dev/*` push sources to the same un-branched Crowdin project. Diverging keys between branches can cause Crowdin to delete translations when a branch with fewer keys pushes after one with more. If you're working on a long-lived `dev/*` and adding/removing keys, expect the other branch's translations to churn until they reconverge.
- **Empty source string**: an empty `en.json` value gives Crowdin nothing to anchor a translation on. Every locale will stay empty regardless of CLI uploads. Always give a source string a non-empty English value.
- **Brand attributions are not translated**: lines like "powered by Tracktion Engine", "made with JUCE", "DSP by FAUST" are intentionally literal strings in C++ - not `tr()` keys - so they stay English in every locale. Don't add them to `en.json`.
- **Don't push the token**: `.env` is gitignored; double-check before committing if you've touched it.
