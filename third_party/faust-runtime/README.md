# Vendored Faust runtime headers

Minimal header-only subset of the Faust architecture files, required to compile
AOT-generated DSP classes (e.g. `magda/daw/audio/faust_dsp/magda_drive_generated.hpp`)
without depending on a system Faust install.

## Contents

- `faust/dsp/dsp.h` — abstract `dsp` base class
- `faust/gui/UI.h` — abstract `UI` visitor
- `faust/gui/meta.h` — abstract `Meta` visitor
- `faust/export.h` — `FAUST_API` export macro

## Upstream

- Source: https://github.com/grame-cncm/faust (directory `architecture/faust/`)
- Vendored version: Faust 2.85.5

## License

Faust architecture files are distributed under LGPL 2.1 **with a special
exception** permitting inclusion in larger works under other terms, provided the
architecture section is not modified. See each header's banner and the companion
`LICENSE` file. Flag for proper legal review before shipping.

## Regenerating

If you bump Faust upstream, re-copy only these four headers from the new
release (do not pull the full `architecture/` tree — the rest of it has heavier
dependencies). After copying, also regenerate any checked-in `*_generated.hpp`
files via `scripts/regen-faust.sh`.
