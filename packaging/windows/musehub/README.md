# MAGDA Windows Installer Source Bundle (MuseHub)

This bundle contains everything needed to build a signed MAGDA Windows
installer. The two MAGDA executables are shipped **unsigned** so you can sign
them with your own certificate before they are packed into the installer.

## Contents

| Item | Notes |
| --- | --- |
| `magda-installer.nsi` | NSIS installer script |
| `MAGDA.exe` | Main application (unsigned, sign in place) |
| `magda_plugin_scanner.exe` | Out-of-process plugin scanner (unsigned, sign in place) |
| `onnxruntime.dll` | ONNX Runtime, Microsoft-signed (used by the sample tagger) |
| `onnxruntime_providers_shared.dll` | ONNX Runtime, Microsoft-signed |
| `mgd_doc_icon.ico` | Document icon for `.mgd` project files |
| `lang/` | Localization JSON files |
| `controllers/` | Hardware controller profiles + Lua scripts |
| `faustlibraries/` | Faust standard libraries (so `import("stdfaust.lib")` resolves) |
| `drumkits/` | Stock drumkit templates seeded on first launch |
| `build-installer.ps1` | Wrapper that runs `makensis` |
| `version.txt` | Release version, read by the wrapper |

The two `onnxruntime` DLLs are already signed by Microsoft and do not need
re-signing. Sign only `MAGDA.exe` and `magda_plugin_scanner.exe`.

## Prerequisites

- NSIS 3.x (`makensis` on PATH, or pass `-Makensis` with a full path)
- Your code-signing toolchain (`signtool` or equivalent)

## Workflow

1. **Extract** this bundle to a working directory. Keep the layout flat: the
   `.nsi` resolves every input relative to its own location, so all files must
   stay siblings (and `lang/` and `controllers/` must keep their subfolders).

2. **Sign the executables in place** (do not move or rename them):

   ```powershell
   signtool sign /fd sha256 /tr <timestamp-url> /td sha256 MAGDA.exe
   signtool sign /fd sha256 /tr <timestamp-url> /td sha256 magda_plugin_scanner.exe
   ```

3. **Build the installer:**

   ```powershell
   .\build-installer.ps1
   ```

   This reads the version from `version.txt` and runs
   `makensis /DVERSION=<version> magda-installer.nsi`, producing
   `MAGDA-<version>-Windows-Setup.exe` in this directory. To override the
   version or point at a specific makensis:

   ```powershell
   .\build-installer.ps1 -Version 0.9.0 -Makensis "C:\Program Files (x86)\NSIS\makensis.exe"
   ```

4. **Sign the installer:**

   ```powershell
   signtool sign /fd sha256 /tr <timestamp-url> /td sha256 MAGDA-<version>-Windows-Setup.exe
   ```

The signed `MAGDA-<version>-Windows-Setup.exe` is the final deliverable.

## Notes

- MAGDA links the dynamic Microsoft Visual C++ runtime. Target machines need
  the Visual C++ 2015-2022 x64 Redistributable installed (present on most
  systems and via Windows Update). It is not bundled in this installer.
- The installer writes to `Program Files\MAGDA`, creates Start Menu and Desktop
  shortcuts, and registers the `.mgd` file association.
