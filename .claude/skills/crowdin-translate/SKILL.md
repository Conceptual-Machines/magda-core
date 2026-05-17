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

**Don't write non-English translations yourself.** The default workflow is:
1. Edit `lang/en.json` with the new key + English text.
2. Push the branch - GitHub Action uploads sources to Crowdin automatically.
3. Crowdin translators (humans) fill in the other locales.
4. Workflow opens a `l10n_crowdin_*` PR with the new translations.

This is the rule because Luca doesn't speak the target locales and the project has volunteer translators on Crowdin. Drafting Chinese / French / etc. yourself bypasses them.

## When the user explicitly overrides

The user may ask to push a specific locale's translation directly via the CLI - usually when they have a string already verified or want to ship something without waiting for the translator pipeline. In that case, follow the **Push a specific translation** flow below.

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
