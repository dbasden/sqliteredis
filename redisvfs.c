#ifdef STATIC_REDISVFS
#include "sqlite3.h"
#else
#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#endif // STATIC_REDISVFS

#include <stdio.h>
#include <string.h>

#include "redisvfs.h"

// Debugging
#define DLOG(fmt,...) printf("%s@%s[%d]: " fmt "\n", __func__, __FILE__, __LINE__, ##__VA_ARGS__)

// Reference the parent VFS that we reference in pAppData
#define PARENT_VFS(vfs) ((sqlite3_vfs *)(vfs->pAppData))

int redisvfs_open(sqlite3_vfs *vfs, const char *zName, sqlite3_file *f, int flags, int *pOutFlags) {
DLOG("redisvfs_open zName='%s'",  zName);
	RedisFile *rf = (RedisFile *)f;
	memset(rf, 0, sizeof(RedisFile));

	if (!(flags & SQLITE_OPEN_MAIN_DB)) {
		return SQLITE_CANTOPEN;
	}

	return !SQLITE_OK;
	return SQLITE_OK;
}

int redisvfs_delete(sqlite3_vfs *vfs, const char *zName, int syncDir) {
DLOG("redisvfs_delete zName='%s'",  zName);
	// FIXME: Can implement
	return SQLITE_IOERR_DELETE;
}
int redisvfs_access(sqlite3_vfs *vfs, const char *zName, int flags, int *pResOut) {
DLOG("redisvfs_access zName='%s'",  zName);
	if (pResOut != 0)
		*pResOut = 0;
	return SQLITE_OK;
}
int redisvfs_fullPathname(sqlite3_vfs *vfs, const char *zName, int nOut, char *zOut) {
DLOG("redisvfs_fullPathname zName='%s'",  zName);
	sqlite3_snprintf(nOut, zOut, "%s", zName); // effectively strcpy with sqlite3 mm
	return SQLITE_OK;
}

//
// These we don't implement but just pass through to the existing default VFS
// As they are more OS abstraction than FS abstraction it doesn't affect us
//
// Note: Turns out that you really need to pass the parent VFS struct referennce into it's own
//       calls not our VFS struct ref. This is obvious in retrospect.
//
#define VFS_SHIM_CALL(_callname,_vfs,...) PARENT_VFS(_vfs)->_callname(PARENT_VFS(_vfs), __VA_ARGS__)

void * redisvfs_dlOpen(sqlite3_vfs *vfs, const char *zFilename) {
	return VFS_SHIM_CALL(xDlOpen, vfs, zFilename);
}
void redisvfs_dlError(sqlite3_vfs *vfs, int nByte, char *zErrMsg) {
	VFS_SHIM_CALL(xDlError, vfs, nByte, zErrMsg);
}
void (* redisvfs_dlSym(sqlite3_vfs *vfs, void *pHandle, const char *zSymbol))(void) {
	return VFS_SHIM_CALL(xDlSym, vfs, pHandle, zSymbol);
}
void redisvfs_dlClose(sqlite3_vfs *vfs, void *pHandle) {
	VFS_SHIM_CALL(xDlClose, vfs, pHandle);
}
int redisvfs_randomness(sqlite3_vfs *vfs, int nByte, char *zOut) {
	return VFS_SHIM_CALL(xRandomness, vfs, nByte, zOut);
}
int redisvfs_sleep(sqlite3_vfs *vfs, int microseconds) {
	return VFS_SHIM_CALL(xSleep, vfs, microseconds);
}
int redisvfs_currentTime(sqlite3_vfs *vfs, double *prNow) {
	return VFS_SHIM_CALL(xCurrentTime, vfs, prNow);
}
int redisvfs_getLastError(sqlite3_vfs *vfs, int nBuf, char *zBuf) {
	return VFS_SHIM_CALL(xGetLastError, vfs, nBuf, zBuf);
}
int redisvfs_currentTimeInt64(sqlite3_vfs *vfs, sqlite3_int64 *piNow) {
	return VFS_SHIM_CALL(xCurrentTimeInt64, vfs, piNow);
}

/* VFS object for sqlite3 */
sqlite3_vfs redis_vfs = {
	2, 0, 1024, 0, /* iVersion, szOzFile, mxPathname, pNext */
	"redisvfs", 0,  /* zName, pAppData */
	redisvfs_open,
	redisvfs_delete,
	redisvfs_access,
	redisvfs_fullPathname,
	redisvfs_dlOpen,
	redisvfs_dlError,
	redisvfs_dlSym,
	redisvfs_dlClose,
	redisvfs_randomness,
	redisvfs_sleep,
	redisvfs_currentTime,
	redisvfs_getLastError,
	redisvfs_currentTimeInt64
};



int redisvfs_register() {
	int ret;

	redis_vfs.szOsFile = sizeof(RedisFile);

	// Get the existing default vfs and pilfer it's OS abstrations
	// This will normally work as long as the (previously) default
	// vfs is not unloaded.
	//
	// If the existing default vfs is trying to do the same thing
	// things may get weird, but sqlite3 concrete implementations
	// out of the box will not do that (only vfs shim layers like vfslog)
	//
	//
	sqlite3_vfs *defaultVFS = sqlite3_vfs_find(0);
	if (defaultVFS == 0)
		return SQLITE_NOLFS;

	// Use our pAppData opaque pointer to store a reference to the
	// underlying VFS.
	redis_vfs.pAppData = (void *)defaultVFS;

	// Register outselves as the new default
	ret = sqlite3_vfs_register(&redis_vfs, 1);
	if (ret != SQLITE_OK) {
		fprintf(stderr, "redisvfsinit could not register itself\n");
		return ret;
	}

	return SQLITE_OK;
}


#ifndef STATIC_REDISVFS

#ifdef _WIN32
__declspec(dllexport)
#endif

/* If we are compiling as an sqlite3 extension make a module load entrypoint
 *
 * sqlite3_SONAME_init is a well-known symbol
 */
int sqlite3_redisvfs_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
	int ret;

	SQLITE_EXTENSION_INIT2(pApi);
	ret = redisvfs_register();
	return (ret == SQLITE_OK) ? SQLITE_OK_LOAD_PERMANENTLY : ret;
}

#endif
