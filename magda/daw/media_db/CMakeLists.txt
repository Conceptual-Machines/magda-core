# Media database (issue #768) — SQLite-backed catalogue of audio samples,
# presets, and clips. Phase A: schema + connection wrapper only.

target_sources(magda_daw PRIVATE
    AudioFeatures.cpp
    MediaDatabase.cpp
    PathRules.cpp
    Scan.cpp
)

target_link_libraries(magda_daw PUBLIC sqlite3)
