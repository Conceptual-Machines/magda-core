# Build SQLite as a static library from the FetchContent-populated amalgamation.
# Compile flags chosen for the media-database use case (issue #768):
#   - SQLITE_ENABLE_FTS5      : full-text search over path tokens + tag text
#   - SQLITE_THREADSAFE=2     : multi-thread mode (one connection per thread,
#                               required so the indexer thread doesn't block UI)
#   - SQLITE_DEFAULT_FOREIGN_KEYS=1 : ON FK CASCADE for embedding/tag rows
#   - SQLITE_ENABLE_JSON1     : already default but explicit for clarity
#   - SQLITE_OMIT_DEPRECATED  : trim binary; we only target modern API

add_library(sqlite3 STATIC
    "${sqlite3_SOURCE_DIR}/sqlite3.c"
)

target_include_directories(sqlite3 SYSTEM PUBLIC
    "${sqlite3_SOURCE_DIR}"
)

target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5=1
    SQLITE_THREADSAFE=2
    SQLITE_DEFAULT_FOREIGN_KEYS=1
    SQLITE_ENABLE_JSON1=1
    SQLITE_OMIT_DEPRECATED=1
    SQLITE_USE_URI=1
)

# Vendored third-party C; suppress upstream warnings rather than chasing them.
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /w)
else()
    target_compile_options(sqlite3 PRIVATE -w)
endif()

# pthread + dl on POSIX
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()
