#pragma once

#include <vector>

struct filerepo_headers {
	enum {
		CHANGE_ADD = 0,
		CHANGE_REMOVE,
		CHANGE_UPDATE,

		CHANGE_DIRECTORY_RENAME,

		QUERY_FILES,

		// INTERNAL BELOW!
		PARSER_DONE,
		DIRECTORIES
	};
};

struct DirectoryHeader {
	unsigned length; // as in strlen
};

struct ChangeHeader {
	unsigned buffer_size;
	unsigned num_records;
};

/*
 *	- record_size is the size (in bytes) of the WHOLE record (including this header)
 *	- FULL filename (with path starts after the header)
 *	- filename starts at start+offset (where offset is characters)
 *	- filename is in characters(INCLUDING NULL-TERMINATOR)
 *
 *	To get data we cast to pointer of character type (wchar_t or char) :
 *		char *header_start = x;
 *		wchar_t *start_file = (header_start)+sizeof(RecordHeader)
 *		wchar_t *date_start = (start_file+filename_offset+filename_length)
 *
 *
 */
struct RecordHeader {
	unsigned short record_size;
	unsigned char filename_offset;
	unsigned char filename_length;
};

namespace filerepo {
	namespace aux {
		RecordHeader make_recordheader(const wchar_t *fullname, unsigned datestring_len);

		void pack_changeheader(std::vector<char> &s, unsigned changetype, const wchar_t *fullname, unsigned datestring_len, const wchar_t *datestring);

		// will keep db sorted
		void insert_filerecord(std::vector<char> &db, const wchar_t *filename, const wchar_t *date);

		// merge two SORTED db's
		void merge_dbs(std::vector<char> &db, const char *db2, unsigned db2_size);

		// remove all in db2 from db
		void exclude_db(std::vector<char> &db, const char *db2, unsigned db2_size);

		// add (or if existing replace) to db from db2
		void add_replace_db(std::vector<char> &db, const char *db2, unsigned db2_size);

		unsigned search_db(std::vector<char> &result,
							const std::vector<char> &db,
							unsigned search_all,
							unsigned char num_include,
							unsigned char num_exclude,
							const wchar_t *include,
							const wchar_t *exclude,
							std::vector<char> &delayed_events);

		void rename_directory(std::vector<char> &db, const wchar_t *from, const wchar_t *to, std::vector<char> &temp_buffer);
	}


	void make_internal_datestring_st(wchar_t *res,const void *stin);
	void make_internal_datestring_ft(wchar_t *res, const void *ft);
}
