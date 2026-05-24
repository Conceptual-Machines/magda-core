---
name: app-icon
description: Update the MAGDA application icon (dock / taskbar / window icon) from a new source PNG. Use when the user wants to change, swap, or update the app icon, e.g. "update the app icon to Bold-M6". Covers the assets/app_icon.png pipeline, the square-PNG requirement, and rebuilding so JUCE regenerates the .icns/.ico.
---

# Updating the App Icon

`assets/app_icon.png` is the **single source** for the application icon. JUCE
consumes it via `ICON_BIG` / `ICON_SMALL` in `magda/daw/CMakeLists.txt` and
generates the platform icons at configure time:

- **macOS** -> `Icon.icns` (referenced by `CFBundleIconFile` in the app bundle)
- **Windows** -> embedded `.ico`
- **Linux** -> `release.yml` copies `app_icon.png` to the 256x256 hicolor path directly

There is no separate `.icns`/`.ico` to edit and no generation script to run by
hand. Update the icon by replacing `app_icon.png` and rebuilding.

## Conventions

- The source must be a **square PNG, 1024x1024**.
- Each iteration is kept in `assets/` as `Bold-M`, `Bold-M2`, `Bold-M3`, ...
  (the current mark at the time of writing is `Bold-M5`). Add the next mark as
  `Bold-M<N>.png` and keep the prior ones as history. `app_icon.png` is always a
  byte-for-byte copy of the chosen mark.

## Steps

1. Drop the new mark into `assets/` (e.g. `assets/Bold-M6.png`).
2. Point `app_icon.png` at it:

   ```bash
   .claude/skills/app-icon/update-app-icon.sh assets/Bold-M6.png
   ```

   The script validates the PNG is square (1024x1024 by convention), copies it
   over `assets/app_icon.png`, and prints an md5 so you can confirm the swap.
3. Reconfigure and rebuild so the embedded icon regenerates:

   ```bash
   make configure && make debug
   ```

   CMake regenerates `Icon.icns` (macOS) and the `.ico` (Windows) from
   `app_icon.png` **at configure time**, then the build copies the fresh icns
   into the app bundle. A plain `make debug` will **not** pick up the change --
   ninja does not track `app_icon.png` as a dependency, so the reconfigure is
   required. (`make rebuild` also works, since it cleans and reconfigures.)
4. Commit `app_icon.png` and the new source together:

   ```bash
   git add assets/app_icon.png assets/Bold-M6.png
   git commit -m "chore: new app icon (Bold-M6)"
   ```

## Notes

- The icon cannot be verified visually from the build alone (it shows in the
  dock / taskbar at runtime). A clean `make debug` confirms the generation step
  accepted the PNG; the user sees the result when they launch the app.
- Follow the [git](../git/SKILL.md) and [building](../building/SKILL.md) skills
  for branching and build commands.
