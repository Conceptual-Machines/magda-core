# Media database (issue #768) — SQLite-backed catalogue of audio samples,
# presets, and clips. Phase A: schema + connection wrapper only.

target_sources(magda_daw PRIVATE
    AudioFeatures.cpp
    ClapAudioEncoder.cpp
    ClapTextEncoder.cpp
    MediaDatabase.cpp
    MediaDbContext.cpp
    MediaDbIndexer.cpp
    MediaDbMetadata.cpp
    MediaDbQuery.cpp
    MediaDbZeroShotTags.cpp
    MelSpectrogram.cpp
    PathRules.cpp
    PresetDbIndexer.cpp
    RobertaTokenizer.cpp
    SampleTaggerDownloader.cpp
    Scan.cpp
)

target_link_libraries(magda_daw PUBLIC sqlite3 onnxruntime)
