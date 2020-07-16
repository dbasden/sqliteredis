# Redis block storage implementation

Straight off: It's not a good fit for sqlite to use a network backed datastore
at all, even for a network filesystem that provides harder guarantees than the
block store described here does.   SQLite's documentation gives a good overview
of why this is (and some better options to consider) in their article
[Appropriate Uses For SQLite](https://sqlite.org/whentouse.html)

That out of the way, let's get into how we do this anyway.

# Design goals

 * Simplicity
   * Minimal guarantees
   * Block storage independent from file emulation
 * Fixed block size
 * Partial block reads + writes
 * Sparse block implementation
 * Namespaced

## Simplicity / minimal guarantees

The implementation of the block storage for the sqlite3 redisvfs is
deliberately simple with minimal guarantees.

Although this makes the sqlite3 redisvfs extension code a little uglier
 and more complex,  having a simpler virtual block storage layer
might make it easier in the future to replace redis with a different
distributed block storage layer entirely.

### Block storage independent from file emulation

Although redisvfs emulates files, the file emulation is built on top of
the block storage (or alongside) the simple block storage here.

### Minimal guarantees

The less guarantees the block storage implementation has to provide,
the more flexibility there can be in how to implement the block storage
backend   (e.g. using redis-cluster, or a different scalable distributed
datastore)

## Block store requirements

Any guarantees not explicitly made MUST NOT be relied upon, even if they seem
to work.

### Fixed block size

The blocks should be a fixed size.  This is so we hopefully get the benefits of

* Simple implementation
* Upper bounds on read/write impact to other clients on the same redis server

There is deliberately no requirement for any single read/write operation to
affect multiple blocks and this SHOULD NOT be implemented 

### Partial block reads + writes if possible

* To save on network and IO overhead and to avoid adding fetch/modify/store races, 
reads and writes of less than a whole block should be possible.

* There is deliberately no requirement that a partial block read/write cross a block
boundary (i.e. a single read/write touch multiple blocks) and this SHOULD NOT
be implemented. (This would significantly increase the guarantees the backend would have to provide.)

* A partial write to a block MUST NOT fill the rest of the block with anything
  other than \0s
* Any read (partial or whole) to a block that has been partially filed MUST
  treat parts of the block that have not been written to as be filled with \0s

### Sparse blocks

* Blocks are independent and should be stored sparsely where possible. e.g.

	* Assuming a fixed block size of 1024 bytes, a write to the block at offset
	  819200 will take at most 1024 bytes of storage
	* Assuming a fixed block size of 1024 bytes, a write to the block at offset
	  819200, followed by a  write to the block at 4096 will take at most 2048 bytes of storage
	* Assuming a fixed block size of 1024 bytes, a write to the block at offset
	  8192, followed by a write to the block at 40960 will take at most 2048 bytes of storage

* Any reads from a block that does not exist MUST either be interpreted as the block being zero filled OR fail entirely
* That said, there is no hard requirement that a read succeed where no block has been written before

### Multiple block stores on same backend

The implementation shouldn't get in the way of running multiple different 
blockstores in the same backend server. e.g. in redis, any by adding a prefix to any
keys should be enough for complete independence between prefixes

### IO should be pipelined if possible

* If possible, reads to multiple blocks should be possible without blocking on response for each block.  This is not a hard requirement
* There is deliberately no requirement for pipelining of mixed reads and writes. and this SHOULD NOT be implemented
 e.g. Although the ops  (READ BLOCK 2, READ BLOCK 3, READ BLOCK 4) should be pipelined, but there is no requirement for (READ BLOCK 3, WRITE BLOCK 2, READ BLOCK 4) to be pipelined


### Write barriers

* There must be a way to guarantee all previous writes are available for read.
* It is acceptable for this barrier to be implicit for every write

# Implementation
