#include <iostream>
#include <memory>

#include <sqlite3.h>



class SQLengine {
	sqlite3 *db;

	static int _row_print_callback(void *arg, int ncols, char **cols, char **colnames) {
		for (int i=0; i<ncols; ++i) {
			std::cout << colnames[i] << "=" << cols[i] << "  ";
		}
		return 0;
	}

	public:

	explicit SQLengine(const char *dbName = ":memory:",
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

	void dumpvfslist() {
	    for(sqlite3_vfs *pVfs=sqlite3_vfs_find(0); pVfs; pVfs=pVfs->pNext){
		std::cout << "vfs zName      = " << pVfs->zName << std::endl
			  << "    iVersion   = " << pVfs->iVersion << std::endl
			  << "    szOsFile   = " << pVfs->szOsFile << std::endl
			  << "    mxPathname = " << pVfs->mxPathname << std::endl
			  << std::endl;
	    }
	}

	void extensionLoad(const char *sharedLib) {
		char *errmsg = NULL;
		sqlite3_db_config(db,SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION,1,NULL);
		if(sqlite3_load_extension(db, sharedLib, NULL, &errmsg)) {
			auto err = std::string(errmsg);
			sqlite3_free(errmsg);
			throw std::runtime_error(err);
		};
	}

};

int main(int argc, const char **argv) {
	if (argc < 2) {
		std::cerr << argv[0] << " < SQL statements>" << std::endl <<
			std::endl << "optional environment variables: SQLITE_DB SQLITE_LOADEXT" <<std::endl;
		return 1;
	}

	auto sql = std::make_shared<SQLengine>(getenv("SQLITE_DB"));

	const char* extname = getenv("SQLITE_LOADEXT");
	if (extname != NULL) {
		sql->extensionLoad(extname);
	}

	sql->dumpvfslist();
	
	sql->exec(argv[1]);

	return 0;
}
