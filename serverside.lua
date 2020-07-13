-- Server-side scripts for using redis as a block store
--
-- See https://redis.io/commands/eval for runtime details.
--
-- Note: hiredis doesn't implement optimistic EVALSHA for us
-- so to keep network IO to a minimum we will want to do that
-- ourselves unless we can fit a script in less than the 43
-- characters that sha1 hexdigest and "SHA" command suffix
-- take up as a minimum.
--
-- All values are assumed by the implemention here to be
-- exactly BLOCKLEN bytes long.  No checking is done on this,
-- as the assumption is that nothing will directly touch these
-- keys outside this script that doesn't maintain the same
-- invariant.

-- Caveats:
--
-- * There is no file emulation here. There is only block storage
--    with a key being treated as a single block.
--
-- * To start with this implementation was only meant to avoid
--   race conditions (and multiple round trips with higher overhead)
--   on partial block writes, and to cut down bandwidth on partial
--   block reads.
--   The scripts have no awareness of any other blocks, and 
--   so even the idea of an ordered set of blocks must be entirely
--   handled by the client.  This leads to some of the edge cases
--   below.
--
-- * A write will create a block if it does not exist (even if
--   only part of the block is written to; the rest is zero filled)
--   but a read will currently fail if a block does not exist. 
--   This leads to inconsistent behaviour for an arbitrary read
--   if the client has not ensured that any block is written to 
--   before write.
--   One case where this might come up is sparse writes followed
--   by a linear read of all blocks.
--
-- * Although there are partial block read/writes, there is no
--   provision for a block < BLOCKLEN.  Anything using this to
--   implement variable length files is going to need to maintain
--   metadata for file length and do boundschecking independently

-- setblock 1 KEYNAME start len data
-- 
-- Does a partial write in the middle of a block
-- If KEYNNAME doesn't exist, it is first created and zerofilled
--
-- PRE: start < BLOCKLEN
--      start+len <= BLOCKLEN
local BLOCKLEN=1024
local KEYNAME=KEYS[1]
local start=ARGV[1]
local len=ARGV[2]
local block
if redis.call('EXISTS',KEYNAME) == 1 then
  block = redis.call('GET',KEYNAME)
else
  block = string.rep("\0", BLOCKLEN)
end

block = string.sub(block,1,start) ..ARGV[3].. string.sub(block,start+len+1)
redis.call('SET',KEYNAME,block)


-- getblock 1 KEYNAME start len
--
-- Does a partial read of a block..
-- Fails if KEYNAME does not exist
--
-- PRE: start < BLOCKLEN
--      start+len <= BLOCKLEN
local block=redis.call('get',KEYS[1])
return string.sub(block,ARGV[1]+1,ARGV[1]+ARGV[2])
