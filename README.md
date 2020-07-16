# sqliteredis

SQLite extension to use redis as storage via an emulated VFS

I've only written this as a quick proof of concept at the expense of code quality. Don't ever be put into production or with data you care about.

This isn't something you should use without realising the horrible, horrible implications of meshing these things together.  The sqlite3 docs go into some detail about why it's a bad idea to back the VFS layer onto a network filesystem, and this has even more edge cases to take into account.


### Features / Warnings / Implementation

* Uses sqlite3 VFS interface to emulate pseudo-posix file IO (to the bare minimum needed)
* No need to change SQL at all to suit the backend
* Uses redis keys to emulate raw block storage
  * Each file is split up into 1024 byte blocks (too small and there is too much network bandwidth/latency overhead.  Too large and the single threaded redis server may start blocking for more than microseconds, starving other clients)
  * Sparse block implementation (reading *only* a sparse area may or may not work but sqlite does not seem to do this)
  * Partial block reads/writes are done with GETRANGE/SETRANGE avoid read/modify/write races and to cut down on network overhead
  * Relies on redis ordering and consistency guarantees to have a consistent view from multiple sqlite3 clients on the same database (entirely untested)
* Uses different redis keys to emulate a "file" on top of the block store
  * Tracks file lengths on write.
  * Allows truncation  (current lazy implementation: only filesize metadata is changed)
* Multiple sqlite databases  supported on the same redis server (current "filename" used as a prefix in redis keyspace)
* Can be dynamically loaded as an sqlite3 extension (.so) or built statically
  * Sets itself as the default VFS on load, so if you can get your app to load sqlite3 extensions, you shouldn't need to change anything else
* Redis server connection defaults to locahost:6379, or (TODO) set in database connection URI as option

### Build requirements

* hiredis (redis client library for C/C++) https://github.com/redis/hiredis
* A recent sqlite3 (https://sqlite.org/)
* C / C++ compiler
* cmake + GNU Make

### Building the extension and cli test tool

```
mkdir build
cd build
cmake ..
make

# Test
../test.sh
```

See `./test.sh` for examples.  `sqlitedis` needs to be told to load the `redisvfs` extension to talk to redis.  `static-sqlitedis` has the redis VFS compiled in, and uses it by default.


### Author

David Basden <davidb-sqliteredis@oztechninja.com>

### License

BSD 3-clause.  See "LICENSE" for more detail
