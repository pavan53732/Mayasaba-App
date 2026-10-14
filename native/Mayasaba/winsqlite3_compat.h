#pragma once

// winsqlite3_compat.h
// -----------------------------------------------------------------------------
// Minimal declaration shim for the Windows SDK "winsqlite3" C API.
//
// WHY THIS FILE EXISTS
//   Storage.h/likewise-across-the-project includes <winsqlite3.h> on Windows.
//   The Windows SDK installed on this machine (10.0.26100.0) ships the import
//   library winsqlite3.lib under Lib/10.0.26100.0/um/x64/ but does NOT ship the
//   winsqlite3.h header under Include/10.0.26100.0/um/. The build therefore
//   fails at the #include, not at link time.
//
// WHAT THIS IS
//   Exactly the declarations Storage.cpp consumes (19 entry points, 4 result
//   codes, 3 typedefs). Every prototype matches published SQLite C API 3.x
//   definitions, which have been ABI-stable for years and are identical in
//   winsqlite3. Declarations are wrapped in extern "C" so the linker finds the
//   unmangled exports in winsqlite3.lib.
//
// WHAT THIS IS NOT
//   Not a replacement for the SDK header, and not a bundled SQLite. The real
//   system import library is still what we link against. If a future SDK
//   restores winsqlite3.h, Storage.h prefers it automatically via __has_include
//   and this file stops being compiled.
//
// If you extend SQLite usage in Storage.cpp, add the corresponding declaration
// here in the same commit; do not widen the surface speculatively.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
typedef long long sqlite3_int64;
typedef void (*sqlite3_destructor_type)(void*);
typedef int (*sqlite3_callback)(void*, int, char**, char**);

// Result codes actually tested against by Storage.cpp.
#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_DONE 101
#define SQLITE_TRANSIENT ((sqlite3_destructor_type)-1)

const char* sqlite3_libversion(void);
const char* sqlite3_errmsg(sqlite3*);
sqlite3* sqlite3_db_handle(sqlite3_stmt*);

int sqlite3_open(const char* filename, sqlite3** ppDb);
int sqlite3_open16(const void* filename, sqlite3** ppDb);
int sqlite3_close(sqlite3*);

int sqlite3_exec(sqlite3*, const char* sql, sqlite3_callback, void*, char** errmsg);
int sqlite3_prepare_v2(sqlite3* db, const char* zSql, int nByte,
    sqlite3_stmt** ppStmt, const char** pzTail);
int sqlite3_step(sqlite3_stmt*);
int sqlite3_finalize(sqlite3_stmt*);
void sqlite3_free(void*);

int sqlite3_busy_timeout(sqlite3*, int ms);
int sqlite3_changes(sqlite3*);

int sqlite3_bind_int(sqlite3_stmt*, int, int);
int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int,
    sqlite3_destructor_type);

int sqlite3_column_int(sqlite3_stmt*, int);
int sqlite3_column_bytes(sqlite3_stmt*, int);
const unsigned char* sqlite3_column_text(sqlite3_stmt*, int);

#ifdef __cplusplus
}
#endif
