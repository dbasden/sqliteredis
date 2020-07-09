### Using redis as a block store where
### the 

local BLOCKLEN = 1024

# setblock BLOCKNAME start len DATA
# PRE: start < BLOCKLEN
#      start+len <= BLOCKLEN
local blockname=KEYS[1]
local start=KEYS[2]
local len=KEYS[3]
local block
if redis.call('exists',blockname) == 1 then
  block = redis.call('get',blockname)
else
  block = string.rep(\"\\0\", BLOCKLEN)
end

block = string.sub(block,1,start) ..KEYS[4].. string.sub(block,start+len+1)
redis.call('set',blockname,block)


# getblock BLOCKNAME start len
local block=redis.call('get',KEYS[1])
return string.sub(block,KEYS[2]+1,KEYS[2]+KEYS[3])
