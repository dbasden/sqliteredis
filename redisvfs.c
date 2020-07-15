#ifdef STATIC_REDISVFS
#include "sqlite3.h"
#else
#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#endif // STATIC_REDISVFS

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <hiredis/hiredis.h>

#include "redisvfs.h"

// Debugging
//
// Reference the parent VFS that we reference in pAppData
#define PARENT_VFS(vfs) ((sqlite3_vfs *)(vfs->pAppData))

/* keyspace helpers */

/* pre: outkeyname is exactly REDISVFS_MAX_KEYLEN+1 bytes */
static int get_blockkey(RedisFile *rf, int64_t offset, char *outkeyname) {
    // (REDISVFS_MAX_KEYLEN - REDISVFS_MAX_PREFIXLEN) = 32 characters
    // to encode the block number.  1 character for a delimiter, plus
    // 14 bytes for hex encoding any block number for a 64 bit offset
    // (given 1024 byte blocks) meeans we still have 17 bytes free
    // if we need to encode something else in the keyname later on.
    int blocknum = offset / REDISVFS_BLOCKSIZE;
    int written = snprintf(outkeyname, REDISVFS_KEYBUFLEN, "%s:%x", rf->keyprefix, blocknum);

    assert(written < REDISVFS_KEYBUFLEN);
    return written;
}

/* emulate file size tracking by storing the max value stored
 * pre: outkeyname is exactly REDISVFS_MAX_KEYLEN+1 bytes */
static int get_filesizekey(RedisFile *rf, char *outkeyname) {
    int written = snprintf(outkeyname, REDISVFS_KEYBUFLEN, "%s:filelen", rf->keyprefix);
    assert(written < REDISVFS_KEYBUFLEN);
    return written;
}

static inline int64_t _start_of_block(int64_t offset) {
        return offset - (offset % REDISVFS_BLOCKSIZE);
}
static inline int64_t _start_of_next_block(int64_t offset) {
        return _start_of_block(offset) + REDISVFS_BLOCKSIZE;
}

// Only used if we nest too much evil macro expansion of the debugreply macros
static inline void redis_debugreplyarray (const redisReply *reply) {
    for (int i=0; i<reply->elements; ++i) redis_debugreply(reply->element[i]);
}

/* Make it easier to play fast and loose with redis pipelining */
static int redis_discard_replies(RedisFile *rf, int ndiscards) {
    for (int i=0; i<ndiscards; ++i) {
        redisReply *reply;
        if (redisGetReply(rf->redisctx, (void **)&reply) != REDIS_OK)
            return REDIS_ERR;
#if 0
        DLOG("DISCARDING REPLY:");
        redis_debugreply(reply);
#endif
        freeReplyObject(reply);
    }
    return REDIS_OK;
}


/* redis blockio */

static int redis_queuecmd_whole_block_read(RedisFile *rf, const sqlite3_int64 offset) {
    assert((offset % REDISVFS_BLOCKSIZE) == 0);

    char key[REDISVFS_KEYBUFLEN];
    int keylen = get_blockkey(rf, offset, key);

    return redisAppendCommandArgv(rf->redisctx, 2,
            (const char *[]){ "GET", key },
            (const size_t[]){ 3, keylen });
}

/* caller is saying filesize is at least 'minfilesize'
 * appends 2 commands */
static int redis_queue_increase_filesize_to(RedisFile *rf, int64_t minfilesize) {
    char key[REDISVFS_KEYBUFLEN];
    get_filesizekey(rf, key);

    // We store filesize in an ordered set
    //
    // To get the filesize we peek the item with the highest cardinality
    //
    // If we have a lower bound on filesize, we just add the bound to the
    // ordered set, and then remove (trim) all but the highest cardinality item.
    //
    // As the trim is both optional, and safe to rerun, there is no race with
    // multiple interleved calls
    int ret = redisAppendCommand(rf->redisctx, "ZADD %s %d %d", key, minfilesize, minfilesize);
    if (ret == REDIS_ERR)
        return ret;

    // Trim to only largest entry
    return redisAppendCommand(rf->redisctx, "ZREMRANGEBYRANK %s %d %d", key, 0, -2);

}
static int redis_consume_increase_filesize_to(RedisFile *rf) {
    return redis_discard_replies(rf, 2);
}

// WARNING: Don't use in pipeline
// Returns 0 if the file doesnt exist
static int64_t redis_get_filesize(RedisFile *rf) {
    char key[REDISVFS_KEYBUFLEN];
    get_filesizekey(rf, key);

    redisReply *reply;
    if ((reply = redisCommand(rf->redisctx, "ZREVRANGE %s 0 0", key)) == NULL) {
            return REDIS_ERR;
    }
    redis_debugreply(reply);

    int64_t filesize;
    if (reply->type != REDIS_REPLY_ARRAY) {
            filesize = -1;
    } else if (reply->elements == 0) {
            filesize = 0;
    } else if (reply->element[0]->type != REDIS_REPLY_STRING) {
            filesize = -1;
    } else {
            filesize = atoll(reply->element[0]->str);
    }
    freeReplyObject(reply);
    return filesize;
}

static int64_t redis_force_set_filesize(RedisFile *rf, int64_t filesize) {
    assert(filesize >= 0);
    char key[REDISVFS_KEYBUFLEN];
    get_filesizekey(rf, key);

    if (redisAppendCommand(rf->redisctx, "MULTI") == REDIS_ERR)
        return REDIS_ERR;
    if (redisAppendCommand(rf->redisctx, "DEL %s", key) == REDIS_ERR)
        return REDIS_ERR;
    if (redis_queue_increase_filesize_to(rf, filesize) == REDIS_ERR)
        return REDIS_ERR;
    if (redisAppendCommand(rf->redisctx, "EXEC") == REDIS_ERR)
        return REDIS_ERR;

    // TODO: Actually check return codes.  Ignore if DEL fails.
    if (redis_discard_replies(rf, 2) == REDIS_ERR)  // MULTI,DEL
        return REDIS_ERR;
    if (redis_consume_increase_filesize_to(rf))
        return REDIS_ERR;
    if (redis_discard_replies(rf, 1) == REDIS_ERR)  // EXEC
        return REDIS_ERR;

    return REDIS_OK;
}


// NO PIPELINING HANDLED.   Don't call it unless you are
// sure  there are no other commands sent before or after
// without appropriate compartmentalisation
//
// FIXME: if we're not going to pipeline, just use redisCommand
static bool redis_does_block_exist(RedisFile *rf, int64_t offset) {
    assert((offset % REDISVFS_BLOCKSIZE) == 0);

    char key[REDISVFS_KEYBUFLEN];
    int keylen = get_blockkey(rf, offset, key);

    if (redisAppendCommandArgv(rf->redisctx, 2,
           (const char *[]){ "EXISTS", key },
           (const size_t[]){ 6, keylen }) != REDIS_OK) {
        return false;
    };
    bool exists = false;
    redisReply *reply;
    if (redisGetReply(rf->redisctx, (void**)&reply) == REDIS_OK) {
        if (reply->type == REDIS_REPLY_INTEGER) {
            DLOG("Redis INT: %lld", reply->integer);
            exists = reply->integer == 1;
        } else { DLOG("Redis something else"); }
    }
    return exists;
}

/* pre: buf is >= REDISVFS_BLOCKSIZE */
static int redis_queuecmd_whole_block_write(RedisFile *rf, int64_t offset, const char *buf) {
    assert((offset % REDISVFS_BLOCKSIZE) == 0);

    char key[REDISVFS_KEYBUFLEN];
    int keylen = get_blockkey(rf, offset, key);

    return redisAppendCommandArgv(rf->redisctx, 3,
            (const char *[]){ "SET", key, buf },
            (const size_t[]){ 3, keylen, REDISVFS_BLOCKSIZE });
}

static int redis_queuecmd_partial_block_read(RedisFile *rf, int64_t offset, int64_t len) {
    // GETRANGE range is inclusive of first and last indices
    int64_t block_first = offset % REDISVFS_BLOCKSIZE;
    int64_t block_last = block_first + len - 1;

    assert(len > 0);
    assert(block_last < REDISVFS_BLOCKSIZE);


    char key[REDISVFS_KEYBUFLEN];
    get_blockkey(rf, offset, key);

    return redisAppendCommand(rf->redisctx, "GETRANGE %s %d %d", key, block_first, block_last);
}

static int redis_queuecmd_partial_block_write(RedisFile *rf, int64_t offset, const char *buf, int64_t len) {
    assert(len > 0);
    int64_t block_first = offset % REDISVFS_BLOCKSIZE;
    assert((block_first + len) <= REDISVFS_BLOCKSIZE);

    char key[REDISVFS_KEYBUFLEN];
    int keylen = get_blockkey(rf, offset, key);
    char block_offsetstr[32];
    snprintf(block_offsetstr, 32, "%ld", block_first);

    DLOG("SETRANGE %s %s ...(len %ld)",key, block_offsetstr, len);
    return redisAppendCommandArgv(rf->redisctx, 4,
            (const char *[]){ "SETRANGE", key, block_offsetstr, buf },
            (const size_t[]){ 8, keylen, strlen(block_offsetstr), len });
}


static int redis_queuecmd_delete_block(RedisFile *rf, sqlite3_int64 offset) {
    assert((offset % REDISVFS_BLOCKSIZE) == 0);

    char key[REDISVFS_KEYBUFLEN];
    int keylen = get_blockkey(rf, offset, key);

    return redisAppendCommandArgv(rf->redisctx, 2,
            (const char *[]){ "DEL", key },
            (const size_t[]){ 3, keylen });
}

/*
 * File API implementation
 *
 * These all work on a specific open file. pointers to these
 * are rolled up in an sqlite3_io_methods struct, which is referrenced
 * by each sqlite3_file * generated by redisvfs_open
 */

int redisvfs_close(sqlite3_file *fp) {
    DLOG("disconnecting from redis");
    RedisFile *rf = (RedisFile *)fp;
    if (rf->redisctx) {
        redisFree(rf->redisctx);
        rf->redisctx = 0;
    }
    return SQLITE_OK;
}
int redisvfs_write(sqlite3_file *fp, const void *buf, int iAmt, sqlite3_int64 iOfst) {
    RedisFile *rf = (RedisFile *)fp;
    DLOG("(fp=%p prefix='%s' offset=%lld len=%d)", rf, rf->keyprefix, iOfst, iAmt);

    int64_t write_startp = iOfst;
    int64_t write_endp = iOfst+iAmt;

    // Queue writes
    for (int64_t leftp=write_startp; leftp<write_endp; leftp=_start_of_next_block(leftp)) {
            int64_t blkstart = _start_of_block(leftp);
            int64_t blknext = _start_of_next_block(leftp);
            int64_t rightp = (write_endp > blknext) ? blknext : write_endp;

            const char *bufleft = (const char *)buf + (leftp - write_startp);

            if ((leftp == blkstart) && (rightp == blknext)) {
                    assert((rightp-leftp) == REDISVFS_BLOCKSIZE);
                    DLOG("%s full block write @ %ld", rf->keyprefix, leftp);
                    if( redis_queuecmd_whole_block_write(rf, leftp, bufleft) == REDIS_ERR) {
                            return SQLITE_IOERR;
                            // TODO:  use redis check error and bubble up to sql last error
                    }
            } else {
                    DLOG("%s Partial block write [%ld..%ld)", rf->keyprefix, leftp,rightp);
                    if( redis_queuecmd_partial_block_write(rf, leftp, bufleft, rightp-leftp) == REDIS_ERR) {
                         return SQLITE_IOERR;
                    }
            }
    }

    // Execute write and check responses
    int64_t successfully_written = 0;
    int return_status = SQLITE_OK;

    for (int64_t leftp=write_startp; leftp<write_endp; leftp=_start_of_next_block(leftp)) {
            int64_t blknext = _start_of_next_block(leftp);
            int64_t rightp = (write_endp > blknext) ? blknext : write_endp;

            redisReply *reply;

            DLOG("checking reply for [%ld..%ld)", leftp,rightp);
            if (redisGetReply(rf->redisctx, (void **)&reply) == REDIS_ERR) {
                DLOG("ERROR: redisGetReply: %s", rf->redisctx->errstr);
                return SQLITE_IOERR_WRITE;
            }

            redis_debugreply(reply);
            // FIXME: check reply before incrementing written
            if (return_status == SQLITE_OK) {
                    successfully_written += rightp-leftp;
            }
            freeReplyObject(reply);
    }

    // write barrier (guaranteed for single server) then update filesize
    // TODO: Make write barrier optional and remove SAFE_APPEND guarantee
    // if we need  to increase write performance
    // FIXME : Add write barrier cross-cluster
    if (successfully_written > 0) {
        int64_t minfilesize = iOfst + successfully_written;
        // I think we could do this earlier on there without the extra RTT, but
        // that assumes all the writes succeeded.
        if (redis_queue_increase_filesize_to(rf, minfilesize) != REDIS_OK)
            return_status = SQLITE_IOERR_WRITE;
        if (redis_consume_increase_filesize_to(rf) != REDIS_OK)
            return_status = SQLITE_IOERR_WRITE;
    }
    DLOG("written %ld/%d.  Returning %s\n", successfully_written, iAmt,
            return_status == SQLITE_OK ? "SQLITE_OK" : "NOT OK");
    return return_status;
}

int redisvfs_read(sqlite3_file *fp, void *buf, int iAmt, sqlite3_int64 iOfst) {
    RedisFile *rf = (RedisFile *)fp;
    DLOG("(fp=%p prefix='%s' offset=%lld len=%d)", rf, rf->keyprefix, iOfst, iAmt);

    int64_t read_startp = iOfst;
    int64_t read_endp = iOfst+iAmt;

    // Queue reads
    for (int64_t leftp=read_startp; leftp<read_endp; leftp=_start_of_next_block(leftp)) {
            int64_t blkstart = _start_of_block(leftp);
            int64_t blknext = _start_of_next_block(leftp);
            int64_t rightp = (read_endp > blknext) ? blknext : read_endp;

            if ((leftp == blkstart) && (rightp == blknext)) {
                    DLOG("full block read");
                    if( redis_queuecmd_whole_block_read(rf, blkstart) == REDIS_ERR) {
                            return SQLITE_IOERR;
                            // TODO:  use redis check error and bubble up to sql last error
                    }
            } else {
                    DLOG("Partial block read [%ld..%ld)", leftp,rightp);
                    if( redis_queuecmd_partial_block_read(rf, leftp, rightp-leftp) == REDIS_ERR) {
                         return SQLITE_IOERR;
                    }
            }
    }

    // sqlite3 requires short reads be zero-filled for the rest of the buffer,
    // and says database corruption will otherwise occur
    memset(buf, 0, iAmt); /* This will cover the requirement but only required in the case of a short read */

    int64_t successfully_read = 0;

    // We track this becausei we need to continue draining the
    // connection of command responses regardless of if the commands
    // were successful.
    int returnStatus = SQLITE_OK;

    // Execute and read responses
    for (int64_t leftp=read_startp; leftp<read_endp; leftp=_start_of_next_block(leftp)) {
            int64_t blknext = _start_of_next_block(leftp);
            int64_t rightp = (read_endp > blknext) ? blknext : read_endp;

            redisReply *reply;

            DLOG("fetching next (sub)block from redis stream");
            if (redisGetReply(rf->redisctx, (void **)&reply) == REDIS_ERR) {
                DLOG("ERROR: redisGetReply: %s", rf->redisctx->errstr);
                return SQLITE_IOERR_READ;
            }
            if (reply->type == REDIS_REPLY_STRING) {
                DLOG("Redis STRING: %lu bytes", reply->len);
                // The read counter can only increment if any previous
                // reads were successful and not short
                if (returnStatus == SQLITE_OK) {
                    if (reply->len > rightp-leftp) {
                        DLOG("read reply overflow");
                        return SQLITE_IOERR_READ;
                    }
                    if (reply->len < rightp-leftp) {
                        DLOG("short read");
                        returnStatus = SQLITE_IOERR_SHORT_READ;
                    }
                    if (reply->len > 0) {
                        memcpy(buf+(leftp-read_startp), reply->str, reply->len);
                    }
                    successfully_read += rightp-leftp;
                }
                else {
                    DLOG("Dropping because lack of continuity");
                }
            }
            else if (reply->type == REDIS_REPLY_NIL) {
                DLOG("Block not found");
                if (returnStatus == SQLITE_OK)
                    returnStatus = SQLITE_IOERR_SHORT_READ;
            }
            else {
                DLOG("wrong reply type. Bailing straight away");
                return SQLITE_IOERR_READ;
            }

            freeReplyObject(reply);
    }
    if ((returnStatus == SQLITE_IOERR_SHORT_READ) && (successfully_read == 0)) {
        returnStatus = SQLITE_IOERR_READ;
    }
    assert(!SQLITE_OK || (successfully_read == iAmt));
    return returnStatus;
}
int redisvfs_truncate(sqlite3_file *fp, sqlite3_int64 size) {
    sqlite3_int64 existing_size;
    if (redisvfs_fileSize(fp, &existing_size) == REDIS_ERR)
        return SQLITE_ERROR;
    if (existing_size < size)
        return SQLITE_ERROR;
    if (redis_force_set_filesize((RedisFile *)fp, size) == REDIS_ERR)
        return SQLITE_ERROR;
    return SQLITE_OK;
}
int redisvfs_sync(sqlite3_file *fp, int flags) {
    DLOG("stub");
    // Noop. All our writes are synchronous.
    // TODO: We can put a hard barrier in here to redis and block if we really want
    return SQLITE_OK;
}
int redisvfs_fileSize(sqlite3_file *fp, sqlite3_int64 *pSize) {
    RedisFile *rf = (RedisFile *)fp;
    DLOG("get_filesize(%s)", rf->keyprefix);
    *pSize = redis_get_filesize(rf);
    DLOG("... get_filesize(%s) = %lld", rf->keyprefix, *pSize);
    return (*pSize >= 0) ? SQLITE_OK : SQLITE_ERROR;
}
int redisvfs_lock(sqlite3_file *fp, int eLock) {
    DLOG("stub flock(%s,%d)",((RedisFile *)fp)->keyprefix,eLock);
    return SQLITE_OK; // FIXME: Implement
}
int redisvfs_unlock(sqlite3_file *fp, int eLock) {
    DLOG("stub funlock(%s,%d)",((RedisFile *)fp)->keyprefix,eLock);
    return SQLITE_OK; // FIXME: Implement
}
int redisvfs_checkReservedLock(sqlite3_file *fp, int *pResOut) {
    DLOG("stub");
    return !SQLITE_OK; // FIXME: Implement
}
int redisvfs_fileControl(sqlite3_file *fp, int op, void *pArg) {
    if ( op == SQLITE_FCNTL_VFSNAME ) {
        DLOG("SQLITE_FCNTL_VFSNAME");
        char **out = (char **)pArg;
        *out = sqlite3_mprintf("redisvfs");
        return SQLITE_OK;
    }
    DLOG("No idea what %d is", op);
    return SQLITE_NOTFOUND;
}
int redisvfs_sectorSize(sqlite3_file *fp) {
    DLOG("stub");
    return REDISVFS_BLOCKSIZE;
}
int redisvfs_deviceCharacteristics(sqlite3_file *fp) {
    DLOG("entry");
    // Describe ordering and consistency guarantees that we
    // can provide.  See sqlite3.h
    // TODO implement SQLITE_IOCAP_BATCH_ATOMIC
    // TODO: If we remove SQLITE_IOCAP_ATOMIC and replace with caveated
    // atomic op flags, we can remove transactions with redis entirely
    return ( SQLITE_IOCAP_ATOMIC | SQLITE_IOCAP_SAFE_APPEND |
        SQLITE_IOCAP_SEQUENTIAL | SQLITE_IOCAP_POWERSAFE_OVERWRITE |
        SQLITE_IOCAP_UNDELETABLE_WHEN_OPEN );
}

#if 0
/* Methods above are valid for version 1 */
int redisvfs_shmMap(sqlite3_file *fp, int iPg, int pgsz, int, void volatile **pp);
int redisvfs_shmLock(sqlite3_file *fp, int offset, int n, int flags);
void redisvfs_shmBarrier(sqlite3_file *fp);
int redisvfs_shmUnmap(sqlite3_file *fp, int deleteFlag);
/* Methods above are valid for version 2 */
int redisvfs_fetch(sqlite3_file *fp, sqlite3_int64 iOfst, int iAmt, void **pp);
int redisvfs_unfetch(sqlite3_file *fp, sqlite3_int64 iOfst, void *p);
/* Methods above are valid for version 3 */
#endif

/* references to file API implementation. Added to each RedisFile *
 */
const sqlite3_io_methods redisvfs_io_methods = {
    1,
    redisvfs_close,
    redisvfs_read,
    redisvfs_write,
    redisvfs_truncate,
    redisvfs_sync,
    redisvfs_fileSize,
    redisvfs_lock,
    redisvfs_unlock,
    redisvfs_checkReservedLock,
    redisvfs_fileControl,
    redisvfs_sectorSize,
    redisvfs_deviceCharacteristics,
};


/*
 * VFS API implementation
 *
 * Stuff that isn't just for a specific already-open file.
 * This all gets referenced by our sqlite3_vfs *
 */

int redisvfs_open(sqlite3_vfs *vfs, const char *zName, sqlite3_file *f, int flags, int *pOutFlags) {
DLOG("(zName='%s',flags=%d)", zName,flags);
#if 0
    if (!(flags & SQLITE_OPEN_MAIN_DB)) {
        return SQLITE_CANTOPEN;
    }
#endif

    //hardcode hostname and port. for now. grab from database URI later
    const char *hostname = REDISVFS_DEFAULT_HOST;
    int port = REDISVFS_DEFAULT_PORT;


    RedisFile *rf = (RedisFile *)f;
    memset(rf, 0, sizeof(RedisFile));
    //  pMethods must be set even if redisvfs_open fails!
    rf->base.pMethods = &redisvfs_io_methods;

    rf->keyprefixlen = strnlen(zName, REDISVFS_MAX_PREFIXLEN+1);
    if (rf->keyprefixlen > REDISVFS_MAX_PREFIXLEN) {
DLOG("key prefix ('filename') length too long");
        return SQLITE_CANTOPEN;
    }
    rf->keyprefix = zName;  // Guaranteed to be unchanged until after xClose(*rf)
DLOG("key prefix: '%s'", rf->keyprefix);

    rf->redisctx = redisConnect(hostname,port);
    if (!(rf->redisctx) || rf->redisctx->err) {
        if (rf->redisctx)
            fprintf(stderr, "%s: Error: %s\n", __func__, rf->redisctx->errstr);
        return SQLITE_CANTOPEN;
    }

    // FIXME: Check if OCREATE
#if 0
    if (!redis_does_block_exist(rf, 0)) {
        DLOG("does not exists. Told.");
        return SQLITE_IOERR;
    }
#endif

    return SQLITE_OK;
}

int redisvfs_delete(sqlite3_vfs *vfs, const char *zName, int syncDir) {
DLOG("(zName='%s',syncDir=%d)",  zName, syncDir);
    // TODO: Better implementation that actually deletes things
    RedisFile rf;
    int openflags;

    if (redisvfs_open(vfs, zName, (sqlite3_file *)(&rf), 0, &openflags) != SQLITE_OK) 
        return SQLITE_IOERR_DELETE;

    if (redis_force_set_filesize(&rf, 0) == REDIS_ERR)
        return SQLITE_IOERR_DELETE;

    redisvfs_close((sqlite3_file *)(&rf));
    return SQLITE_OK;
}
int redisvfs_access(sqlite3_vfs *vfs, const char *zName, int flags, int *pResOut) {
DLOG("(zName='%s', flags=%d (%s%s%s))", zName, flags,
     (flags & SQLITE_ACCESS_EXISTS) == SQLITE_ACCESS_EXISTS ? "SQLITE_ACCESS_EXISTS" : "",
     (flags &  SQLITE_ACCESS_READWRITE) == SQLITE_ACCESS_READWRITE ? "SQLITE_ACCESS_READWRITE" : "",
     (flags &  SQLITE_ACCESS_READ) == SQLITE_ACCESS_READ ? "SQLITE_ACCESS_READ" : "");

//   FIXME: Can only  check redis from a file created with redisvfs_open
    //static bool redis_does_block_exist(RedisFile *rf, int64_t offset) {
    *pResOut = 0;
    return SQLITE_OK;
}
int redisvfs_fullPathname(sqlite3_vfs *vfs, const char *zName, int nOut, char *zOut) {
DLOG("(zName='%s',nOut=%d)", zName,nOut);
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
#ifdef DEBUG_REDISVFS
#define VFS_SHIM_CALL(_callname,_vfs,...) \
    DLOG("%s->" #_callname,PARENT_VFS(_vfs)->zName), \
    PARENT_VFS(_vfs)->_callname(PARENT_VFS(_vfs), __VA_ARGS__)
#else
#define VFS_SHIM_CALL(_callname,_vfs,...) \
    PARENT_VFS(_vfs)->_callname(PARENT_VFS(_vfs), __VA_ARGS__)
#endif

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
    // FIXME: Implement. Called after  another call fails.
    return VFS_SHIM_CALL(xGetLastError, vfs, nBuf, zBuf);
}
int redisvfs_currentTimeInt64(sqlite3_vfs *vfs, sqlite3_int64 *piNow) {
    return VFS_SHIM_CALL(xCurrentTimeInt64, vfs, piNow);
}

/* VFS object for sqlite3 */
sqlite3_vfs redis_vfs = {
    2, 0, REDISVFS_MAX_PREFIXLEN, 0, /* iVersion, szOzFile, mxPathname, pNext */
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


/* Setup VFS structures and initialise */
int redisvfs_register() {
    int ret;

    //FIXME Move out of here. Can be evaluated ompiletime
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
