#ifndef __redisvfs_h
#define __redisvfs_h

typedef struct sqlite3_vfs RedisVFS;
typedef struct RedisFile RedisFile;

/* virtual file that we can use to keep per "file" state */
struct RedisFile {
	sqlite3_file base;
};

/* Prototypes of all sqlite3 vfs functions that can be implemented
 */
int redisvfs_open(sqlite3_vfs *vfs, const char *zName, sqlite3_file *f, int flags, int *pOutFlags);
int redisvfs_delete(sqlite3_vfs *vfs, const char *zName, int syncDir);
int redisvfs_access(sqlite3_vfs *vfs, const char *zName, int flags, int *pResOut);
int redisvfs_fullPathname(sqlite3_vfs *vfs, const char *zName, int nOut, char *zOut);
#if 0
/* 
 * These calls we don't need (or want) to implement
 * (Most of the "VFS" is OS abstraction rather than than VFS so the
 * default VFS implementations will be fine)
 */
void *redisvfs_dlOpen(sqlite3_vfs*, const char *zFilename);
void redisvfs_dlError(sqlite3_vfs*, int nByte, char *zErrMsg);
void * redisvfs_dlSym(sqlite3_vfs*,void*, const char *zSymbol);
void redisvfs_dlClose(sqlite3_vfs*, void*);
int redisvfs_randomness(sqlite3_vfs*, int nByte, char *zOut);
int redisvfs_sleep(sqlite3_vfs*, int microseconds);
int redisvfs_currentTime(sqlite3_vfs*, double*);
int redisvfs_getLastError(sqlite3_vfs*, int, char *);

// This is only implemented in sqlite versions that have version 2 of the
// struct. (from comments in sqlite3.h)
int redisvfs_currentTimeInt64(sqlite3_vfs*, sqlite3_int64*);

// These are only implemented in sqlite versions that have version 3 of the
// struct. (from comments in sqlite3.h)
int redisvfs_setSystemCall(sqlite3_vfs*, const char *zName, sqlite3_syscall_ptr);
sqlite3_syscall_ptr redisvfs_getSystemCall(sqlite3_vfs*, const char *zName);
const char *(*redisvfs_nextSystemCall)(sqlite3_vfs*, const char *zName);
#endif

#endif // __redisvfs_h
