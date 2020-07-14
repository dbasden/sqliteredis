# sqliteredis

SQLite extension to use redis as storage via an emulated VFS

Not yet functionally complete!

This isn't something you should use without realising the horrible, horrible implications of meshing these things together.  The sqlite3 docs go into some detail about why it's a bad idea to back the VFS layer onto a network filesystem, and this has even more edge cases to take into account.

I've only written this as a quick proof of concept at the expense of code quality. It's not as something that should ever be put into production or with data you care about.

### Features / Warnings / Implementation

* Uses redis keys to emulate files
  * Each file is split up into 1024 byte blocks (too small and there is too much network bandwidth/latency overhead.  Too large and the single threaded redis server starts blocking for more than microseconds, starving other clients)
  * Partial block writes are done with redis server-side SCRIPT (lua) to avoid read/modify/write races and to cut down on network overhead
  * Partial block reads also use lua to only send over the wire the part of the block needed
  * Relies on redis ordering and consistency guarantees to have a consistent view from multiple sqlite3 clients on the same database (or even a single client for that matter)
* Uses sqlite3 VFS interface to abstract the emulated files
  * Avoids having to change SQL at all to suit the backend
* Can be dynamically loaded as an sqlite3 extension (.so) or built statically
  * Sets itself as the default VFS on load, so if you can get your app to load sqlite3 extensions, you shouldn't need to change anything else
* database name URI configures redis server

### Build requirements

* hiredis (redis client library for C/C++) https://github.com/redis/hiredis
* A recent sqlite3 (https://sqlite.org/)
* C / C++ compiler
* cmake + GNU Make

### Building the extension

* Make sure sqlite3ext.h is in your include path, or edit the Makefile to point to a build of sqlite3
* Run `make redisvfs.so`

### Building the cli test tool

```
mkdir build
cd build
cmake ..
make
```

See `./test.sh` for examples.  `sqlitedis` needs to be told to load the `redisvfs` extension to talk to redis.  `static-sqlitedis` has the redis VFS compiled in, and uses it by default.

### Alternate implementation options

 * `SETRANGE` and `GETRANGE` are available in Redis >= 2.2 which do substring ops without the need for scripts.
 * Redis strings are still limited to 512MB

### Author

David Basden <davidb-sqliteredis@oztechninja.com>

### License

BSD 3-clause.  See "LICENSE" for more detail
