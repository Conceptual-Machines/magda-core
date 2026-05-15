# Media database (issue #768) — SQLite-backed catalogue of audio samples,
# presets, and clips. Phase A: schema + connection wrapper only.

target_sources(magda_daw PRIVATE
    AudioFeatures.cpp
    ClapAudioEncoder.cpp
    ClapTextEncoder.cpp
    MediaDatabase.cpp
    MediaDbContext.cpp
    MediaDbIndexer.cpp
    MediaDbQuery.cpp
    MelSpectrogram.cpp
    PathRules.cpp
    RobertaTokenizer.cpp
    Scan.cpp
)

target_link_libraries(magda_daw PUBLIC sqlite3 onnxruntime)
