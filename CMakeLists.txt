cmake_minimum_required(VERSION 3.10)

project(sqlitedis VERSION 0.9 LANGUAGES C CXX)
if(NOT CMAKE_BUILD_TYPE)
	  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(sqlitedis sqlitedis.cc)
add_executable(static-sqlitedis redisvfs.c sqlitedis.cc)
add_library(redisvfs SHARED redisvfs.c)

find_library(SQLITE3 sqlite3 REQUIRED)
find_library(HIREDIS hiredis REQUIRED)

# sqlite extension module
# sqlite3 wants us to drop the "lib" prefix for the .so
set_property(TARGET redisvfs PROPERTY POSITION_INDEPENDENT_CODE 1)
set_property(TARGET redisvfs PROPERTY PREFIX "")
target_link_libraries(redisvfs sqlite3 hiredis)

# sqlitedis without redisvfs  (probably should rename as it's a misnomer)
target_link_libraries(sqlitedis sqlite3 hiredis)

# sqlitedis with the redisvfs extension statically linked in rather than
# needing sqlite to dynload it at runtime
target_compile_definitions(static-sqlitedis PUBLIC STATIC_REDISVFS)
target_link_libraries(static-sqlitedis sqlite3 hiredis)
