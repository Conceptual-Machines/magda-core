"""Registry of the commercially-clean chord corpora.

We do NOT auto-download or scrape anything — each source has its own terms and
must be fetched manually into data/raw/<key>/. This file documents where each
comes from, its license, the required citation, and the expected on-disk
layout. See findings.md for the licensing rationale.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass
class Source:
    key: str
    name: str
    license: str
    url: str
    citation: str
    layout: str          # what to place under data/raw/<key>/
    commercial_ok: bool


SOURCES = [
    Source(
        key="billboard",
        name="McGill Billboard",
        license="CC0 (public-domain dedication)",
        url="https://ddmal.music.mcgill.ca/research/The_McGill_Billboard_Project_(Chord_Analysis_Dataset)/",
        citation="Burgoyne, Wild & Fujinaga, 'An Expert Ground Truth Set for Audio "
                 "Chord Recognition and Music Analysis', ISMIR 2011.",
        layout="McGill-Billboard/<id>/salami_chords.txt  (Harte-syntax chord annotations)",
        commercial_ok=True,
    ),
    Source(
        key="nottingham",
        name="Nottingham (Jukedeck cleaned)",
        license="Public domain (traditional folk); repo MIT/GPLv3",
        url="https://github.com/jukedeck/nottingham-dataset",
        citation="E. Foxley, Nottingham Music Database.",
        layout="*.abc  (ABC notation with inline \"C\" \"G7\" chord marks)",
        commercial_ok=True,
    ),
    Source(
        key="openewld",
        name="OpenEWLD",
        license="Public-domain subset of EWLD",
        url="https://github.com/00sapo/OpenEWLD",
        citation="Simonetta et al., 'Enhanced Wikifonia Leadsheet Dataset', 2018 "
                 "(OpenEWLD public-domain subset).",
        layout="**/*.mxl or *.musicxml  (MusicXML lead sheets with <harmony>)",
        commercial_ok=True,
    ),
]

# Explicitly excluded (documented so nobody re-adds them):
EXCLUDED = {
    "hooktheory": "ToS forbids scraping / TDM / bulk download / redistribution.",
    "wikifonia_full": "Non-commercial + copyrighted; use OpenEWLD subset instead.",
    "pop909_audio": "MIT covers annotations only; underlying songs copyrighted.",
}


def by_key(key: str) -> Source | None:
    return next((s for s in SOURCES if s.key == key), None)
