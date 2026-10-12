/* LAME's config.h for MAGDA's build (#3032), in place of the one autoconf would
 * generate. One file for every platform: everything below is C99 and present on
 * the compilers MAGDA supports, so nothing here has to be probed. */
#ifndef LAME_CONFIG_H
#define LAME_CONFIG_H

#define STDC_HEADERS 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1

#define PACKAGE "lame"
#define PROTOTYPES 1
#define LAME_LIBRARY_BUILD 1
#define USE_FAST_LOG 1

/* The library writes MP3 and never reads it: no decoder, no SIMD paths that
 * need NASM or a per-architecture source list. */
#undef HAVE_MPGLIB
#undef HAVE_NASM
#undef HAVE_XMMINTRIN_H

typedef float ieee754_float32_t;
typedef double ieee754_float64_t;
typedef long double ieee854_float80_t;

#if defined(_MSC_VER)
#pragma warning(disable : 4305)
#endif

#endif
