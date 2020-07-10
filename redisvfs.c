#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#include <stdio.h>
#include <string.h>

#include "redisvfs.h"

#define DLOG(fmt,...) fprintf (stderr, "%s@%s[%d]: " fmt "\n", __func__, __FILE__, __LINE__, ##__VA_ARGS__)

int redisvfs_open(sqlite3_vfs *vfs, const char *zName, sqlite3_file *f, int flags, int *pOutFlags) {
DLOG("redisvfs_open zName='%s'",  zName);
	RedisFile *rf = (RedisFile *)f;
	memset(rf, 0, sizeof(RedisFile));

	if (!(flags & SQLITE_OPEN_MAIN_DB)) {
		return SQLITE_CANTOPEN;
	}

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

/* VFS object for sqlite3 */
sqlite3_vfs redis_vfs = {
	2, 0, 1024, 0, /* iVersion, szOzFile, mxPathname, pNext */
	"redisvfs", 0,  /* zName, pAppData */
	redisvfs_open,
	redisvfs_delete,
	redisvfs_access,
	redisvfs_fullPathname,
	0, // redisvfs_dlOpen,  (These are all stubs. See sqlite3_redisvfs_init)
	0, // redisvfs_dlError,
	0, // redisvfs_dlSym,
	0, // redisvfs_dlClose,
	0, // redisvfs_randomness,
	0, // redisvfs_sleep,
	0, // redisvfs_currentTime,
	0, // redisvfs_sleep,
	0, // redisvfs_currentTime,
	0, // redisvfs_getLastError,
	0, // redisvfs_currentTimeInt64
};


/* We want to be able to compile as an sqlite3 extension so make a
 * module load entrypoint*/

#ifdef _WIN32
__declspec(dllexport)
#endif

int sqlite3_redisvfs_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
	int ret; 

	SQLITE_EXTENSION_INIT2(pApi);
	redis_vfs.pAppData = sqlite3_vfs_find(0);
	redis_vfs.szOsFile = sizeof(RedisFile);

	// Get the existing default vfs and pilfer it's OS abstrations
	sqlite3_vfs *defaultVFS = sqlite3_vfs_find(0);
	if (defaultVFS == 0)
		return SQLITE_NOLFS;

	redis_vfs.xDlOpen = defaultVFS->xDlOpen;
	redis_vfs.xDlError = defaultVFS->xDlError;
	redis_vfs.xDlSym = defaultVFS->xDlSym;
	redis_vfs.xDlClose = defaultVFS->xDlClose;
	redis_vfs.xRandomness = defaultVFS->xRandomness;
	redis_vfs.xSleep = defaultVFS->xSleep;
	redis_vfs.xCurrentTime = defaultVFS->xCurrentTime;
	redis_vfs.xGetLastError = defaultVFS->xGetLastError;
	redis_vfs.xCurrentTimeInt64 = defaultVFS->xCurrentTimeInt64;

	// Register outselves as the new default
	ret = sqlite3_vfs_register(&redis_vfs, 1);
	if (ret != SQLITE_OK) {
		fprintf(stderr, "redisvfsinit could not register itself\n");
		return ret;
	}

	return SQLITE_OK_LOAD_PERMANENTLY;
}
