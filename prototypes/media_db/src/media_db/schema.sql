-- Media database schema. Designed to port 1:1 to the C++ implementation.
-- All blobs use raw little-endian byte layout so a C++ memcpy is sufficient.

PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA user_version = 3;

CREATE TABLE IF NOT EXISTS media_file (
    id              INTEGER PRIMARY KEY,
    path            TEXT    NOT NULL UNIQUE,
    kind            TEXT    NOT NULL CHECK (kind IN ('audio','preset','clip')),
    format          TEXT    NOT NULL,                  -- 'wav','aiff','vstpreset',...
    size_bytes      INTEGER NOT NULL,
    mtime_ns        INTEGER NOT NULL,                  -- ns since epoch
    content_hash    BLOB,                              -- xxh64 raw 8-byte LE
    indexed_at      INTEGER NOT NULL,                  -- s since epoch

    -- audio-only columns (NULL otherwise)
    duration_s          REAL,
    sample_rate         INTEGER,
    channels            INTEGER,
    bpm                 REAL,
    key_root            TEXT,                          -- 'C','C#','D',...
    key_scale           TEXT,                          -- 'major','minor'
    rms                 REAL,
    spectral_centroid   REAL,
    spectral_flatness   REAL,                          -- [0,1]; high = noisy/percussive
    transient_density   REAL,                          -- onsets per second
    key_confidence      REAL,                          -- chroma-profile correlation peak [0,1]

    -- Derived categorical labels (computed by indexer from features + tags).
    shape               TEXT CHECK (shape  IN ('one-shot','loop','sustained','unknown')),
    family              TEXT CHECK (family IN
                            ('drum','bass','lead','pad','keys','guitar','orchestral',
                             'vocal','fx','texture','unknown')),
    tonal               INTEGER CHECK (tonal IN (0, 1))  -- bool: 1 if pitched material
);

CREATE INDEX IF NOT EXISTS idx_media_file_kind   ON media_file (kind);
CREATE INDEX IF NOT EXISTS idx_media_file_format ON media_file (format);
CREATE INDEX IF NOT EXISTS idx_media_file_bpm    ON media_file (bpm);
CREATE INDEX IF NOT EXISTS idx_media_file_key    ON media_file (key_root, key_scale);
CREATE INDEX IF NOT EXISTS idx_media_file_shape  ON media_file (shape);
CREATE INDEX IF NOT EXISTS idx_media_file_family ON media_file (family);

-- Embeddings: one row per (file, model). Vectors stored as raw float32 LE.
-- C++ reads with: std::memcpy(out.data(), blob.data(), vector_dim * sizeof(float));
CREATE TABLE IF NOT EXISTS media_embedding (
    file_id         INTEGER NOT NULL REFERENCES media_file(id) ON DELETE CASCADE,
    model_id        TEXT    NOT NULL,                  -- e.g. 'laion/larger_clap_music'
    model_version   TEXT    NOT NULL,                  -- model commit / weights hash
    vector_dim      INTEGER NOT NULL,
    vector_blob     BLOB    NOT NULL,                  -- vector_dim * 4 bytes, float32 LE
    PRIMARY KEY (file_id, model_id, model_version)
);

CREATE INDEX IF NOT EXISTS idx_media_embedding_model ON media_embedding (model_id, model_version);

-- Zero-shot tags scored against the model. Tag text is the natural-language label
-- the model was prompted with. Confidence is cosine similarity in [-1, 1] or
-- a softmaxed probability in [0, 1] depending on source_model convention.
CREATE TABLE IF NOT EXISTS media_tag (
    file_id         INTEGER NOT NULL REFERENCES media_file(id) ON DELETE CASCADE,
    tag             TEXT    NOT NULL,
    confidence      REAL    NOT NULL,
    source_model    TEXT    NOT NULL,
    PRIMARY KEY (file_id, tag, source_model)
);

CREATE INDEX IF NOT EXISTS idx_media_tag_tag ON media_tag (tag);

-- Optional: arbitrary key/value metadata parsed from preset files etc.
CREATE TABLE IF NOT EXISTS media_metadata (
    file_id         INTEGER NOT NULL REFERENCES media_file(id) ON DELETE CASCADE,
    key             TEXT    NOT NULL,
    value           TEXT    NOT NULL,
    PRIMARY KEY (file_id, key)
);

-- Full-text index over path tokens + tag text for keyword search.
-- rowid mirrors media_file.id so we can join cheaply.
CREATE VIRTUAL TABLE IF NOT EXISTS media_fts USING fts5(
    path_text, tag_text,
    content='', tokenize='unicode61 remove_diacritics 2'
);
