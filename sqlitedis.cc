/* A simple C++ wrapper for sqlite3 to allow for easy testing of extensions
 *
 * David Basden <davidb-sqlitedis@oztechninja.com>
 */

#include <iostream>
#include <memory>

#include "sqlite3.h"

#ifdef STATIC_REDISVFS
extern "C" {
#include "redisvfs.h"
}
#endif


class SQLengine {
	sqlite3 *db;

	static int _row_print_callback(void *arg, int ncols, char **cols, char **colnames) {
		for (int i=0; i<ncols; ++i) {
			std::cout << colnames[i] << "=" << cols[i] << "  ";
		}
		std::cout << std::endl;
		return 0;
	}

	public:

	explicit SQLengine(const char *dbName = "database.sqlite",
			int openFlags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI)
	{

		 if (sqlite3_open_v2(dbName, &db, openFlags, NULL)) {
			auto err = std::string("sqlite3_open: ") + dbName +": "+ sqlite3_errmsg(db);
			throw std::runtime_error(err);
		 }
	}
	~SQLengine() {
		sqlite3_close(db);
	}

	
	void exec(const char *sql) {
		char *errmsg = NULL;
		if (sqlite3_exec(db, sql, _row_print_callback, NULL, &errmsg)) {
			auto err = std::string(errmsg);
			sqlite3_free(errmsg);
			throw std::runtime_error(err);
		};

	}

	void loadExtension(const char *sharedLib) {
		char *errmsg = NULL;
		sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 1, NULL);
		if(sqlite3_load_extension(db, sharedLib, NULL, &errmsg)) {
			auto err = std::string(errmsg);
			sqlite3_free(errmsg);
			throw std::runtime_error(err);
		};
	}

	std::string currentVFSname() {
		sqlite3_vfs *vfs;
		sqlite3_file_control(db, "main", SQLITE_FCNTL_VFS_POINTER, &vfs);
		return std::string(vfs->zName);
	}

	static void loadPersistentExtension(const char *sharedLib) {
		// If the know the extension is persistent, we just create a
		// memory backed sqlite db load the extension into it, and then
		// throw away the db
		SQLengine tmpeng(":memory:");
		tmpeng.loadExtension(sharedLib);
	}

	static void dumpvfslist() {
	    std::cerr << "vfs available:";
	    for(sqlite3_vfs *vfs=sqlite3_vfs_find(0); vfs; vfs=vfs->pNext){
		std::cerr << " " << vfs->zName;
	    }
	    std::cerr << std::endl;
	    std::cerr << "default vfs is " << SQLengine::defaultVFS() << std::endl;
	}
	static std::string defaultVFS() {
	    sqlite3_vfs *vfs = sqlite3_vfs_find(0);
	    return std::string(vfs->zName);
	}


};

int main(int argc, const char **argv) {
#ifdef STATIC_REDISVFS
	if (redisvfs_register() != SQLITE_OK) {
		return 1;
	}
#endif

	const char* extname = getenv("SQLITE_LOADEXT");
	if (extname != NULL) {
		//std::cerr << "Loading extension "<<extname <<std::endl;
		SQLengine::loadPersistentExtension(extname);
	}


	if (argc < 2) {
		std::cerr << argv[0] << " <SQL statements>" << std::endl <<
			std::endl << "optional environment variables: SQLITE_DB SQLITE_LOADEXT" <<std::endl;
		SQLengine::dumpvfslist();
		return 1;
	}

	std::shared_ptr<SQLengine> sql;

	char *envdbname = getenv("SQLITE_DB");
	if (envdbname) {
		//std::cerr << "(using database '"<<envdbname<<"' from env SQLITE_DB) " << std::endl;
		sql = std::make_shared<SQLengine>(envdbname);
	}
	else {
		sql = std::make_shared<SQLengine>();
	}

	//std::cerr << "(using vfs " << sql->currentVFSname() << ")" << std::endl;
	sql->exec(argv[1]);

	return 0;
}
