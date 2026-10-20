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

// Reports whether the connection is in autocommit mode: non-zero means no transaction is
// open. Used to prove the physical transaction state after a failed rollback rather than
// assuming the rollback succeeded.
int sqlite3_get_autocommit(sqlite3*);

int sqlite3_bind_int(sqlite3_stmt*, int, int);
int sqlite3_bind_int64(sqlite3_stmt*, int, sqlite3_int64);
int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int,
    sqlite3_destructor_type);

int sqlite3_column_int(sqlite3_stmt*, int);
sqlite3_int64 sqlite3_column_int64(sqlite3_stmt*, int);
int sqlite3_column_bytes(sqlite3_stmt*, int);
const unsigned char* sqlite3_column_text(sqlite3_stmt*, int);

typedef struct sqlite3_backup sqlite3_backup;
sqlite3_backup* sqlite3_backup_init(sqlite3* pDest, const char* zDestDb, sqlite3* pSource, const char* zSourceDb);
int sqlite3_backup_step(sqlite3_backup* p, int nPage);
int sqlite3_backup_finish(sqlite3_backup* p);

// Authorizer hook, used as the quarantine backstop: once the connection is quarantined
// this denies writes at the SQLite layer, so a mutation entry point that forgets to call
// the C++ admission check still fails closed. Values are the published, ABI-stable SQLite
// action and result codes.
#ifndef SQLITE_AUTH
#define SQLITE_AUTH 23
#endif
#ifndef SQLITE_DENY
#define SQLITE_DENY 1
#endif
#ifndef SQLITE_CREATE_INDEX
#define SQLITE_CREATE_INDEX 1
#endif
#ifndef SQLITE_CREATE_TABLE
#define SQLITE_CREATE_TABLE 2
#endif
#ifndef SQLITE_CREATE_TEMP_INDEX
#define SQLITE_CREATE_TEMP_INDEX 3
#endif
#ifndef SQLITE_CREATE_TEMP_TABLE
#define SQLITE_CREATE_TEMP_TABLE 4
#endif
#ifndef SQLITE_CREATE_TEMP_TRIGGER
#define SQLITE_CREATE_TEMP_TRIGGER 5
#endif
#ifndef SQLITE_CREATE_TEMP_VIEW
#define SQLITE_CREATE_TEMP_VIEW 6
#endif
#ifndef SQLITE_CREATE_TRIGGER
#define SQLITE_CREATE_TRIGGER 7
#endif
#ifndef SQLITE_CREATE_VIEW
#define SQLITE_CREATE_VIEW 8
#endif
#ifndef SQLITE_DELETE
#define SQLITE_DELETE 9
#endif
#ifndef SQLITE_DROP_INDEX
#define SQLITE_DROP_INDEX 10
#endif
#ifndef SQLITE_DROP_TABLE
#define SQLITE_DROP_TABLE 11
#endif
#ifndef SQLITE_DROP_TEMP_INDEX
#define SQLITE_DROP_TEMP_INDEX 12
#endif
#ifndef SQLITE_DROP_TEMP_TABLE
#define SQLITE_DROP_TEMP_TABLE 13
#endif
#ifndef SQLITE_DROP_TEMP_TRIGGER
#define SQLITE_DROP_TEMP_TRIGGER 14
#endif
#ifndef SQLITE_DROP_TEMP_VIEW
#define SQLITE_DROP_TEMP_VIEW 15
#endif
#ifndef SQLITE_DROP_TRIGGER
#define SQLITE_DROP_TRIGGER 16
#endif
#ifndef SQLITE_DROP_VIEW
#define SQLITE_DROP_VIEW 17
#endif
#ifndef SQLITE_INSERT
#define SQLITE_INSERT 18
#endif
#ifndef SQLITE_UPDATE
#define SQLITE_UPDATE 23
#endif
#ifndef SQLITE_ATTACH
#define SQLITE_ATTACH 24
#endif
#ifndef SQLITE_DETACH
#define SQLITE_DETACH 25
#endif
#ifndef SQLITE_ALTER_TABLE
#define SQLITE_ALTER_TABLE 26
#endif
#ifndef SQLITE_REINDEX
#define SQLITE_REINDEX 27
#endif
#ifndef SQLITE_ANALYZE
#define SQLITE_ANALYZE 28
#endif
#ifndef SQLITE_CREATE_VTABLE
#define SQLITE_CREATE_VTABLE 29
#endif
#ifndef SQLITE_DROP_VTABLE
#define SQLITE_DROP_VTABLE 30
#endif

typedef int (*sqlite3_authorizer_callback)(void* userData, int action, const char* arg1,
    const char* arg2, const char* databaseName, const char* triggerName);
int sqlite3_set_authorizer(sqlite3*, sqlite3_authorizer_callback, void* userData);

#ifdef __cplusplus
}
#endif
