#ifndef __redisvfs_h
#define __redisvfs_h

#include <hiredis/hiredis.h>

typedef struct sqlite3_vfs RedisVFS;
typedef struct RedisFile RedisFile;

#define REDISVFS_DEFAULT_HOST "127.0.0.1"
#define REDISVFS_DEFAULT_PORT 6379

#define REDISVFS_BLOCKSIZE 1024

// These are mostly arbitrary, but both MAX_PREFIXLEN and MAX_KEYLEN
// must be increased/decreased by the same amount.  Given every file
// operation sends the key over the wire, there is an impact of a larger
// key size
#define REDISVFS_MAX_PREFIXLEN 96 
#define REDISVFS_MAX_KEYLEN 128

/* virtual file that we can use to keep per "file" state */
struct RedisFile {
	// mandatory base class
	sqlite3_file base;

	// Just have a file be the same as a redis connection for now
	redisContext *redisctx;
	
	const char *keyprefix;
	size_t keyprefixlen;
};

/* Prototypes of all sqlite3 file op functions that can be implemented
 */
int redisvfs_close(sqlite3_file *fp);
int redisvfs_read(sqlite3_file *fp, void *buf, int iAmt, sqlite3_int64 iOfst);
int redisvfs_write(sqlite3_file *fp, const void *buf, int iAmt, sqlite3_int64 iOfst);
int redisvfs_truncate(sqlite3_file *fp, sqlite3_int64 size);
int redisvfs_sync(sqlite3_file *fp, int flags);
int redisvfs_fileSize(sqlite3_file *fp, sqlite3_int64 *pSize);
int redisvfs_lock(sqlite3_file *fp, int eLock);
int redisvfs_unlock(sqlite3_file *fp, int eLock);
int redisvfs_checkReservedLock(sqlite3_file *fp, int *pResOut);
int redisvfs_fileControl(sqlite3_file *fp, int op, void *pArg);
int redisvfs_sectorSize(sqlite3_file *fp);
int redisvfs_deviceCharacteristics(sqlite3_file *fp);
/* Methods above are valid for version 1 */
int redisvfs_shmMap(sqlite3_file *fp, int iPg, int pgsz, int, void volatile **pp);
int redisvfs_shmLock(sqlite3_file *fp, int offset, int n, int flags);
void redisvfs_shmBarrier(sqlite3_file *fp);
int redisvfs_shmUnmap(sqlite3_file *fp, int deleteFlag);
/* Methods above are valid for version 2 */
int redisvfs_fetch(sqlite3_file *fp, sqlite3_int64 iOfst, int iAmt, void **pp);
int redisvfs_unfetch(sqlite3_file *fp, sqlite3_int64 iOfst, void *p);
/* Methods above are valid for version 3 */

/* Prototypes of all sqlite3 vfs functions that can be implemented
 */
int redisvfs_open(sqlite3_vfs *vfs, const char *zName, sqlite3_file *f, int flags, int *pOutFlags);
int redisvfs_delete(sqlite3_vfs *vfs, const char *zName, int syncDir);
int redisvfs_access(sqlite3_vfs *vfs, const char *zName, int flags, int *pResOut);
int redisvfs_fullPathname(sqlite3_vfs *vfs, const char *zName, int nOut, char *zOut);
void * redisvfs_dlOpen(sqlite3_vfs*, const char *zFilename);
void redisvfs_dlError(sqlite3_vfs*, int nByte, char *zErrMsg);
void (* redisvfs_dlSym(sqlite3_vfs*,void*, const char *zSymbol))(void);
void redisvfs_dlClose(sqlite3_vfs*, void*);
int redisvfs_randomness(sqlite3_vfs*, int nByte, char *zOut);
int redisvfs_sleep(sqlite3_vfs*, int microseconds);
int redisvfs_currentTime(sqlite3_vfs*, double*);
int redisvfs_getLastError(sqlite3_vfs*, int, char *);
int redisvfs_currentTimeInt64(sqlite3_vfs*, sqlite3_int64*);

int redisvfs_register();
#ifndef STATIC_REDISVFS
int sqlite3_redisvfs_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi);
#endif

#endif // __redisvfs_h
