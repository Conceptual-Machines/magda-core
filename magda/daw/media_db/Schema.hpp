// Embedded SQL schema for the media database (issue #768).
//
// The source of truth is prototypes/media_db/src/media_db/schema.sql; this
// header is a verbatim mirror so the C++ runtime ships without a SQL file
// dependency. When the prototype's schema changes, update this string AND
// bump kSchemaVersion in MediaDatabase.hpp.
//
// All blobs use raw little-endian byte layout so std::memcpy is the
// idiomatic way to read embedding vectors back from the DB.

#pragma once

namespace magda::media {

// The schema DDL. user_version is NOT set here: it is owned by the
// migration ladder in MediaDatabase.cpp, which reads PRAGMA user_version to
// gate per-version steps and stamps kSchemaVersion once they apply. Setting
// it here would clobber that gate on every open. foreign_keys is a
// per-connection pragma (defaults OFF, not persisted) so it must run on
// every open; it stays here.
inline constexpr const char* kSchemaSql = R"SQL(
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

CREATE TABLE IF NOT EXISTS media_file (
    id              INTEGER PRIMARY KEY,
    path            TEXT    NOT NULL UNIQUE,
    kind            TEXT    NOT NULL CHECK (kind IN ('audio','preset','clip','progression')),
    format          TEXT    NOT NULL,
    size_bytes      INTEGER NOT NULL,
    mtime_ns        INTEGER NOT NULL,
    content_hash    BLOB,
    indexed_at      INTEGER NOT NULL,
    display_name    TEXT,

    duration_s          REAL,
    sample_rate         INTEGER,
    channels            INTEGER,
    bpm                 REAL,
    key_root            TEXT,
    key_scale           TEXT,
    rms                 REAL,
    spectral_centroid   REAL,
    spectral_flatness   REAL,
    transient_density   REAL,
    key_confidence      REAL,

    -- User overrides (NULL = no override). Read path takes COALESCE(_user,
    -- detected) so the UI sees one effective value. Only the user-edit path
    -- writes these; the indexer never touches them, so user edits survive
    -- re-scans of unchanged files and re-encodes alike.
	    bpm_user            REAL,
	    key_root_user       TEXT,
	    key_scale_user      TEXT,
	    total_beats_user    REAL,
	    beat_mode_user      INTEGER CHECK (beat_mode_user IN (0, 1)),
	    warp_markers_json   TEXT,

    shape   TEXT CHECK (shape  IN ('one-shot','loop','sustained','unknown')),
    family  TEXT CHECK (family IN
                ('drum','bass','lead','pad','keys','guitar','orchestral',
                 'vocal','fx','texture','unknown')),
    tonal   INTEGER CHECK (tonal IN (0, 1)),

    -- Preset rows only. NULL for kind='audio'/'clip'. Mirrors the on-disk
    -- folder split under <PresetsDir>/{Chains,Racks,Devices}/.
    preset_kind TEXT CHECK (preset_kind IN ('chain','rack','device'))
);

CREATE INDEX IF NOT EXISTS idx_media_file_kind   ON media_file (kind);
CREATE INDEX IF NOT EXISTS idx_media_file_format ON media_file (format);
CREATE INDEX IF NOT EXISTS idx_media_file_bpm    ON media_file (bpm);
CREATE INDEX IF NOT EXISTS idx_media_file_key    ON media_file (key_root, key_scale);
CREATE INDEX IF NOT EXISTS idx_media_file_shape  ON media_file (shape);
CREATE INDEX IF NOT EXISTS idx_media_file_family ON media_file (family);
-- Filter-only browse and the two-stage retrieval fallback both want the
-- N most-recently-indexed rows; without this index that path falls back to
-- a full table scan + sort.
CREATE INDEX IF NOT EXISTS idx_media_file_indexed_at ON media_file (indexed_at);

CREATE TABLE IF NOT EXISTS media_embedding (
    file_id         INTEGER NOT NULL REFERENCES media_file(id) ON DELETE CASCADE,
    model_id        TEXT    NOT NULL,
    model_version   TEXT    NOT NULL,
    vector_dim      INTEGER NOT NULL,
    vector_blob     BLOB    NOT NULL,
    PRIMARY KEY (file_id, model_id, model_version)
);

CREATE INDEX IF NOT EXISTS idx_media_embedding_model
    ON media_embedding (model_id, model_version);

CREATE TABLE IF NOT EXISTS media_tag (
    file_id         INTEGER NOT NULL REFERENCES media_file(id) ON DELETE CASCADE,
    tag             TEXT    NOT NULL,
    confidence      REAL    NOT NULL,
    source_model    TEXT    NOT NULL,
    PRIMARY KEY (file_id, tag, source_model)
);

CREATE INDEX IF NOT EXISTS idx_media_tag_tag ON media_tag (tag);

CREATE TABLE IF NOT EXISTS media_metadata (
    file_id         INTEGER NOT NULL REFERENCES media_file(id) ON DELETE CASCADE,
    key             TEXT    NOT NULL,
    value           TEXT    NOT NULL,
    PRIMARY KEY (file_id, key)
);

CREATE VIRTUAL TABLE IF NOT EXISTS media_fts USING fts5(
    path_text, tag_text,
    content='', tokenize='unicode61 remove_diacritics 2'
);
)SQL";

}  // namespace magda::media
