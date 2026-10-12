# LAME

LAME 3.100 from https://lame.sourceforge.io, fetched at configure time by the root
`CMakeLists.txt` (`FetchContent_Declare(lame ...)`) and pinned by its SHA-256. Nothing from
the tarball is checked in here; this directory holds only what MAGDA adds to it:

- `CMakeLists.txt` builds `libmp3lame` as the static target `mp3lame`.
- `config.h` stands in for the one LAME's configure script would generate. It is the same
  on every platform and turns off the decoder (mpglib) and the NASM and SSE paths.
- `COPYING` is LAME's licence, copied from the tarball.

The LAME sources themselves are unmodified.

## Licence

LGPL-2.0-or-later (the GNU Library General Public License, version 2 or later). `COPYING`
is the full text.

MAGDA is distributed under GPL-3.0, and the LGPL allows its libraries to be used under
the terms of the GPL, so linking this into a MAGDA binary carries no obligation beyond
the ones GPL-3.0 already imposes.

## Relinking

The library is linked statically. It is built as its own archive, rather than compiled
into an engine target, so that the relink LGPL section 6 provides for stays a matter of
swapping one file:

1. Build MAGDA from source as the README describes.
2. Point FetchContent at a different LAME, either by changing the URL and hash in the
   root `CMakeLists.txt` or by configuring with
   `-DFETCHCONTENT_SOURCE_DIR_LAME=/path/to/lame`.
3. Rebuild. Only `mp3lame` and the binaries that link it are rebuilt.

## Acknowledgement

MP3 encoding in MAGDA uses LAME, https://lame.sourceforge.io.
