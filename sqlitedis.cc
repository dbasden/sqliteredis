#include <iostream>
#include <memory>

#include <sqlite3.h>

class SQLengine {
	sqlite3 *db;

	static int _row_print_callback(void *arg, int ncols, char **cols, char **colnames) {
		for (int i=0; i<ncols; ++i) {
			std::cout << colnames[i] << "=" << cols[i] << std::endl;
		}
		return 0;
	}

	public:
	explicit SQLengine() {
		 if (sqlite3_open("database", &db)) {
			auto err = std::string("sqlite3_open: ") + sqlite3_errmsg(db);
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

};

int main(int argc, const char **argv) {
	if (argc < 2) {
		std::cerr << argv[0] << " < SQL statement >" << std::endl;
		return 1;
	}
	auto sql = std::make_shared<SQLengine>();
	
	sql->exec(argv[1]);

	return 0;
}
